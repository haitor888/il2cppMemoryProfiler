#include "umputils.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>

bool umpIsLibraryLoaded(const std::string& lib, std::string& libraryPath) {
    char                                   line[512]; // proc/self/maps parsing code by xhook
    FILE                                  *fp;
    uintptr_t                              baseAddr;
    char                                   perm[5];
    unsigned long                          offset;
    int                                    pathNamePos;
    char                                  *pathName;
    size_t                                 pathNameLen;
    if (NULL == (fp = fopen("/proc/self/maps", "r"))) 
        return false;
    while(fgets(line, sizeof(line), fp)) {
        if(sscanf(line, "%" PRIxPTR"-%*lx %4s %lx %*x:%*x %*d%n", &baseAddr, perm, &offset, &pathNamePos) != 3) continue;
        // check permission & offset
        if(perm[0] != 'r') continue;
        if(perm[3] != 'p') continue; // do not touch the shared memory
        if(0 != offset) continue;
        // get pathname
        while(isspace(line[pathNamePos]) && pathNamePos < (int)(sizeof(line) - 1))
            pathNamePos += 1;
        if(pathNamePos >= (int)(sizeof(line) - 1)) continue;
        pathName = line + pathNamePos;
        pathNameLen = strlen(pathName);
        if(0 == pathNameLen) continue;
        if(pathName[pathNameLen - 1] == '\n') {
            pathName[pathNameLen - 1] = '\0';
            pathNameLen -= 1;
        }
        if(0 == pathNameLen) continue;
        if('[' == pathName[0]) continue;
        // check path
        auto pathnameStr = std::string(pathName);
        if (pathnameStr.find(lib) != std::string::npos) {
            libraryPath = pathnameStr;
            UMPLOGI("%s is loaded", pathnameStr.c_str());
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
    return false;
}

#ifdef __cplusplus
}
#endif // __cplusplus