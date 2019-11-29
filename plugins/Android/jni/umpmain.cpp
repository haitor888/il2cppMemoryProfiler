#include <iomanip>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/looper.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <jni.h>
#include <unistd.h>

#include "buffer.h"
#include "umpmemory.h"
#include "umpserver.h"
#include "umputils.h"

Il2CppManagedMemorySnapshot* (*umpCaptureMemorySnapshot_)(void);
void (*umpFreeCapturedMemorySnapshot_)(Il2CppManagedMemorySnapshot*);

ALooper* mainThreadLooper_;
int messagePipe_[2];
io::buffer buffer((size_t)0, 256 * 1024 * 1024);

int umpMainThreadLooperCallback(int fd, int, void*) {
    char msg;
    read(fd, &msg, 1);
    if (umpCaptureMemorySnapshot_ != nullptr && umpFreeCapturedMemorySnapshot_ != nullptr) {
        auto snapshot = umpCaptureMemorySnapshot_();
        UMPLOGI("Snapshot! heaps: %i, stacks: %i, types: %i, gcHandles: %i, %u", 
            snapshot->heap.sectionCount, snapshot->stacks.stackCount, snapshot->metadata.typeCount, snapshot->gcHandles.trackedObjectCount, kSnapshotMagicBytes);
        io::bufferwriter writer(buffer);
        writer << kSnapshotMagicBytes << kSnapshotFormatVersion;
        writer << kSnapshotHeapMagicBytes << snapshot->heap.sectionCount;
        for (std::uint32_t i = 0; i < snapshot->heap.sectionCount; i++) {
            auto& section = snapshot->heap.sections[i];
            writer << section.sectionStartAddress << section.sectionSize;
            writer.append(section.sectionBytes, section.sectionSize);
        }
        writer << kSnapshotStacksMagicBytes << snapshot->stacks.stackCount;
        for (std::uint32_t i = 0; i < snapshot->stacks.stackCount; i++) {
            auto& stack = snapshot->stacks.stacks[i];
            writer << stack.sectionStartAddress << stack.sectionSize;
            writer.append(&stack.sectionBytes, stack.sectionSize);
        }
        writer << kSnapshotMetadataMagicBytes << snapshot->metadata.typeCount;
        for (std::uint32_t i = 0; i < snapshot->metadata.typeCount; i++) {
            auto& type = snapshot->metadata.types[i];
            writer << static_cast<std::uint32_t>(type.flags) << type.baseOrElementTypeIndex;
            if ((type.flags & Il2CppMetadataTypeFlags::kArray) == 0) {
                writer << type.fieldCount;
                for (std::uint32_t j = 0; j < type.fieldCount; j++) {
                    auto& field = type.fields[j];
                    writer << field.offset << field.typeIndex << field.name << field.isStatic;
                }
                writer << type.staticsSize;
                writer.append(type.statics, type.staticsSize);
            }
            writer << type.name << type.assemblyName << type.typeInfoAddress << type.size;
        }
        writer << kSnapshotGCHandlesMagicBytes << snapshot->gcHandles.trackedObjectCount;
        for (std::uint32_t i = 0; i < snapshot->gcHandles.trackedObjectCount; i++) {
            writer << snapshot->gcHandles.pointersToObjects[i];
        }
        writer << kSnapshotRuntimeInfoMagicBytes;
        writer << snapshot->runtimeInformation.pointerSize << snapshot->runtimeInformation.objectHeaderSize << 
                snapshot->runtimeInformation.arrayHeaderSize << snapshot->runtimeInformation.arrayBoundsOffsetInHeader << 
                snapshot->runtimeInformation.arraySizeOffsetInHeader << snapshot->runtimeInformation.allocationGranularity;
        writer << kSnapshotTailMagicBytes;
        umpSend(buffer.data(), buffer.size()); // blocking
        buffer.clear();
        umpFreeCapturedMemorySnapshot_(snapshot);
    }
    return 1; // continue listening for events
}

void umpOnRecvMessage(unsigned int type, const char* data, unsigned int size) {
    (void)data;
    (void)size;
    if (type == UMPMessageType::CAPTURE_SNAPSHOT) {
        char empty = 255;
        write(messagePipe_[1], &empty, 1);
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    UMPLOGI("JNI_OnLoad");
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }
    mainThreadLooper_ = ALooper_forThread(); // get looper for this thread
    ALooper_acquire(mainThreadLooper_); // add reference to keep object alive
    pipe(messagePipe_); //create send-receive pipe
    // listen for pipe read end, if there is something to read
    // - notify via provided callback on main thread
    ALooper_addFd(mainThreadLooper_, messagePipe_[0],
                  0, ALOOPER_EVENT_INPUT, umpMainThreadLooperCallback, nullptr);
    std::thread([]() {
        while (true) {
            std::string il2cppPath;
            if (umpIsLibraryLoaded("libil2cpp", il2cppPath)) {
                char *error;
                void *handle = dlopen(il2cppPath.c_str(), RTLD_LAZY);
                if (handle) {
                    dlerror();
                    *(void **) (&umpCaptureMemorySnapshot_) = dlsym(handle, "il2cpp_capture_memory_snapshot");
                    if ((error = dlerror()) != NULL)  {
                        UMPLOGE("Error dlsym il2cpp_capture_memory_snapshot: %s", error);
                    }
                    dlerror();
                    *(void **) (&umpFreeCapturedMemorySnapshot_) = dlsym(handle, "il2cpp_free_captured_memory_snapshot");
                    if ((error = dlerror()) != NULL)  {
                        UMPLOGE("Error dlsym il2cpp_free_captured_memory_snapshot: %s", error);
                    }
                } else {
                    UMPLOGE("Error dlopen: %s", dlerror());
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }).detach();
    auto ecode = umpServerStart(7100);
    umpRecv(umpOnRecvMessage);
    UMPLOGI("UMP server start status %i", ecode);
    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif // __cplusplus