#include "umpserver.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <malloc.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "umputils.h"

struct umpSendCache {
    std::uint32_t size;
    const char* data;
};

std::mutex sendCacheMutex_;
umpSendCache sendCache_;
void (*recvCallback_)(unsigned int, const char*, unsigned int);

char* buffer_ = nullptr;
std::atomic<bool> serverRunning_ {true};
std::atomic<bool> hasClient_ {false};
std::thread socketThread_;
bool started_ = false;

bool umpServerStarted() {
    return started_;
}

void umpSend(const char* buffer, unsigned int size) {
    {
        std::lock_guard<std::mutex> lock(sendCacheMutex_);
        sendCache_.size = size;
        sendCache_.data = buffer;
        UMPLOGI("Sending Snapshot size: %i", size);
    }
    if (size > 0) {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(sendCacheMutex_);
                if (sendCache_.size == 0)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void umpRecv(void (*recvCallback)(unsigned int, const char*, unsigned int)) {
    recvCallback_ = recvCallback;
}

void umpServerLoop(int sock);

int umpServerStart(int port = 8000) {
    if (started_)
        return 0;
    // allocate buffer
    buffer_ = (char*)malloc(BUFSIZ);
    memset(buffer_, 0, BUFSIZ);
    // setup server addr
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    // create socket
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        UMPLOGI("start.socket %i", sock);
        return -1;
    }
    // bind address
    int ecode = bind(sock, (struct sockaddr*)&serverAddr, sizeof(struct sockaddr));
    if (ecode < 0) {
        UMPLOGI("start.bind %i", ecode);
        return -1;
    }
    // set max send buffer
    int sendbuff = 327675;
    ecode = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
    if (ecode < 0) {
        UMPLOGI("start.setsockopt %i", ecode);
        return -1;
    }
    // listen for incomming connections
    ecode = listen(sock, 2);
    if (ecode < 0) {
        UMPLOGI("start.listen %i", ecode);
        return -1;
    }
    started_ = true;
    serverRunning_ = true;
    hasClient_ = false;
    socketThread_ = std::thread(umpServerLoop, sock);
    return 0;
}

void umpServerShutdown() {
    if (!started_)
        return;
    serverRunning_ = false;
    hasClient_ = false;
    socketThread_.join();
    delete[] buffer_;
    buffer_ = nullptr;
    started_ = false;
}

void umpServerLoop(int sock) {
    struct timeval time;
    time.tv_usec = 33;
    fd_set fds;
    int clientSock = -1;
    while (serverRunning_) {
        if (!serverRunning_)
            break;
        if (!hasClient_) { // handle new connection
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            if (select(sock + 1, &fds, NULL, NULL, &time) < 1)
                continue;
            if (FD_ISSET(sock, &fds)) {
                clientSock = accept(sock, NULL, NULL);
                if (clientSock >= 0) {
                    hasClient_ = true;
                }
            }
        } else {
            // check for client connectivity
            FD_ZERO(&fds);
            FD_SET(clientSock, &fds);
            if (select(clientSock + 1, &fds, NULL, NULL, &time) > 0 && FD_ISSET(clientSock, &fds)) {
                int length = recv(clientSock, buffer_, BUFSIZ, 0);
                if (length <= 0) {
                    hasClient_ = false;
                    continue;
                } else {
                    if (length > 0) {
                        std::uint32_t type = ntohl(*reinterpret_cast<std::uint32_t*>(buffer_));
                        recvCallback_(type, buffer_ + 4, static_cast<std::uint32_t>(length - 4));
                    }
                }
            }
            std::lock_guard<std::mutex> lock(sendCacheMutex_);
            if (sendCache_.size > 0) {
                send(clientSock, &sendCache_.size, 4, 0); // send net buffer size
                send(clientSock, sendCache_.data, sendCache_.size, 0); // then send data
                // UMPLOGI("sending: %i, %i", sendCache_.compressedSize, sendCache_.size);
                sendCache_.size = 0;
            }
        }
    }
    close(sock);
    if (hasClient_)
        close(clientSock);
}

#ifdef __cplusplus
}
#endif // __cplusplus