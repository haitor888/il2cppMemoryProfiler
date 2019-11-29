#include "remoteprocess.h"

#include "umpmemory.h"

#include <QtEndian>
#include <QTcpSocket>
#include <QTextStream>
#include <QDataStream>
#include <QRegularExpression>
#include <QProcess>
#include <QDebug>

void Il2CppFreeMemorySnapshot(Il2CppManagedMemorySnapshot* snapshot) {
    if (snapshot->heap.sectionCount > 0) {
        for (uint32_t i = 0; i < snapshot->heap.sectionCount; i++) {
            delete[] snapshot->heap.sections[i].sectionBytes;
        }
        delete[] snapshot->heap.sections;
        snapshot->heap.sectionCount = 0;
        snapshot->heap.sections = nullptr;
    }
    if (snapshot->stacks.stackCount > 0) {
        for (uint32_t i = 0; i < snapshot->stacks.stackCount; i++) {
            delete[] snapshot->stacks.stacks[i].sectionBytes;
        }
        delete[] snapshot->stacks.stacks;
        snapshot->stacks.stackCount = 0;
        snapshot->stacks.stacks = nullptr;
    }
    if (snapshot->gcHandles.trackedObjectCount > 0) {
        delete[] snapshot->gcHandles.pointersToObjects;
        snapshot->gcHandles.pointersToObjects = nullptr;
        snapshot->gcHandles.trackedObjectCount = 0;
    }
    if (snapshot->metadata.typeCount > 0) {
        for (uint32_t i = 0; i < snapshot->metadata.typeCount; i++) {
            auto& type = snapshot->metadata.types[i];
            if ((type.flags & kArray) == 0) {
                for (uint32_t j = 0; j < type.fieldCount; j++) {
                    auto& field = type.fields[j];
                    delete[] field.name;
                }
                delete[] type.fields;
                delete[] type.statics;
            }
            delete[] type.name;
            delete[] type.assemblyName;
        }
        delete[] snapshot->metadata.types;
        snapshot->metadata.types = nullptr;
        snapshot->metadata.typeCount = 0;
    }
}

#define BUFFER_SIZE 65535

RemoteProcess::RemoteProcess(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this)) {
    buffer_ = new char[BUFFER_SIZE];
    compressBuffer_ = new char[compressBufferSize_];
    snapShot_ = new Il2CppManagedMemorySnapshot();
    socket_->setReadBufferSize(BUFFER_SIZE);
    connect(socket_, &QTcpSocket::readyRead, this, &RemoteProcess::OnDataReceived);
    connect(socket_, &QTcpSocket::connected, this, &RemoteProcess::OnConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &RemoteProcess::OnDisconnected);
}

RemoteProcess::~RemoteProcess(){
    delete[] buffer_;
    delete[] compressBuffer_;
    Il2CppFreeMemorySnapshot(snapShot_);
    delete snapShot_;
}

void RemoteProcess::ConnectToServer(int port) {
    QProcess process;
    QStringList arguments;
    arguments << "forward" << "tcp:" + QString::number(port) << "tcp:7100";
    process.setProgram(execPath_);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted()) {
        emit ConnectionLost();
        return;
    }
    if (!process.waitForFinished()) {
        emit ConnectionLost();
        return;
    }
    connectingServer_ = true;
    socket_->connectToHost("127.0.0.1", static_cast<quint16>(port));
}

void RemoteProcess::Disconnect() {
    socket_->close();
    packetSize_ = 0;
}

void RemoteProcess::Send(UMPMessageType type) {
    auto typeData = static_cast<std::uint32_t>(type);
    socket_->write(reinterpret_cast<const char*>(&typeData), 4);
}

void RemoteProcess::Interpret(const QByteArray& bytes) {
    auto packetSize = bytes.size();
    if (!DecodeData(bytes.data(), static_cast<std::size_t>(packetSize))) {
        qDebug() << "Decode failed";
        Il2CppFreeMemorySnapshot(snapShot_);
        return;
    }
    qDebug() << "Snapshot heaps: " << snapShot_->heap.sectionCount << " stacks " << snapShot_->stacks.stackCount << " types " <<
                snapShot_->metadata.typeCount << " gcHandles " << snapShot_->gcHandles.trackedObjectCount;
    emit DataReceived();
}

bool RemoteProcess::DecodeData(const char* data, size_t size) {
    Il2CppFreeMemorySnapshot(snapShot_);
    if (size < 8)
        return false;
    bufferreader reader(data, size);
    std::uint32_t magic, version;
    reader >> magic >> version;
    if (magic != kSnapshotMagicBytes) {
        qDebug() << "Invalide MagicBytes!" << magic << kSnapshotMagicBytes;
        return false;
    }
    if (version > kSnapshotFormatVersion) {
        qDebug() << "Version Missmatch!";
        return false;
    }
    while (!reader.atEnd()) {
        reader >> magic;
        if (magic == kSnapshotHeapMagicBytes) {
            reader >> snapShot_->heap.sectionCount;
            snapShot_->heap.sections = new Il2CppManagedMemorySection[snapShot_->heap.sectionCount];
            for (std::uint32_t i = 0; i < snapShot_->heap.sectionCount; i++) {
                auto& section = snapShot_->heap.sections[i];
                reader >> section.sectionStartAddress >> section.sectionSize;
                section.sectionBytes = new std::uint8_t[section.sectionSize];
                reader.read(reinterpret_cast<char*>(section.sectionBytes), section.sectionSize);
            }
        } else if (magic == kSnapshotStacksMagicBytes) {
            reader >> snapShot_->stacks.stackCount;
            snapShot_->stacks.stacks = new Il2CppManagedMemorySection[snapShot_->stacks.stackCount];
            for (std::uint32_t i = 0; i < snapShot_->stacks.stackCount; i++) {
                auto& section = snapShot_->stacks.stacks[i];
                reader >> section.sectionStartAddress >> section.sectionSize;
                section.sectionBytes = new std::uint8_t[section.sectionSize];
                reader.read(reinterpret_cast<char*>(section.sectionBytes), section.sectionSize);
            }
        } else if (magic == kSnapshotMetadataMagicBytes) {
            reader >> snapShot_->metadata.typeCount;
            snapShot_->metadata.types = new Il2CppMetadataType[snapShot_->metadata.typeCount];
            for (std::uint32_t i = 0; i < snapShot_->metadata.typeCount; i++) {
                auto& type = snapShot_->metadata.types[i];
                std::uint32_t flags;
                reader >> flags >> type.baseOrElementTypeIndex;
                type.flags = static_cast<Il2CppMetadataTypeFlags>(flags);
                if ((type.flags & Il2CppMetadataTypeFlags::kArray) == 0) {
                    reader >> type.fieldCount;
                    type.fields = new Il2CppMetadataField[type.fieldCount];
                    for (uint32_t j = 0; j < type.fieldCount; j++) {
                        auto& field = type.fields[j];
                        reader >> field.offset >> field.typeIndex >> field.name >> field.isStatic;
                    }
                    reader >> type.staticsSize;
                    type.statics = new std::uint8_t[type.staticsSize];
                    reader.read(reinterpret_cast<char*>(type.statics), type.staticsSize);
                } else {
                    type.statics = nullptr;
                    type.staticsSize = 0;
                    type.fields = nullptr;
                    type.fieldCount = 0;
                }
                reader >> type.name >> type.assemblyName >> type.typeInfoAddress >> type.size;
            }
        } else if (magic == kSnapshotGCHandlesMagicBytes) {
            reader >> snapShot_->gcHandles.trackedObjectCount;
            snapShot_->gcHandles.pointersToObjects = new std::uint64_t[snapShot_->gcHandles.trackedObjectCount];
            for (std::uint32_t i = 0; i < snapShot_->gcHandles.trackedObjectCount; i++) {
                reader >> snapShot_->gcHandles.pointersToObjects[i];
            }
        } else if (magic == kSnapshotRuntimeInfoMagicBytes) {
            reader >> snapShot_->runtimeInformation.pointerSize >> snapShot_->runtimeInformation.objectHeaderSize >>
                    snapShot_->runtimeInformation.arrayHeaderSize >> snapShot_->runtimeInformation.arrayBoundsOffsetInHeader >>
                    snapShot_->runtimeInformation.arraySizeOffsetInHeader >> snapShot_->runtimeInformation.allocationGranularity;
        } else if (magic == kSnapshotTailMagicBytes) {
            break;
        } else {
            qDebug() << "Unknown Section!";
            return false;
        }
    }
    return true;
}

void RemoteProcess::OnDataReceived() {
    auto size = socket_->read(buffer_, BUFFER_SIZE);
    if (size <= 0)
        return;
    qint64 bufferPos = 0; // pos = size - remains
    qint64 remainBytes = size;
    while (remainBytes > 0) {
        if (packetSize_ == 0) {
            packetSize_ = *reinterpret_cast<quint32*>(buffer_ + bufferPos);
//            qDebug() << "receiving: " <<  packetSize_;
            remainBytes -= 4;
            bufferPos = size - remainBytes;
            bufferCache_.resize(0);
            if (remainBytes > 0) {
                if (packetSize_ < remainBytes) { // the data is stored in the same packet, then read them all
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(packetSize_));
                    remainBytes -= packetSize_;
                    bufferPos = size - remainBytes;
                    Interpret(bufferCache_);
                    bufferCache_.resize(0);
                    packetSize_ = 0;
                } else { // the data is splited to another packet, we just append whatever we got
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                    break;
                }
            }
        } else {
            auto remainPacketSize = packetSize_ - static_cast<uint>(bufferCache_.size());
            if (remainPacketSize <= remainBytes) { // the remaining data is stored in this packet, read what we need
                bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainPacketSize));
                remainBytes -= remainPacketSize;
                bufferPos = size - remainBytes;
                Interpret(bufferCache_);
                bufferCache_.resize(0);
                packetSize_ = 0;
            } else { // the remaining data is splited to another packet, we just append whatever we got
                bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                break;
            }
        }
    }
}

void RemoteProcess::OnConnected() {
    connectingServer_ = false;
    serverConnected_ = true;
}

void RemoteProcess::OnDisconnected() {
    connectingServer_ = false;
    serverConnected_ = false;
    emit ConnectionLost();
}
