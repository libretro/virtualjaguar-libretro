LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM
LOCAL_ARM_MODE := arm
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS +=  -DANDROID_X86
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS
endif

LOCAL_MODULE    := libretro

CORE_DIR     := ../src
LIBRETRO_DIR := ..

include ../Makefile.common

LOCAL_SRC_FILES := $(SOURCES_CXX) $(SOURCES_C)

LOCAL_CFLAGS = -O3 -DINLINE=inline -DLSB_FIRST -D__LIBRETRO__ -DFRONTEND_SUPPORTS_RGB565 -D__GCCUNIX__ $(INCFLAGS)

LOCAL_STATIC_LIBRARIES +=  libstlport

LOCAL_C_INCLUDES += external/stlport/stlport 
LOCAL_C_INCLUDES += bionic

include $(BUILD_SHARED_LIBRARY)
