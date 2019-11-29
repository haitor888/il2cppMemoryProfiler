#ifndef UMPSERVER_H
#define UMPSERVER_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

enum UMPMessageType {
    CAPTURE_SNAPSHOT = 0, 
};

int umpServerStart(int port);
bool umpServerStarted();
void umpSend(const char* buffer, unsigned int size);
void umpRecv(void (*recvCallback)(unsigned int, const char*, unsigned int));
void umpServerShutdown();

#ifdef __cplusplus
}
#endif // __cplusplus

#endif