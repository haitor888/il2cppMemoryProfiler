LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := libumemperf
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS     := -Wall -Wextra -Werror -fvisibility=hidden -std=c11
LOCAL_CXXFLAGS   := -Wall -Wextra -Werror -fvisibility=hidden -std=c++11
LOCAL_SRC_FILES  := umpmain.cpp \
					umpserver.cpp \
					umputils.cpp
LOCAL_LDLIBS     := -llog -latomic -landroid

include $(BUILD_SHARED_LIBRARY)
