#ifndef UMPUTILS_H
#define UMPUTILS_H

#include <string>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>

#define UMPLOGI(...) __android_log_print(ANDROID_LOG_INFO, "UMP", __VA_ARGS__)
#define UMPLOGW(...) __android_log_print(ANDROID_LOG_WARN, "UMP", __VA_ARGS__)
#define UMPLOGE(...) __android_log_print(ANDROID_LOG_ERROR, "UMP", __VA_ARGS__)

bool umpIsLibraryLoaded(const std::string& lib, std::string& libraryPath);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif