LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := torchd
LOCAL_SRC_FILES := torchd.c
LOCAL_CFLAGS    := -Wall -Wextra -O2 -fPIE
LOCAL_LDFLAGS   := -fPIE -pie
include $(BUILD_EXECUTABLE)
