LOCAL_PATH := $(call my-dir)/..

CORE_DIR := $(LOCAL_PATH)

# Blitter SIMD arch, picked from the ABI ndk-build is currently building.
# Makefile.common's own detection can't do it here: `platform` and `ARCH`
# are unset under ndk-build and $(CC) isn't a cross-compiler triple it
# recognises, so every Android ABI -- including x86_64 -- silently landed
# on the scalar blitter.
#
# Reset first. With APP_ABI=all, ndk-build re-includes this file once per
# ABI in the same make process, so a stale value from the previous ABI
# would leak (arm64-v8a then riscv64 would try to build the NEON source
# for RISC-V).
#
# Why each mapping is safe:
#   arm64-v8a    -- NEON is mandatory in ARMv8-A.
#   armeabi-v7a  -- NDK r21+ builds armeabi-v7a with NEON by default and
#                   r26 dropped non-NEON ARMv7 entirely; CI pins r26d.
#   x86 / x86_64 -- the NDK x86 ABI baseline is SSSE3, which subsumes
#                   SSE2, so no -msse2 is needed on either.
# Anything else (riscv64, ...) leaves BLITTER_SIMD empty and falls
# through to Makefile.common's scalar default.
BLITTER_SIMD :=
ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
   BLITTER_SIMD := neon
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
   BLITTER_SIMD := neon
else ifeq ($(TARGET_ARCH_ABI),x86_64)
   BLITTER_SIMD := sse2
else ifeq ($(TARGET_ARCH_ABI),x86)
   BLITTER_SIMD := sse2
endif

include $(CORE_DIR)/Makefile.common

# $(BLITTER_SIMD_DEFINE) is what makes blitter.c inline the same ops the
# arch .c compiles. Without it blitter.c falls back to the scalar inline
# set -- which links fine against the neon/sse2 vtable and just runs
# slower, so nothing would catch its absence at build time.
#
# -O3 -DNDEBUG -fvisibility=hidden -fno-stack-protector
# -fomit-frame-pointer -fno-semantic-interposition: the release
# optimisation/visibility flags from the desktop Makefile (issue #569),
# added by hand because ndk-build never includes Makefile -- only
# Makefile.common -- so without this line Android inherited none of
# them and built at whatever default APP_OPTIM/-O ndk-build picks.
# LOCAL_LDFLAGS below already pins the production link.T version
# script, same as every other ELF target; -fvisibility=hidden is safe
# alongside it because libretro.h declares every retro_* entry point
# RETRO_API (visibility("default")), so those symbols stay exported.
COREFLAGS := -DINLINE="inline" -D__LIBRETRO__ $(INCFLAGS) $(BLITTER_SIMD_DEFINE) \
             -O3 -DNDEBUG -fvisibility=hidden -fno-stack-protector \
             -fomit-frame-pointer -fno-semantic-interposition

# libretro.c includes the generated src/core/version.h.  Generate it
# at parse time -- ndk-build doesn't go through the project Makefile,
# so the parse-time $(shell ...) there doesn't fire for us.
_VERSION_GEN := $(shell sh $(CORE_DIR)/scripts/gen-version-h.sh && echo ok)

# libchdr is C99. A separate static module so -std=c99 never lands on
# first-party SOURCES_C (the desktop Makefile isolates it the same way
# via the unity.o rule).
include $(CLEAR_VARS)
LOCAL_MODULE    := vjchdr
LOCAL_SRC_FILES := $(SOURCES_LIBCHDR)
LOCAL_CFLAGS    := $(COREFLAGS) $(LIBCHDR_CFLAGS) $(LIBCHDR_WARNFLAGS)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C)
LOCAL_CFLAGS    := $(COREFLAGS)
LOCAL_STATIC_LIBRARIES := vjchdr
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/link.T
include $(BUILD_SHARED_LIBRARY)
