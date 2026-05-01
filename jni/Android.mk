LOCAL_PATH := $(call my-dir)/..

CORE_DIR := $(LOCAL_PATH)

include $(CORE_DIR)/Makefile.common

COREFLAGS := -DINLINE="inline" -D__LIBRETRO__ $(INCFLAGS)

# libretro.c includes the generated src/core/version.h.  Generate it
# at parse time -- ndk-build doesn't go through the project Makefile,
# so the parse-time $(shell ...) there doesn't fire for us.
_VERSION_GEN := $(shell sh $(CORE_DIR)/scripts/gen-version-h.sh && echo ok)

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C)
LOCAL_CFLAGS    := $(COREFLAGS)
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/link.T
include $(BUILD_SHARED_LIBRARY)
