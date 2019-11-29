#ifndef STACKTRACEPROCESS_H
#define STACKTRACEPROCESS_H

#include <QObject>
#include <QVector>

enum class UMPMessageType : std::uint32_t {
    CAPTURE_SNAPSHOT = 0,
};

struct Il2CppManagedMemorySnapshot;
class QTcpSocket;
class RemoteProcess : public QObject {
    Q_OBJECT
public:
    RemoteProcess(QObject* parent = nullptr);
    ~RemoteProcess() override;

    void ConnectToServer(int port);
    void Disconnect();
    bool IsConnecting() const { return connectingServer_; }
    bool IsConnected() const { return serverConnected_; }

    void Send(UMPMessageType type);
    const Il2CppManagedMemorySnapshot* GetSnapShot() const { return snapShot_; }
    Il2CppManagedMemorySnapshot* GetSnapShot() { return snapShot_; }

    void SetExecutablePath(const QString& str) { execPath_ = str; }
    const QString& GetExecutablePath() const { return execPath_; }

signals:
    void DataReceived();
    void ConnectionLost();

private:
    void Interpret(const QByteArray& bytes);
    bool DecodeData(const char* data, size_t size);
    void OnDataReceived();
    void OnConnected();
    void OnDisconnected();

private:
    QString execPath_;
    QTcpSocket* socket_ = nullptr;
    bool connectingServer_ = false;
    bool serverConnected_ = false;
    quint32 packetSize_ = 0;
    char* buffer_ = nullptr;
    char* compressBuffer_ = nullptr;
    quint32 compressBufferSize_ = 1024;
    QByteArray bufferCache_;
    Il2CppManagedMemorySnapshot *snapShot_ = nullptr;
};

#endif // STACKTRACEPROCESS_H
