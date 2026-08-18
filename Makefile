DEBUG = 0

SPACE :=
SPACE := $(SPACE) $(SPACE)
BACKSLASH :=
BACKSLASH := \$(BACKSLASH)
filter_out1 = $(filter-out $(firstword $1),$1)
filter_out2 = $(call filter_out1,$(call filter_out1,$1))
unixpath = $(subst \,/,$1)
unixcygpath = /$(subst :,,$(call unixpath,$1))

ifeq ($(platform),)
	platform = unix
	ifeq ($(shell uname -a),)
		platform = win
	else ifneq ($(findstring Darwin,$(shell uname -a)),)
		platform = osx
		arch = intel
		ifeq ($(shell uname -p),powerpc)
			arch = ppc
		endif
	        ifeq ($(shell uname -p),arm)
	        	arch = arm
	        endif
	else ifneq ($(findstring MINGW,$(shell uname -a)),)
		platform = win
	endif
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
	EXE_EXT = .exe
	system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	system_platform = osx
	arch = intel
	ifeq ($(shell uname -p),powerpc)
		arch = ppc
	endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
	system_platform = win
endif

TARGET_NAME := virtualjaguar

# Single source-of-truth for the human-readable version string.
# Bumped by .github/workflows/version-bump.yml (greps this line).
# Composed into CORE_VERSION in src/core/version.h, generated below.
CORE_BASE_VERSION := v3.3.0

ifeq ($(DEBUG),1)
   CFLAGS += -DBUILD_TIMESTAMP="\"debug $(shell date -u +%Y-%m-%dT%H:%M:%SZ)\""
endif

# Opt-in instrumentation counters (src/core/perf_counters.h).
# `make BENCH_PROFILE=1` defines the macro so PERF_COUNTER/PERF_INC
# emit real code; otherwise every counter macro is a no-op.
ifeq ($(BENCH_PROFILE),1)
   CFLAGS += -DBENCH_PROFILE
endif

# Per-blit slow-path tracing in BlitterMidsummer2.
# `make BLITTER_TRACE=1` enables an stderr dump of any single blit
# whose wall time exceeds ~1.5 ms (configurable via the threshold in
# src/tom/blitter.c).  Useful for finding pathological blit commands
# that dominate frame-time variance.  macOS-only (uses mach_*).
ifeq ($(BLITTER_TRACE),1)
   CFLAGS += -DBLITTER_TRACE
endif

# Symbol export gating.
#
#   GNU ld (Linux, Windows MSYS2, ARM, ...) honours --version-script:
#     link.T       : production ABI (retro_* only).
#     link-test.T  : wide symbol set used by white-box test harnesses.
#
#   Mach-O ld64 (macOS / iOS / tvOS) ignores --version-script; it uses
#   -exported_symbols_list instead:
#     exports.list       : production retro_* only.
#     exports-test.list  : wide test ABI (mirrors link-test.T).
#
# The `test` target re-invokes make with TEST_EXPORTS=1 so the shipped
# library on default `make` hides internals, while `make test` produces
# a library the test binaries can dlsym into.  Static archives ignore
# both mechanisms and still export everything with default visibility.
ifeq ($(TEST_EXPORTS),1)
LINK_SCRIPT := link-test.T
MACHO_EXPORTS := exports-test.list
CFLAGS += -DVJ_TRACE
else
LINK_SCRIPT := link.T
MACHO_EXPORTS := exports.list
endif
MACHO_EXPORTS_FLAGS := -Wl,-exported_symbols_list,$(MACHO_EXPORTS)

# Records the build configuration the objects in the tree were last
# compiled under; see the mode-switch hook next to the link rule below.
#
# Every variable listed here changes how objects are COMPILED (or which
# symbols survive the link), so flipping any one of them invalidates
# every existing .o.  Adding a new such switch means adding it here --
# that is the whole maintenance burden, and forgetting costs a silent
# chimera binary rather than a build error.
#
# It is deliberately a list of *variable names* and not $(CFLAGS)
# itself.  Stamping the flags would look more future-proof but breaks
# on DEBUG=1, whose CFLAGS carry a -DBUILD_TIMESTAMP that changes every
# second: the stamp would never match and every single build would
# flush the tree.  Values here are plain tokens (1, or a platform
# name), so the comparison also stays free of shell quoting hazards --
# CFLAGS contains -DINLINE="inline".
BUILD_AXES := TEST_EXPORTS BENCH_PROFILE DEBUG BLITTER_TRACE COVERAGE \
              RELEASE_DEBUG_INFO DEBUG_PRESENTATION STATIC_LINKING platform
BUILD_CONFIG := $(strip $(foreach v,$(BUILD_AXES),$(v)=$($(v))))
BUILD_CONFIG_STAMP := .build-config
# Superseded .link-mode, which tracked TEST_EXPORTS alone; removed by the
# hook below so a tree built before this change doesn't keep a dead file.
LEGACY_LINK_MODE_STAMP := .link-mode

# Unix
ifeq ($(platform), unix)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	ifneq ($(findstring SunOS,$(shell uname -a)),)
		# Solaris ld: no --gc-sections, so GC_STYLE stays unset.
		SHARED := -shared -z defs -z gnu-version-script-compat
	else
		SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
		GC_STYLE := gnu
	endif

# Classic Platforms ####################
# Platform affix = classic_<ISA>_<µARCH>
# Help at https://modmyclassic.com/comp

# (armv7 a7, hard point, neon based) ###
# NESC, SNESC, C64 mini
else ifeq ($(platform), classic_armv7_a7)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
	# --gc-sections belongs on the link line, not in CFLAGS; see the
	# GC_STYLE block below for why this target sets it by hand.
	LDFLAGS += -Wl,--gc-sections
	CFLAGS += -Ofast \
	-flto=4 -fwhole-program -fuse-linker-plugin \
	-fdata-sections -ffunction-sections \
	-fno-stack-protector -fno-ident -fomit-frame-pointer \
	-falign-functions=1 -falign-jumps=1 -falign-loops=1 \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-unroll-loops \
	-fmerge-all-constants -fno-math-errno \
	-marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
	CXXFLAGS += $(CFLAGS)
	HAVE_NEON = 1
	ARCH = arm
	ifeq ($(shell echo `$(CC) -dumpversion` "< 4.9" | bc -l), 1)
	  CFLAGS += -march=armv7-a
	else
	  CFLAGS += -march=armv7ve
	  # If gcc is 5.0 or later
	  ifeq ($(shell echo `$(CC) -dumpversion` ">= 5" | bc -l), 1)
	    LDFLAGS += -static-libgcc -static-libstdc++
	  endif
	endif
#######################################

# OSX
else ifeq ($(platform), osx)
	TARGET := $(TARGET_NAME)_libretro.dylib
	fpic := -fPIC
	SHARED := -dynamiclib $(MACHO_EXPORTS_FLAGS)
	GC_STYLE := macho
	ifeq ($(arch),ppc)
		FLAGS += -DMSB_FIRST
		OLD_GCC = 1
	endif
	OSXVER = `sw_vers -productVersion | cut -d. -f 2`
	OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
ifeq ($(OSX_LT_MAVERICKS),YES)
	fpic += -mmacosx-version-min=10.1
endif

   ifeq ($(CROSS_COMPILE),1)
		TARGET_RULE   = -target $(LIBRETRO_APPLE_PLATFORM) -isysroot $(LIBRETRO_APPLE_ISYSROOT)
		CFLAGS   += $(TARGET_RULE)
		CPPFLAGS += $(TARGET_RULE)
		CXXFLAGS += $(TARGET_RULE)
		LDFLAGS  += $(TARGET_RULE)
   endif

	CFLAGS  += $(ARCHFLAGS)
	CXXFLAGS  += $(ARCHFLAGS)
	LDFLAGS += $(ARCHFLAGS)

# iOS
else ifneq (,$(findstring ios,$(platform)))
	TARGET := $(TARGET_NAME)_libretro_ios.dylib
	fpic := -fPIC
	SHARED := -dynamiclib $(MACHO_EXPORTS_FLAGS)
	GC_STYLE := macho
	MINVERSION :=
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
	endif
ifeq ($(platform),ios-arm64)
   CC = cc -arch arm64 -isysroot $(IOSSDK)
   CXX = clang++ -arch arm64 -isysroot $(IOSSDK)
else
   CC = cc -arch armv7 -isysroot $(IOSSDK)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
endif
ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
	MINVERSION = -miphoneos-version-min=8.0
else
	MINVERSION = -miphoneos-version-min=5.0
endif
        SHARED += $(MINVERSION)
        CFLAGS += $(MINVERSION)

else ifeq ($(platform), tvos-arm64)
# tvOS
	TARGET := $(TARGET_NAME)_libretro_tvos.dylib
	fpic := -fPIC
	SHARED := -dynamiclib $(MACHO_EXPORTS_FLAGS)
	GC_STYLE := macho
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcodebuild -version -sdk appletvos Path)
	endif
        CC = cc -arch arm64 -isysroot $(IOSSDK)
        CXX = clang++ -arch arm64 -isysroot $(IOSSDK)
        MINVERSION = -mappletvos-version-min=11.0
        SHARED += $(MINVERSION)
        CFLAGS += $(MINVERSION)

# Theos
else ifeq ($(platform), theos_ios)
	DEPLOYMENT_IOSVERSION = 5.0
	TARGET = iphone:latest:$(DEPLOYMENT_IOSVERSION)
	ARCHS = armv7 armv7s
	TARGET_IPHONEOS_DEPLOYMENT_VERSION=$(DEPLOYMENT_IOSVERSION)
	THEOS_BUILD_DIR := objs
	include $(THEOS)/makefiles/common.mk
	LIBRARY_NAME = $(TARGET_NAME)_libretro_ios

# QNX
else ifeq ($(platform), qnx)
	TARGET := $(TARGET_NAME)_libretro_$(platform).so
	fpic := -fPIC
	SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
	GC_STYLE := gnu
	CC = qcc -Vgcc_ntoarmv7le
	CXX = QCC -Vgcc_ntoarmv7le_cpp

# ARM
else ifneq (,$(findstring armv,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
	GC_STYLE := gnu
	ARCH = arm

# Nintendo Switch (libnx)
else ifeq ($(platform), libnx)
	include $(DEVKITPRO)/libnx/switch_rules
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	DEFINES := -DSWITCH=1 -D__SWITCH__
	CFLAGS := $(DEFINES) -fPIE -I$(LIBNX)/include/ -ffunction-sections -fdata-sections -ftls-model=local-exec -specs=$(LIBNX)/switch.specs
	CFLAGS += -march=armv8-a -mtune=cortex-a57 -mtp=soft -mcpu=cortex-a57+crc+fp+simd -ffast-math
	CXXFLAGS := $(ASFLAGS) $(CFLAGS)
	STATIC_LINKING = 1

# Lightweight PS3 Homebrew SDK
else ifneq (,$(filter $(platform), ps3 psl1ght))
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)gcc$(EXE_EXT)
	CXX = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)g++$(EXE_EXT)
	AR = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)ar$(EXE_EXT)
	STATIC_LINKING = 1
	FLAGS += -DMSB_FIRST -D__PS3__
	OLD_GCC = 1
	ifeq ($(platform), psl1ght)
		FLAGS += -D__PSL1GHT__
	endif

# PSP
else ifeq ($(platform), psp1)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = psp-gcc$(EXE_EXT)
	CXX = psp-g++$(EXE_EXT)
	AR = psp-ar$(EXE_EXT)
	STATIC_LINKING = 1
	FLAGS += -G0

# Vita
else ifeq ($(platform), vita)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = arm-vita-eabi-gcc$(EXE_EXT)
	CXX = arm-vita-eabi-g++$(EXE_EXT)
	AR = arm-vita-eabi-ar$(EXE_EXT)
	STATIC_LINKING = 1
	FLAGS += -DVITA

# CTR (3DS)
else ifeq ($(platform), ctr)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITARM)/bin/arm-none-eabi-gcc$(EXE_EXT)
	CC = $(DEVKITARM)/bin/arm-none-eabi-g++$(EXE_EXT)
	AR = $(DEVKITARM)/bin/arm-none-eabi-ar$(EXE_EXT)
   STATIC_LINKING = 1
   FLAGS += -D_3DS

# Switch (libtransistor)
else ifeq ($(platform), switch)
	TARGET := $(TARGET_NAME)_libretro_switch.a
	include $(LIBTRANSISTOR_HOME)/libtransistor.mk
	STATIC_LINKING=1
	fpic := -nostdlib

# emscripten (WebAssembly / asm.js for RetroArch Web Player)
else ifeq ($(platform), emscripten)
	TARGET := $(TARGET_NAME)_libretro_$(platform).bc
	CC = emcc
	CXX = em++
	AR = emar
	STATIC_LINKING = 1
	FLAGS += -DHAVE_EMSCRIPTEN

# Windows MSVC 2017 all architectures
else ifneq (,$(findstring windows_msvc2017,$(platform)))

	PlatformSuffix = $(subst windows_msvc2017_,,$(platform))
	ifneq (,$(findstring desktop,$(PlatformSuffix)))
		WinPartition = desktop
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP -FS
		LDFLAGS += -MANIFEST -LTCG:incremental -NXCOMPAT -DYNAMICBASE -DEBUG -OPT:REF -INCREMENTAL:NO -SUBSYSTEM:WINDOWS -MANIFESTUAC:"level='asInvoker' uiAccess='false'" -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1
		#LIBS += kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
	else ifneq (,$(findstring uwp,$(PlatformSuffix)))
		WinPartition = uwp
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_APP -DWINDLL -D_UNICODE -DUNICODE -DWRL_NO_DEFAULT_LIB -FS
		LDFLAGS += -APPCONTAINER -NXCOMPAT -DYNAMICBASE -MANIFEST:NO -LTCG -OPT:REF -SUBSYSTEM:CONSOLE -MANIFESTUAC:NO -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1 -DEBUG:FULL -WINMD:NO
		LIBS += WindowsApp.lib
	endif

	CFLAGS += $(MSVC2017CompileFlags)
	CXXFLAGS += $(MSVC2017CompileFlags)

	TargetArchMoniker = $(subst $(WinPartition)_,,$(PlatformSuffix))

	CC  = cl.exe
	CXX = cl.exe

	reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
	fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))

	ProgramFiles86w := $(shell cmd //c "echo %PROGRAMFILES(x86)%")
	ProgramFiles86 := $(shell cygpath "$(ProgramFiles86w)")

	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir := $(WindowsSdkDir)

	WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
	WindowsSDKVersion := $(WindowsSDKVersion)

	VsInstallBuildTools = $(ProgramFiles86)/Microsoft Visual Studio/2017/BuildTools
	VsInstallEnterprise = $(ProgramFiles86)/Microsoft Visual Studio/2017/Enterprise
	VsInstallProfessional = $(ProgramFiles86)/Microsoft Visual Studio/2017/Professional
	VsInstallCommunity = $(ProgramFiles86)/Microsoft Visual Studio/2017/Community

	VsInstallRoot ?= $(shell if [ -d "$(VsInstallBuildTools)" ]; then echo "$(VsInstallBuildTools)"; fi)
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallEnterprise)" ]; then echo "$(VsInstallEnterprise)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallProfessional)" ]; then echo "$(VsInstallProfessional)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallCommunity)" ]; then echo "$(VsInstallCommunity)"; fi)
	endif
	VsInstallRoot := $(VsInstallRoot)

	VcCompilerToolsVer := $(shell cat "$(VsInstallRoot)/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" | grep -o '[0-9\.]*')
	VcCompilerToolsDir := $(VsInstallRoot)/VC/Tools/MSVC/$(VcCompilerToolsVer)

	WindowsSDKSharedIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\shared")
	WindowsSDKUCRTIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\ucrt")
	WindowsSDKUMIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\um")
	WindowsSDKUCRTLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\ucrt\$(TargetArchMoniker)")
	WindowsSDKUMLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\um\$(TargetArchMoniker)")

	# For some reason the HostX86 compiler doesn't like compiling for x64
	# ("no such file" opening a shared library), and vice-versa.
	# Work around it for now by using the strictly x86 compiler for x86, and x64 for x64.
	# NOTE: What about ARM?
	ifneq (,$(findstring x64,$(TargetArchMoniker)))
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX64
	else
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX86
	endif

	PATH := $(shell IFS=$$'\n'; cygpath "$(VCCompilerToolsBinDir)/$(TargetArchMoniker)"):$(PATH)
	PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VsInstallRoot)/Common7/IDE")
	INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/include")
	LIB := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/lib/$(TargetArchMoniker)")

	export INCLUDE := $(INCLUDE);$(WindowsSDKSharedIncludeDir);$(WindowsSDKUCRTIncludeDir);$(WindowsSDKUMIncludeDir)
	export LIB := $(LIB);$(WindowsSDKUCRTLibDir);$(WindowsSDKUMLibDir)
	TARGET := $(TARGET_NAME)_libretro.dll
	PSS_STYLE :=2
	LDFLAGS += -DLL

# Windows MSVC 2015 x64
else ifeq ($(platform), windows_msvc2015_x64)
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../../VC/bin/amd64"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/lib/amd64")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/include")

reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir := $(WindowsSdkDir)

WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
WindowsSDKVersion := $(WindowsSDKVersion)

export INCLUDE := $(INCLUDE);$(VCINSTALLDIR)INCLUDE;$(VCINSTALLDIR)ATLMFC\INCLUDE;$(WindowsSdkDir)include\$(WindowsSDKVersion)ucrt;$(WindowsSdkDir)include\$(WindowsSDKVersion)shared;$(WindowsSdkDir)include\$(WindowsSDKVersion)um
export LIB := $(LIB);$(VCINSTALLDIR)LIB\amd64;$(VCINSTALLDIR)ATLMFC\LIB\amd64;$(WindowsSdkDir)lib\$(WindowsSDKVersion)ucrt\x64;$(WindowsSdkDir)lib\$(WindowsSDKVersion)um\x64

INCFLAGS_PLATFORM = -I"$(WindowsSDKVersion)um" -I"$(WindowsSDKVersion)shared"
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL -MACHINE:X64
CFLAGS += -FS
CXXFLAGS += -FS

# Windows MSVC 2015 x86
else ifeq ($(platform), windows_msvc2015_x86)
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS140COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/lib")
INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VS140COMNTOOLS)../../VC/include")

reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
WindowsSdkDir := $(WindowsSdkDir)

WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
WindowsSDKVersion := $(WindowsSDKVersion)

export INCLUDE := $(INCLUDE);$(VCINSTALLDIR)INCLUDE;$(VCINSTALLDIR)ATLMFC\INCLUDE;$(WindowsSdkDir)include\$(WindowsSDKVersion)ucrt;$(WindowsSdkDir)include\$(WindowsSDKVersion)shared;$(WindowsSdkDir)include\$(WindowsSDKVersion)um
export LIB := $(LIB);$(VCINSTALLDIR)LIB;$(VCINSTALLDIR)ATLMFC\LIB;$(WindowsSdkDir)lib\$(WindowsSDKVersion)ucrt\x86;$(WindowsSdkDir)lib\$(WindowsSDKVersion)um\x86

INCFLAGS_PLATFORM = -I"$(WindowsSDKVersion)um" -I"$(WindowsSDKVersion)shared"
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL -MACHINE:X86
CFLAGS += -FS
CXXFLAGS += -FS

# Windows MSVC 2010 x64
else ifeq ($(platform), windows_msvc2010_x64)
	LIBS=
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/bin/amd64"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/lib/amd64")
INCLUDE := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')
WindowsSdkDir ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')

WindowsSDKIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include")
WindowsSDKGlIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\gl")
WindowsSDKLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\x64")

INCFLAGS_PLATFORM = -I"$(WindowsSDKIncludeDir)"
export INCLUDE := $(INCLUDE);$(WindowsSDKIncludeDir);$(WindowsSDKGlIncludeDir)
export LIB := $(LIB);$(WindowsSDKLibDir)
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL
LIBS =
# Windows MSVC 2010 x86
else ifeq ($(platform), windows_msvc2010_x86)
	LIBS=
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../IDE")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS100COMNTOOLS)../../VC/lib")
INCLUDE := $(shell IFS=$$'\n'; cygpath "$(VS100COMNTOOLS)../../VC/include")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.1A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')
WindowsSdkDir ?= $(shell reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v7.0A" -v "InstallationFolder" | grep -o '[A-Z]:\\.*')

WindowsSDKIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include")
WindowsSDKGlIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\gl")
WindowsSDKLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib")

INCFLAGS_PLATFORM = -I"$(WindowsSDKIncludeDir)"
export INCLUDE := $(INCLUDE);$(WindowsSDKIncludeDir);$(WindowsSDKGlIncludeDir)
export LIB := $(LIB);$(WindowsSDKLibDir)
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL
LIBS =

# Windows MSVC 2005 x86
else ifeq ($(platform), windows_msvc2005_x86)
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../../VC/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../IDE")
INCLUDE := $(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../../VC/include")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS80COMNTOOLS)../../VC/lib")
BIN := $(shell IFS=$$'\n'; cygpath "$(VS80COMNTOOLS)../../VC/bin")

WindowsSdkDir := $(shell reg query "HKLM\SOFTWARE\Microsoft\MicrosoftSDK\InstalledSDKs\8F9E5EF3-A9A5-491B-A889-C58EFFECE8B3" -v "Install Dir" | grep -o '[A-Z]:\\.*')

WindowsSDKIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include")
WindowsSDKAtlIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\atl")
WindowsSDKCrtIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\crt")
WindowsSDKGlIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\gl")
WindowsSDKMfcIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\mfc")
WindowsSDKLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib")

export INCLUDE := $(INCLUDE);$(WindowsSDKIncludeDir);$(WindowsSDKAtlIncludeDir);$(WindowsSDKCrtIncludeDir);$(WindowsSDKGlIncludeDir);$(WindowsSDKMfcIncludeDir);libretro-common/include/compat/msvc
export LIB := $(LIB);$(WindowsSDKLibDir)
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL
CFLAGS += -D_CRT_SECURE_NO_DEPRECATE

# Windows MSVC 2003 Xbox 1
else ifeq ($(platform), xbox1_msvc2003)
TARGET := $(TARGET_NAME)_libretro_xdk1.lib
CC  = CL.exe
CXX  = CL.exe
LD   = lib.exe

export INCLUDE := $(XDK)/xbox/include
export LIB := $(XDK)/xbox/lib
PATH := $(call unixcygpath,$(XDK)/xbox/bin/vc71):$(PATH)
PSS_STYLE :=2
CFLAGS   += -D_XBOX -D_XBOX1
CXXFLAGS += -D_XBOX -D_XBOX1
STATIC_LINKING=1
HAS_GCC := 0

# Windows MSVC 2003 x86
else ifeq ($(platform), windows_msvc2003_x86)
	CC  = cl.exe
	CXX = cl.exe

PATH := $(shell IFS=$$'\n'; cygpath "$(VS71COMNTOOLS)../../Vc7/bin"):$(PATH)
PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VS71COMNTOOLS)../IDE")
INCLUDE := $(shell IFS=$$'\n'; cygpath "$(VS71COMNTOOLS)../../Vc7/include")
LIB := $(shell IFS=$$'\n'; cygpath -w "$(VS71COMNTOOLS)../../Vc7/lib")
BIN := $(shell IFS=$$'\n'; cygpath "$(VS71COMNTOOLS)../../Vc7/bin")

WindowsSdkDir := $(INETSDK)

export INCLUDE := $(INCLUDE);$(INETSDK)/Include;libretro-common/include/compat/msvc
export LIB := $(LIB);$(WindowsSdkDir);$(INETSDK)/Lib
TARGET := $(TARGET_NAME)_libretro.dll
PSS_STYLE :=2
LDFLAGS += -DLL
CFLAGS += -D_CRT_SECURE_NO_DEPRECATE

# Windows
else
	TARGET := $(TARGET_NAME)_libretro.dll
	CC ?= gcc
	CXX ?= g++
	SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
	GC_STYLE := gnu
	LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm -lws2_32

endif

CORE_DIR     := .

include Makefile.common

OBJECTS := $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o) $(SOURCES_LIBCHDR:.c=.o)

# ----------------------------------------------------------------
# version.h: generated header read by libretro.c.  Single source of
# truth is CORE_BASE_VERSION above; the script also stamps in the
# short git rev.
#
# Regeneration runs at Makefile parse time via $(shell ...) so the
# dependency graph sees a stable file with a stable mtime.  The
# alternative (a `: FORCE` rule) was racy under `make -j4` on the
# stock /usr/bin/make 3.81 still shipped on macOS, which silently
# stopped mid-build.  The script does an in-place cmp + mv so
# unchanged content leaves mtime untouched and incremental builds
# stay incremental.
# ----------------------------------------------------------------
VERSION_H := $(CORE_DIR)/src/core/version.h

# Skip the generator for read-only / metadata-only goals -- no point
# spawning bash for `make clean`, `make print-FOO`, `make lint`, or
# `make help`.  Builds (the empty MAKECMDGOALS case, default target)
# always run it.
NO_GEN_GOALS := clean lint help print-%
ifeq ($(filter-out $(NO_GEN_GOALS),$(or $(MAKECMDGOALS),all)),)
# All requested goals are read-only -- skip generator.
else
_VERSION_GEN := $(shell bash scripts/gen-version-h.sh && echo ok)
endif

# Note: $(CORE_DIR)/libretro.o: $(VERSION_H) dependency is wired up
# AFTER the `all:` rule below, so Make 3.81 doesn't latch onto
# libretro.o as the default goal.

ifeq ($(DEBUG),1)
   ifneq (,$(findstring msvc,$(platform)))
      CFLAGS += -MTd
      CXXFLAGS += -MTd
      CFLAGS += -Od -Zi -D_DEBUG
      CXXFLAGS += -Od -Zi -D_DEBUG
      LDFLAGS += -DEBUG
   else
      FLAGS += -O0 -g
   endif
else
   ifneq (,$(findstring msvc,$(platform)))
      CFLAGS   += -MT
      CXXFLAGS += -MT
      CFLAGS   += -O2 -DNDEBUG
      CXXFLAGS += -O2 -DNDEBUG
   else
      FLAGS    += -O2 -DNDEBUG
   endif
endif

# Release builds with split debug symbols.
# Set RELEASE_DEBUG_INFO=1 (release.yml does this) to keep -g in the
# optimized build so we can later run objcopy --only-keep-debug /
# dsymutil to ship a separate debug-info archive next to the stripped
# binary in the GitHub release.  Has no effect when DEBUG=1 (-g is
# already on) or under MSVC (which uses /Zi and a .pdb instead).
ifeq ($(RELEASE_DEBUG_INFO),1)
   ifeq (,$(findstring msvc,$(platform)))
      FLAGS += -g
   endif
endif

# COVERAGE=1 instruments the build with gcov.  Used by `make coverage`
# below; don't combine with optimized builds.  Compiler emits .gcno
# files at build time, .gcda files at run time.
ifeq ($(COVERAGE),1)
   ifeq (,$(findstring msvc,$(platform)))
      FLAGS   += --coverage -O0 -g
      LDFLAGS += --coverage
   endif
endif

ifeq (,$(findstring msvc,$(platform)))
FLAGS += -ffast-math -fomit-frame-pointer -fno-common
endif

# ----------------------------------------------------------------
# Linker dead-code elimination (issue #321).
#
# src/m68000/cpustbl.c ships TWO complete 68000 handler tables:
# op_smalltbl_4_ff (3162 entries, "fast") and op_smalltbl_5_ff (1581,
# "slow but compatible").  m68kinterface.c binds op_smalltbl_5_ff
# unconditionally, so table 4 -- and every machine-generated cpuemu.c
# handler body reachable only through it -- is linked in and never
# called.  Letting the linker garbage-collect it needs no source
# change at all; this block is the whole fix.
#
# GC_STYLE is opt-in PER PLATFORM, never global.  The release matrix
# spans 16 toolchains while PR CI covers four, so a globally-applied
# flag that one exotic linker rejects fails at tag time, not in review.
# The rule used to set it in the platform blocks above:
#
#   gnu   -- the platform's SHARED line already passes GNU-ld-only
#            options (--version-script / --no-undefined), which proves
#            the link runs through GNU ld or lld, so --gc-sections is
#            available too.  Section-per-symbol flags are required
#            there: without them ELF/PE GC works at section
#            granularity and one live handler pins the whole object.
#   macho -- Apple ld64.  It is atom-based, so -dead_strip alone is
#            sufficient and -ffunction-sections/-fdata-sections are
#            no-ops; they are deliberately NOT added.
#   unset -- byte-for-byte unchanged from before.  Covers every
#            STATIC_LINKING=1 target (vita, switch/libnx, ctr, ps3,
#            psl1ght, psp1, emscripten -- those emit a .a/.bc and
#            never run a link, so the flags could not take effect
#            anyway), every MSVC/Xbox target, Solaris ld, and theos_ios
#            (Theos drives its own link).
#
# classic_armv7_a7 is the one target that opts in by hand rather than
# via GC_STYLE, because it does NOT want the compile half: its CFLAGS
# already carry -ffunction-sections -fdata-sections, and GC_STYLE:=gnu
# would duplicate them.  It used to pass -Wl,--gc-sections in CFLAGS,
# where it was provably inert -- the link line is $(OBJECTS) $(LDFLAGS)
# only.  Measured with a Debian bookworm arm-linux-gnueabihf gcc 12
# cross-link (a proxy for the modmyclassic toolchain, not that
# toolchain itself), deleting that CFLAGS entry outright produced a
# byte-identical 1,512,628-byte .so.
#
# Do not expect the macOS win here.  This block compiles with
# -flto=4 -fwhole-program, so cpustbl.o is a slim LTO object and GCC's
# IPA -- not the linker -- decides what dies: op_smalltbl_4_ff is
# already absent from the baseline .so.  Moving the flag to LDFLAGS
# links clean with a byte-identical dynsym list and saves 80 bytes
# (1,512,628 -> 1,512,548, 0.005%), versus 22.7% on non-LTO ld64.
# It is kept because a flag that reads as enabled should be enabled,
# not because the size matters.
#
# Android is unaffected either way: ndk-build uses jni/Android.mk,
# not this Makefile.
# ----------------------------------------------------------------
ifneq ($(STATIC_LINKING),1)
ifneq ($(platform),theos_ios)
   ifeq ($(GC_STYLE),gnu)
      FLAGS   += -ffunction-sections -fdata-sections
      LDFLAGS += -Wl,--gc-sections
   else ifeq ($(GC_STYLE),macho)
      LDFLAGS += -Wl,-dead_strip
   endif
endif
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS += $(fpic)
FLAGS += $(INCFLAGS) $(INCFLAGS_PLATFORM)

ifeq ($(OLD_GCC), 1)
WARNINGS := -Wall
else ifeq ($(NO_GCC), 1)
WARNINGS :=
else ifneq (,$(findstring msvc,$(platform)))
WARNINGS :=
else
WARNINGS := -Wall \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-but-set-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	-Wno-strict-aliasing \
	-Wno-overflow \
	-fno-strict-overflow
endif

FLAGS += -D__LIBRETRO__ $(WARNINGS)

ifneq (,$(findstring msvc,$(platform)))
FLAGS += -DINLINE="_inline"
else
FLAGS += -DINLINE="inline"
endif

CXXFLAGS += $(FLAGS)
CFLAGS   += $(FLAGS)

# Optional: build with framebuffer/audio presentation diagnostics.
# Enables periodic LOG_INF dumps from libretro.c retro_run() showing
# tomWidth/tomHeight, screenPitch, sample pixels, ltxd/rtxd, DSPIsRunning.
# Use: make DEBUG_PRESENTATION=1
ifeq ($(DEBUG_PRESENTATION), 1)
CXXFLAGS += -DDEBUG_PRESENTATION
CFLAGS   += -DDEBUG_PRESENTATION
endif

OBJOUT   = -o
LINKOUT  = -o

ifneq (,$(findstring msvc,$(platform)))
	OBJOUT = -Fo
	LINKOUT = -out:
	LD = link.exe
else
	LD = $(CC)
endif

%.o: %.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c $(OBJOUT)$@ $< $(CFLAGS)

ifeq ($(platform), theos_ios)
COMMON_FLAGS := -DIOS $(COMMON_DEFINES) $(INCFLAGS) -I$(THEOS_INCLUDE_PATH) -Wno-error
$(LIBRARY_NAME)_CFLAGS += $(CFLAGS) $(COMMON_FLAGS) $(LIBCHDR_CFLAGS) $(LIBCHDR_WARNFLAGS)
$(LIBRARY_NAME)_CXXFLAGS += $(CXXFLAGS) $(COMMON_FLAGS)
${LIBRARY_NAME}_FILES = $(SOURCES_CXX) $(SOURCES_C) $(SOURCES_LIBCHDR)
include $(THEOS_MAKE_PATH)/library.mk
else
# Force a rebuild when the build configuration changes ($(BUILD_AXES)).
# Almost every object is identical across an ABI flip, so a plain `make`
# followed by `make TEST_EXPORTS=1 test` would otherwise reuse the
# production-slim library -- it is newer than every object, so nothing
# relinks -- and the white-box tests fail with "Missing: m68k_execute".
# Delete the library outright rather than relying on a stamp file's mtime:
# the stamp and the library can land in the same second, which is exactly
# the timestamp-granularity trap this is meant to close.  Runs at parse
# time, once TARGET is known.
#
# The stamp covers every compile-affecting switch, not just TEST_EXPORTS
# (issue #457).  It originally tracked TEST_EXPORTS alone, which left the
# same hazard open one level up:
#   BENCH_PROFILE -- `make TEST_EXPORTS=1` then
#     `make BENCH_PROFILE=1 TEST_EXPORTS=1` recompiled nothing, so no
#     object got -DBENCH_PROFILE, no PERF_COUNTER registered, and every
#     timing_probe tool died with "timing_halfline_callbacks counter not
#     found" -- which reads as a broken tool, not an ignored flag.
#   DEBUG -- `make` then `make DEBUG=1` recompiled *zero* objects: a
#     "debug build" that is entirely -O2 with no debug info.  The reverse
#     leaves -O0 objects in a release binary and silently invalidates any
#     performance measurement taken from it.
# VJ_EXPECT_BUILD cannot catch either: the git rev is identical across the
# flip, so the guard passes while the binary is wrong.
#
# TEST_EXPORTS also changes object *content*, not just the export list:
# it adds -DVJ_TRACE to CFLAGS (see the line above), so vjtrace.o defines
# the vjtrace_* functions only in that branch, and every object carrying a
# VJT_EMIT/VJT_WATCH_*/vjtrace_pchist_*/vjtrace_init call site compiles to
# real calls in one mode and to nothing in the other.  A stale object from
# the other mode that survives the relink breaks the build in one direction
# and lies in the other: TEST_EXPORTS=1 -> plain fails with undefined
# vjtrace_* symbols, while plain -> TEST_EXPORTS=1 links fine but keeps the
# no-op macro expansion, so the ring silently records nothing despite
# vjtrace_* being exported.
#
# So a mode change flushes *every* object, not a curated list of the ones
# believed to be trace-hooked.  That list is exactly the stale-.o hazard it
# was meant to prevent: it shipped without src/tom/blit_memo.o, whose
# BlitMemoLaunch VJT_EMIT call site broke `make TEST_EXPORTS=1 && make`.
# Any list -- hand-written, grepped, or derived from -MD -- can also miss a
# file that picks up the macros through an indirect include, so there is no
# list.  Cost is one full rebuild per flip (~21s at -j8 without ccache);
# the curated list already recompiled most of the heavy objects anyway.
#
# The $(shell) runs at parse time, so it would also fire under `make -n`.
# That was tolerable when the flush was nine objects; with the whole object
# list in scope a dry run would silently cost a full rebuild, so skip it
# when -n is in effect.
#
# GNU make encodes -n two different ways, so both are matched.  Normally the
# short options collect into MAKEFLAGS' first word with no leading dash
# ("n", "ns", "nk") -- but under make 3.81 (stock macOS) a long option
# empties that group and -n reappears as its own word, e.g.
# `make --no-print-directory -n` gives MAKEFLAGS=" --no-print-directory -n".
# Only the letter group is scanned for 'n' (no short option other than -n
# uses that letter); long options and the `--`/NAME=value words that carry
# command-line variables start with '-' or contain '=', and are dropped, so
# `make platform=android` cannot false-match.  A false positive here would
# silently skip the flush and bring the stale-object bug straight back, so
# the detection is deliberately conservative in that direction.
MAKEFLAGS_LETTERS := $(filter-out -%,$(firstword $(MAKEFLAGS)))
DRY_RUN := $(strip $(findstring n,$(MAKEFLAGS_LETTERS)) \
                   $(filter -n --dry-run --just-print --recon,$(MAKEFLAGS)))
$(if $(DRY_RUN),,\
$(shell [ "$$(cat $(BUILD_CONFIG_STAMP) 2>/dev/null)" = "$(BUILD_CONFIG)" ] \
        || { printf '%s' "$(BUILD_CONFIG)" > $(BUILD_CONFIG_STAMP); \
             rm -f $(TARGET) $(OBJECTS) $(LEGACY_LINK_MODE_STAMP); }))

all: $(TARGET)
$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(LD) $(LINKOUT)$@ $(OBJECTS) $(LDFLAGS)
endif

# libchdr is C99; the rest of the core is gnu89. A dedicated rule so
# the generic %.o: %.c line cannot compile unity.c as C89.  Must sit
# AFTER `all:` so it is not the default goal.
$(LIBCHDR_DIR)/unity.o: $(LIBCHDR_DIR)/unity.c
	$(CC) -c $(OBJOUT)$@ $< $(CFLAGS) $(LIBCHDR_CFLAGS) $(LIBCHDR_WARNFLAGS)

# version.h dependency hook (must come after `all:` so Make 3.81 on
# stock macOS doesn't latch onto libretro.o as the default goal).
$(CORE_DIR)/libretro.o: $(VERSION_H)

clean:
	rm -f $(TARGET) $(OBJECTS) $(BUILD_CONFIG_STAMP) $(LEGACY_LINK_MODE_STAMP) \
		test/test_cheat test/test_event_queue test/test_blitter_simd \
		test/test_dsp_mac40 test/test_m68k_ops test/test_m68k_irq_ssp test/test_gpu_ops \
		test/test_dsp_ops test/test_dsp_unit test/test_hle_bios \
		test/test_subsystem_init test/test_subsystem_timeline \
		test/test_irq_cascade test/test_boot_patterns test/test_fountain_crash test/test_audio_pipeline \
		test/test_audio_clipping test/test_audio_presence test/test_audio_boundary test/test_audio_rate test/test_pit_clock_rate \
		test/test_blitter_mmio test/test_blitter_cmd test/test_eeprom_lifecycle \
		test/test_eeprom_read_race \
		test/test_tom_visible_window test/test_framebuffer_integrity \
		test/test_butch_cd test/test_bios_config test/test_boot_config \
		test/test_cart_format test/test_cart_needs_bios \
		test/test_cd_boot test/test_cd_hle_boot test/test_cd_bios_boot test/test_cd_toc_contract test/test_cd_fifo_stream test/test_cd_ssi_stream test/test_cd_second_transfer test/test_cd_hle_idempotent test/test_cd_lost_wakeup test/test_cd_pregap test/test_cd_chd test/test_chd_unit test/test_cd_synth_read test/test_cd_synth_butch test/test_cd_synth_cdda test/test_cd_synth_subq \
		test/test_audio_dac test/test_blitter \
		test/test_state_compat test/test_frontend_pacing test/test_jgd \
		test/dump_pc test/heap_search \
		tools/jagcd/jagcd-chd-check \
		test/tools/test_memory_map test/tools/test_option_visibility test/test_memtrack test/test_nvmbios test/tools/test_dsp_audio_diag \
		test/tools/test_frame_timing test/tools/test_runahead_determinism test/tools/test_pertitle_db \
		test/test_titledb test/test_titlehook test/tools/test_hook_gate test/test_biosdb \
		test/test_cart_bios_loader \
		test/tools/test_wedge_spin test/tools/i2s_lag_probe \
		test/tools/joymatrix_identity test/tools/mouse_decode_test \
		test/tools/rotary_decode_test \
		test/test_quadrature \
		test/.skipped-checks

# Self-contained unit tests (parser + list management + simulated
# memory application). Does not require a ROM or a working build of
# the full core.
ifneq (,$(findstring msvc,$(platform)))
test:
	@echo "make test requires GCC/Clang flags; use MSYS2/Unix or compile test/test_cheat.c manually."
	@false
else ifneq ($(TEST_EXPORTS),1)
# When `make test` is invoked without TEST_EXPORTS=1, the shipped .so
# was linked with link.T (production-slim, retro_* only) and the
# white-box test binaries can't dlsym into JaguarReset / DSPGetRAM /
# etc.  Force a re-link with link-test.T by removing the .so and
# re-invoking make with TEST_EXPORTS=1 so the wider symbol set is
# exported just for this build.  After `make test` finishes, the .so
# in the working tree has the wider exports — re-run `make` (no flag)
# to restore the production-slim ABI.
test:
	@rm -f $(TARGET)
	@$(MAKE) TEST_EXPORTS=1 test

# The per-binary rules below only exist in the TEST_EXPORTS=1 branch.
# Without this guard, a bare `make test/<binary>` silently falls through
# to GNU make's built-in %:%.c rule, which links with the core's
# -dynamiclib LDFLAGS and produces an UNEXECUTABLE shared library
# ("cannot execute binary file" at run time -- this broke
# cd_boot_matrix.sh's harness build).  Fail loudly instead.
test/test_% test/tools/test_%:
	@echo "ERROR: test binaries need the TEST_EXPORTS=1 rule set:" >&2
	@echo "  make TEST_EXPORTS=1 $@" >&2
	@false
else
# Every harness that dlopens the core verifies the binary's embedded git
# rev (+ -dirty) against this before running -- a stale dylib fails loudly
# instead of silently testing the wrong code (see scripts/build-id.sh).
test: export VJ_EXPECT_BUILD := $(shell ./scripts/build-id.sh)
test: test/test_dram_timing test/test_cheat test/test_event_queue test/test_jlink test/test_jlink_tcp test/test_jlink_netpacket test/test_uart_loopback test/test_blitter_simd test/test_dsp_mac40 test/test_titledb test/test_titlehook test/test_biosdb \
		$(TARGET) test/test_m68k_ops test/test_m68k_irq_ssp test/test_gpu_ops test/test_dsp_ops \
		test/test_dsp_unit test/test_hle_bios test/test_subsystem_init \
		test/test_subsystem_timeline test/test_irq_cascade test/test_boot_patterns \
		test/test_fountain_crash \
		test/test_audio_pipeline test/test_audio_clipping test/test_audio_presence test/test_audio_boundary test/test_audio_rate test/test_pit_clock_rate \
		test/test_blitter_mmio test/test_blitter_cmd test/test_eeprom_lifecycle test/test_eeprom_read_race test/test_tom_visible_window \
		test/test_framebuffer_integrity test/test_state_compat \
		test/test_frontend_pacing test/test_jgd \
		test/tools/test_runahead_determinism test/tools/test_wedge_spin \
		test/test_butch_cd test/test_bios_config test/test_boot_config \
		test/test_cart_format test/test_cart_needs_bios test/test_cart_bios_loader \
		test/test_cd_boot test/test_cd_hle_boot test/test_cd_bios_boot test/test_cd_toc_contract test/test_cd_fifo_stream test/test_cd_ssi_stream test/test_cd_second_transfer test/test_cd_hle_idempotent test/test_cd_lost_wakeup test/test_cd_pregap test/test_cd_chd test/test_chd_unit test/test_cd_synth_read test/test_cd_synth_butch test/test_cd_synth_cdda test/test_cd_synth_subq \
		test/test_audio_dac test/test_blitter \
		test/tools/test_memory_map test/tools/test_op_gpu_object test/tools/test_option_visibility test/test_memtrack test/test_nvmbios test/test_uart_core test/test_netlink_host \
		test/tools/netlink_pair test/tools/netlink_latency test/tools/netlink_delay_proxy test/tools/test_pertitle_db \
		test/tools/test_hook_gate \
		test/tools/i2s_lag_probe test/tools/joymatrix_identity \
		test/test_quadrature test/tools/mouse_decode_test \
		test/tools/rotary_decode_test \
		tools/jagcd/jagcd-chd-check
	@# Skip ledger: truncate FIRST so a previous run's rows cannot resurface
	@# as fresh skips (the stale-row failure mode documented for
	@# cd_boot_matrix.sh).  Every optional check below records into it, and
	@# the summary at the end of this recipe prints the roll-up.
	@bash scripts/test-skip.sh reset
	@# The wide test ABI is spelled twice -- link-test.T for GNU ld, and
	@# exports-test.list for Mach-O -- and a symbol added to only one is
	@# hidden on the other platform, where harness_dlsym silently returns
	@# NULL.  Cheap, so it runs before anything links against that ABI.
	@python3 scripts/check-export-lists.py
	./test/test_dram_timing
	./test/test_cheat
	./test/test_event_queue
	./test/test_jlink
	./test/test_jlink_tcp
	./test/test_jlink_netpacket ./$(TARGET)
	./test/test_uart_loopback
	./test/test_uart_core ./$(TARGET)
	./test/test_netlink_host ./$(TARGET)
	bash test/tools/netlink_pair_test.sh ./$(TARGET)
	bash test/tools/netlink_latency_test.sh ./$(TARGET)
	@# vjtrace flight-recorder selftest: determinism (field_diff +
	@# trace_memdiff on two identical runs), watch attribution, and
	@# VJ_TRACE_RING wrap correctness. Builds its own analyzer/smoke tools
	@# into test/tools/ on first run (see the script header); needs only
	@# the in-tree test/roms/yarc.j64, never test/roms/private.
	bash test/tools/vjtrace_selftest.sh ./$(TARGET)
	@# Boot-matrix shared logic (matrix_common.sh): core-fault verdicts must
	@# never be blamed on a title, and the row cache must ignore docs-only
	@# changes. Synthetic logs only -- no ROMs, no core, runs in <1s.
	bash test/tools/matrix_common_test.sh
	./test/test_blitter_mmio
	./test/test_blitter_cmd ./$(TARGET)
	./test/test_pit_clock_rate
	./test/test_tom_visible_window
	./test/test_blitter_simd
	./test/test_dsp_mac40
	./test/test_titledb
	./test/test_biosdb
	./test/test_cart_bios_loader
	./test/test_titlehook
	./test/test_m68k_ops
	./test/test_m68k_irq_ssp
	./test/test_gpu_ops
	./test/test_dsp_ops
	./test/test_dsp_unit
	./test/test_hle_bios
	./test/test_subsystem_init ./$(TARGET)
	./test/test_subsystem_timeline ./$(TARGET)
	./test/test_irq_cascade ./$(TARGET)
	./test/test_boot_patterns
	@# Fountain (#469): dummy-cart vector park always (CI).  Live jagcrypt
	@# ROM is public but not vendored -- ledger a skip for that arm only.
	./test/test_fountain_crash ./$(TARGET) --bios --quiet
	@if [ -f /tmp/fountain_vj.j64 ]; then \
		./test/test_fountain_crash ./$(TARGET) /tmp/fountain_vj.j64 --bios --frames 180 --option virtualjaguar_pal=enabled --quiet; \
	else \
		bash scripts/test-skip.sh record "Fountain live abort (#469)" "no /tmp/fountain_vj.j64"; \
	fi
	@# test_audio_pipeline takes an OPTIONAL positional ROM; without it, its
	@# onset check and its BIOS-vs-HLE comparison skip unconditionally --
	@# with ROMs, without ROMs, always.  It was invoked bare, so those two
	@# checks were permanently vacuous.  Feed it the same Iron Soldier 1
	@# dump the presence check uses (boots straight to a music-on title).
	@rom=$$(bash scripts/find-rom.sh 'Iron Soldier (1994).jag' 'Iron Soldier (World)*.j64' 'Iron Soldier.jag'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_pipeline ./$(TARGET) "$$rom"; \
	else \
		bash scripts/test-skip.sh record "Audio pipeline (onset + BIOS/HLE cmp)" "no ROM matching 'Iron Soldier*' in the private corpus"; \
		./test/test_audio_pipeline ./$(TARGET); \
	fi
	./test/test_audio_clipping ./$(TARGET) --self-test
	@# Wedge-detector spin-aliasing regression (#378 pilot finding): a
	@# healthy GPU wait/spin loop must not log gpu_wedge.  Super Burnout
	@# spins ~446k GPU opcodes/frame at one sampled PC during attract.
	@rom=$$(bash scripts/find-rom.sh 'Super Burnout (1995).jag' 'Super Burnout*.jag' 'Super Burnout*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/tools/test_wedge_spin ./$(TARGET) "$$rom"; \
	else \
		bash scripts/test-skip.sh record "Wedge spin-aliasing (Super Burnout)" "no ROM matching 'Super Burnout*' in the private corpus"; \
	fi
	@# ROM lookup goes through scripts/find-rom.sh, which searches the whole
	@# private corpus case-insensitively and prefers the canonical top-level
	@# copy over duplicates buried in sub-collections.  It replaced a pair of
	@# hardcoded literal paths per title: the corpus names titles
	@# inconsistently ("Iron Soldier 2 (World).j64" vs "Iron Soldier (World)
	@# (v1.04).j64"), so a literal path that does not match turns the check
	@# into a SKIP that still exits 0 -- the suite stays green while the
	@# check is not running at all.  That is exactly how the Skyhammer
	@# sentinel below went inert.  Every miss is now recorded in the skip
	@# ledger and reported in the summary at the end of this recipe.
	@# Negative control: healthy boot should not trip the clipping detector.
	@rom=$$(bash scripts/find-rom.sh 'Atari Karts (1995).jag' 'Atari Karts*.jag' 'Atari Karts*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_clipping ./$(TARGET) "$$rom" --label "Atari Karts (negative control)" --quiet; \
	else \
		bash scripts/test-skip.sh record "Atari Karts (clipping neg. control)" "no ROM matching 'Atari Karts*' in the private corpus"; \
	fi
	@# Frame-boundary discontinuity (60 Hz crackle class).  The clipping
	@# and presence tests are both blind to this: it is neither
	@# saturation nor silence.  Atari Karts' attract mode measured 10x
	@# on the broken core vs 1.0x fixed; see test_audio_boundary.c.
	@rom=$$(bash scripts/find-rom.sh 'Atari Karts (1995).jag' 'Atari Karts*.jag' 'Atari Karts*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_boundary ./$(TARGET) "$$rom" --label "Atari Karts (boundary)" --quiet; \
	else \
		bash scripts/test-skip.sh record "Atari Karts (audio boundary)" "no ROM matching 'Atari Karts*' in the private corpus"; \
	fi
	@# Delivered-vs-advertised sample rate.  A steady deficit drains the
	@# frontend's audio buffer until it underruns -- a pop every few
	@# seconds in every title.  Orthogonal to the boundary check: the
	@# pre-2026-08 core passed this and failed that; the first fix
	@# inverted it.  Both must hold.
	@rom=$$(bash scripts/find-rom.sh 'Atari Karts (1995).jag' 'Atari Karts*.jag' 'Atari Karts*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_rate ./$(TARGET) "$$rom" --label "Atari Karts (rate)" --quiet; \
	else \
		bash scripts/test-skip.sh record "Atari Karts (audio rate)" "no ROM matching 'Atari Karts*' in the private corpus"; \
	fi
	@# Resample-cursor drift (periodic-skip class, #393).  Boundary, rate,
	@# clipping and presence are all blind to this one: the read cursor
	@# lags the write cursor a fraction of a sample per frame until the
	@# gross-drift resync discards ~254 ring samples (~12 ms) in one hop
	@# -- an audible skip every ~36 s NTSC / ~8 s PAL.  Asserts the lag
	@# stays bounded and no resync ever fires during steady-state play.
	@rom=$$(bash scripts/find-rom.sh 'Atari Karts (1995).jag' 'Atari Karts*.jag' 'Atari Karts*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/tools/i2s_lag_probe ./$(TARGET) "$$rom" --frames 2400 --window 2400 --max-lag 64 --max-resyncs 0 --quiet && \
		./test/tools/i2s_lag_probe ./$(TARGET) "$$rom" --frames 2400 --window 2400 --max-lag 64 --max-resyncs 0 --quiet --option virtualjaguar_pal=enabled; \
	else \
		bash scripts/test-skip.sh record "Atari Karts (i2s lag drift)" "no ROM matching 'Atari Karts*' in the private corpus"; \
	fi
	@# Formerly known-broken titles (DSP-synth saturation class): fixed by
	@# the MMULT secondary-bank fix (JTRM: the vector operand is always
	@# register bank 1, not "the non-current bank").  These now assert
	@# clean audio so a regression flips them red again.
	@#
	@# NOTE: the old literal path here ("Skyhammer_(1999).jag") never matched
	@# anything -- the corpus holds it as "Skyhammer (World).j64" -- so this
	@# sentinel silently skipped while the suite still reported exit 0.  The
	@# lookup below matches it case-insensitively under either spelling.
	@#
	@# SETTLED 2026-08-05, measured on "Skyhammer (World).j64": 0.000%
	@# saturated, longest saturation run 0 samples, window RMS 3079.8, first
	@# audio at frame 171.  Skyhammer no longer clips -- the MMULT
	@# secondary-bank fix resolved it -- and it is not silent either, so it is
	@# not the masked-silence failure mode.  Asserting clean, same as the Iron
	@# Soldier 2 line below, is therefore correct on evidence rather than by
	@# default.  CLAUDE.md's "Skyhammer should still fail clipping" was stale
	@# and has been corrected.  Do NOT add an expected-fail wrapper here.
	@rom=$$(bash scripts/find-rom.sh 'Skyhammer_(1999).jag' '*skyhammer*.jag' '*skyhammer*.j64' '*skyhammer*.rom' '*sky hammer*.jag' '*sky hammer*.j64'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_clipping ./$(TARGET) "$$rom" --label Skyhammer --quiet; \
	else \
		bash scripts/test-skip.sh record "Skyhammer (clipping sentinel)" "no ROM matching '*skyhammer*' in the private corpus"; \
	fi
	@rom=$$(bash scripts/find-rom.sh 'Iron Soldier 2 (World).j64' 'Iron Soldier 2*.j64' 'Iron Soldier 2*.jag'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_clipping ./$(TARGET) "$$rom" --label "Iron Soldier 2" --quiet; \
	else \
		bash scripts/test-skip.sh record "Iron Soldier 2 (clipping)" "no ROM matching 'Iron Soldier 2*' in the private corpus"; \
	fi
	@# Presence check: counterpart to the clipping check.  A "fix" that
	@# silences the game (e.g. PR #170 closed without merge) drops RMS
	@# to zero — clipping passes but the game has no audio.  Iron
	@# Soldier 1 boots straight to a music-on title; envelope was
	@# measured on develop (RMS ~1175).  Floor 200 catches silence
	@# regressions; ceiling 25000 catches loud-broken regressions.
	@# The exact 1994 dump is tried first: the envelope was measured on it,
	@# and the [a1]/v1.04 alternates are not guaranteed to share it.
	@rom=$$(bash scripts/find-rom.sh 'Iron Soldier (1994).jag' 'Iron Soldier (World)*.j64' 'Iron Soldier.jag'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_presence ./$(TARGET) "$$rom" --label "Iron Soldier 1" --rms-floor 200 --rms-ceiling 25000 --quiet; \
	else \
		bash scripts/test-skip.sh record "Iron Soldier 1 (audio presence)" "no ROM matching 'Iron Soldier*' in the private corpus"; \
	fi
	@# Same title at risc=2x (issue #314): real game audio through the DSP
	@# with the RISC compute budget doubled.  Presence must stay inside the
	@# measured envelope -- if 2x silences it or blows it out, the scale is
	@# leaking into the audio path.  Same skip discipline as above.
	@rom=$$(bash scripts/find-rom.sh 'Iron Soldier (1994).jag' 'Iron Soldier (World)*.j64' 'Iron Soldier.jag'); \
	if [ -n "$$rom" ]; then \
		./test/test_audio_presence ./$(TARGET) "$$rom" --label "Iron Soldier 1 (risc=2x)" --rms-floor 200 --rms-ceiling 25000 --quiet \
			--option virtualjaguar_risc_clock_scale=2x; \
	else \
		bash scripts/test-skip.sh record "Iron Soldier 1 (audio presence, risc=2x)" "no ROM matching 'Iron Soldier*' in the private corpus"; \
	fi
	@# Save-state determinism: replay the same frames after
	@# retro_unserialize and require identical video AND audio.  This is
	@# what backs `savestate_features = 3` in dist/info/ and the zero
	@# serialization quirks reported from retro_load_game -- rewind,
	@# netplay and run-ahead all assume a state is a complete snapshot.
	@# It caught the DAC register file at F1A148-F1A157 being outside
	@# every STATE_SAVE_BUF; yarc.j64 is in-tree so this never skips, and
	@# a real music-on title is used when the private corpus is present.
	./test/tools/test_runahead_determinism ./$(TARGET) test/roms/yarc.j64 --quiet
	@rom=$$(bash scripts/find-rom.sh 'Iron Soldier (1994).jag' 'Iron Soldier (World)*.j64' 'Iron Soldier.jag'); \
	if [ -n "$$rom" ]; then \
		./test/tools/test_runahead_determinism ./$(TARGET) "$$rom" --quiet; \
	else \
		bash scripts/test-skip.sh record "Iron Soldier 1 (savestate determinism)" "no ROM matching 'Iron Soldier*' in the private corpus"; \
	fi
	@# Issue #400: the two ROMs above are both stock-path titles, so nothing
	@# here exercised a title carrying per-title enhancement defaults — and
	@# the hi-res shadow surface's frame epoch was outside the state blob for
	@# exactly that reason.  Doom takes internal_resolution=2x + true_color
	@# from titledb, so this row is the one that fails if a cache-coherence
	@# counter escapes serialization again.  Keep a DB-tagged title here.
	@rom=$$(bash scripts/find-rom.sh 'Doom - Evil Unleashed (1994).jag' 'Doom (World)*.j64' 'Doom.jag'); \
	if [ -n "$$rom" ]; then \
		./test/tools/test_runahead_determinism ./$(TARGET) "$$rom" --quiet; \
	else \
		bash scripts/test-skip.sh record "Doom (savestate determinism, enhancement path)" "no ROM matching 'Doom*' in the private corpus"; \
	fi
	./test/test_butch_cd
	./test/test_cd_hle_idempotent
	./test/test_cd_pregap
	./test/test_cd_chd
	./test/test_chd_unit
	@# Checker on the committed fixtures: exit 0 = CHSE present, exit 1 =
	@# Jaguar-shaped and missing CHSE. This is the ParseCHD refuse gate
	@# without going through HLE extract.
	./tools/jagcd/jagcd-chd-check test/roms/synth_jagcd.chd
	@if ./tools/jagcd/jagcd-chd-check test/roms/synth_jagcd_nosession.chd; then \
		echo "jagcd-chd-check: expected exit 1 on synth_jagcd_nosession.chd"; exit 1; \
	 else rc=$$?; \
	   if [ $$rc -ne 1 ]; then echo "jagcd-chd-check: unexpected $$rc"; exit $$rc; fi; \
	 fi
	@# Optional: PATH/JAGCD_CHDMAN round-trip. Exit 77 = no CHSE-capable
	@# chdman (CI, Homebrew 0.288). The committed synth_jagcd*.chd files
	@# are the actual CHD load gate; this only checks the converter.
	@if bash test/tools/jagcd_roundtrip.sh; then :; \
	 else rc=$$?; \
	   if [ $$rc -eq 77 ]; then \
	     bash scripts/test-skip.sh record "jagcd CUE->CHD round-trip" "no CHSE-capable chdman on PATH"; \
	   else exit $$rc; fi; \
	 fi
	./test/test_cd_synth_read
	./test/test_cd_synth_butch
	./test/test_cd_synth_cdda
	./test/test_cd_synth_subq
	@# VJ_BIOS_DIR: both tests used to hardcode "test/roms/private" as the
	@# libretro system dir, but the corpus keeps its BIOS dumps one level
	@# down in ROMS/.  The paths never resolved, so ten real-BIOS assertions
	@# reported SKIP while the suite exited 0 -- the same silent-skip class
	@# as the Skyhammer sentinel.  Resolve the directory that actually holds
	@# the Jaguar BIOS and hand it to both tests; they fall back to the old
	@# literal when the variable is unset (CI, which has no BIOS dumps).
	@bios=$$(bash scripts/find-rom.sh '*BIOS* Atari Jaguar (World).j64'); \
	if [ -n "$$bios" ]; then \
		VJ_BIOS_DIR=$$(dirname "$$bios"); export VJ_BIOS_DIR; \
	else \
		bash scripts/test-skip.sh record "Real-BIOS assertions (bios/boot config)" "no '[BIOS] Atari Jaguar (World).j64' in the private corpus"; \
	fi; \
	./test/test_bios_config && ./test/test_boot_config
	./test/test_cart_format ./$(TARGET)
	./test/test_cart_needs_bios ./$(TARGET) --quiet
	./test/test_audio_dac
	./test/tools/test_memory_map ./$(TARGET)
	@# $F14000/$F14002 identity guardrail for the input-devices track
	@# (#428/#429/#436): sweeps the whole joystick read domain and asserts
	@# an FNV digest measured on develop.  Committed before any device code
	@# so later PRs are measured against a number that predates them.
	./test/tools/joymatrix_identity ./$(TARGET) test/roms/yarc.j64 --quiet
	@# Quadrature encoder unit test (no core): the Gray sequence in both
	@# directions, the one-state-per-advance rate policy, the backlog
	@# clamp and the Q8 carry.
	./test/test_quadrature
	@# ST/Amiga mouse end-to-end: synthetic deltas in, decoded direction
	@# and distance out, all three wiring cases and all three poll shapes,
	@# plus row-blindness and the v12 savestate round-trip.
	./test/tools/mouse_decode_test ./$(TARGET) test/roms/yarc.j64 --quiet
	@# Tempest rotary end-to-end (#436): synthetic rotation in, decoded
	@# direction and magnitude out in BOTH directions on BOTH ports, the
	@# row-0-only visibility that distinguishes a matrix device from the
	@# row-blind mouse, Tempest 2000's measured 8-write scan, the
	@# two-controller Pause unlock, the C2/C3 ID bits and a savestate
	@# round-trip mid-rotation.
	./test/tools/rotary_decode_test ./$(TARGET) test/roms/yarc.j64 --quiet
	./test/test_memtrack
	./test/test_nvmbios
	@# Option visibility is content-type dependent; the disc half runs only
	@# when a private CD image is available (the cart half always runs).
	@# Prefer a disc known to load (see docs/cd-boot-matrix.md); fall back to
	@# whatever is present. A disc that will not load is reported as SKIP.
	@# Try discs known to load first (see docs/cd-boot-matrix.md), then any
	@# other. ls sorts all its arguments together, so probe one glob at a
	@# time. A disc that still will not load is reported as SKIP, not FAIL.
	@# The jagniccc.j64 guard that used to wrap this is gone: that ROM is
	@# committed in-tree, so its absence means a broken checkout and must
	@# fail the suite rather than read as a pass -- the same rationale the
	@# test_state_compat and test_frontend_pacing lines below already state.
	@# Only the disc half is genuinely optional, and it now records a ledger
	@# skip so "ran without a disc" is distinguishable from "ran fully".
	@VJ_VIS_ROOT="test/roms/private/Jaguar CD/BinCue"; VJ_VIS_DISC=""; \
		for VJ_VIS_PAT in "Baldies*" "Myst*" "Hover*" "*"; do \
			[ -n "$$VJ_VIS_DISC" ] && break; \
			VJ_VIS_DISC=$$(ls "$$VJ_VIS_ROOT"/$$VJ_VIS_PAT/*.cue 2>/dev/null | head -1); \
		done; \
		if [ -z "$$VJ_VIS_DISC" ]; then \
			bash scripts/test-skip.sh record "Core option visibility (disc half)" "no .cue image under test/roms/private/Jaguar CD/BinCue"; \
		fi; \
		./test/tools/test_option_visibility ./$(TARGET) test/roms/jagniccc.j64 "$$VJ_VIS_DISC"
	./test/tools/test_op_gpu_object ./$(TARGET) test/roms/yarc.j64
	@# Framebuffer integrity: alpha corruption + screen position shift detection.
	@# Run both regions: max_height is region-independent, but the emitted
	@# height is not, so a region-specific overflow must not hide.
	@# Chained with && so an NTSC failure cannot be masked by a passing PAL
	@# run -- with `;` the block would still exit 0 on the second command.
	@# Unguarded on purpose: yarc.j64 is committed in-tree, so a missing file
	@# is a broken checkout, not an absent optional ROM.  The `[ -f ]` guard
	@# that used to wrap this printed a SKIP and exited 0, which is the same
	@# reads-as-a-pass hazard test_state_compat below explicitly refuses.
	./test/test_framebuffer_integrity ./$(TARGET) test/roms/yarc.j64
	./test/test_framebuffer_integrity ./$(TARGET) test/roms/yarc.j64 \
		--option virtualjaguar_pal=enabled
	@# Save-state version gate: every layout a released core wrote (v1, v2,
	@# v3, v7) must still load with its chunks correctly aligned; version 0
	@# and future versions must be refused.  Deliberately NOT wrapped in an
	@# `if [ -f ... ]` guard like the framebuffer test above: yarc.j64 is
	@# committed in-tree, so a missing ROM is a broken checkout and the
	@# test's exit 77 should stop the suite rather than read as a pass.
	./test/test_state_compat ./$(TARGET) test/roms/yarc.j64
	@# Jaguar GameDrive detection + banking + savestate v8 gate.  Fully
	@# synthetic (builds its own probe images), so it runs everywhere,
	@# including checkouts without the private ROM tree.
	./test/test_jgd ./$(TARGET)
	@# Frontend pacing / fast-forward contract: the core must not throttle
	@# itself, and samples-per-frame must match the advertised fps and
	@# sample_rate, otherwise the frontend's audio driver becomes the pacing
	@# bottleneck and fast-forward has nothing to give.  Unguarded for the
	@# same reason as test_state_compat above: yarc.j64 is committed in-tree,
	@# so a missing ROM means a broken checkout and should fail the suite
	@# rather than silently read as a pass.
	./test/test_frontend_pacing ./$(TARGET) test/roms/yarc.j64 --quiet
	@# Clock-scale (issue #314) audio-pacing contract at non-1x.  The one
	@# invariant the scale options must never break is sample pacing:
	@# I2S/DAC event scheduling stays on the unscaled sysclock, so the
	@# audio_rate_contract (exactly 48000/fps samples per frame, 1:1
	@# batches) must hold at every scale -- a violation here is a pitch
	@# shift, the PR #170 regression class.  yarc.j64 is committed
	@# in-tree, so these run everywhere including CI.  The m68k=0.5x row
	@# also exercises the underclock error-diffusion path (zero-budget
	@# slices must skip, not execute).
	@# --max-fastest-frame-fraction 100 defuses the host-speed check on
	@# these rows only: at 2x/2x the emulator does ~double the work per
	@# frame, so "fastest frame < 0.5x period" is a property of the host,
	@# not of the scale options, and would flake on loaded CI runners
	@# (observed locally: 11.3 ms vs the 8.3 ms limit under parallel
	@# builds).  The assertions that matter here -- audio_rate_contract,
	@# one_batch_per_frame, geometry_stability -- keep full strength.
	./test/test_frontend_pacing ./$(TARGET) test/roms/yarc.j64 --quiet \
		--max-fastest-frame-fraction 100 \
		--option virtualjaguar_risc_clock_scale=2x \
		--option virtualjaguar_m68k_clock_scale=2x
	./test/test_frontend_pacing ./$(TARGET) test/roms/yarc.j64 --quiet \
		--max-fastest-frame-fraction 100 \
		--option virtualjaguar_m68k_clock_scale=0.5x
	@# EEPROM lifecycle test: generates a test ROM, then exercises load/unload/reload.
	@$(CC) -O2 -Wall -o /tmp/gen_eeprom_test_rom test/tools/gen_eeprom_test_rom.c && \
		/tmp/gen_eeprom_test_rom /tmp/eeprom_lifecycle_test.j64 && \
		./test/test_eeprom_lifecycle ./$(TARGET) /tmp/eeprom_lifecycle_test.j64
	@# EEPROM read-race test: joystick polls must not steal EEPROM DO bits
	@# (Raiden background-music death regression).
	./test/test_eeprom_read_race ./$(TARGET) /tmp/eeprom_lifecycle_test.j64
	@# Per-title enhancement defaults DB E2E (#368): apply / disable /
	@# user-override contract, driven through the real dlopen'd core.
	@# shadowHiresN is fixed for the whole session at retro_load_game
	@# time, so each case is a separate process invocation -- exactly
	@# like a real frontend restart.  The seed CRC (0xDC187F82) is
	@# verified against the actual ROM bytes first: a corpus rip that
	@# does not match the seed would otherwise make the test "pass"
	@# without ever exercising the DB lookup it claims to test.
	@avp=$$(bash scripts/find-rom.sh 'Alien vs Predator (1994).jag' '*Alien*Predator*.jag' '*Alien*Predator*.j64'); \
	if [ -n "$$avp" ]; then \
		avp_crc=$$(python3 -c "import zlib,sys; print('0x%08X' % zlib.crc32(open(sys.argv[1],'rb').read()))" "$$avp" 2>/dev/null); \
		if [ "$$avp_crc" = "0xDC187F82" ]; then \
			rc=0; \
			./test/tools/test_pertitle_db ./$(TARGET) "$$avp" --case 1 --quiet || rc=1; \
			./test/tools/test_pertitle_db ./$(TARGET) "$$avp" --case 2 --quiet \
				--option virtualjaguar_pertitle_defaults=disabled || rc=1; \
			./test/tools/test_pertitle_db ./$(TARGET) "$$avp" --case 3 --quiet \
				--option virtualjaguar_pertitle_defaults=disabled \
				--option virtualjaguar_internal_resolution=2x || rc=1; \
			./test/tools/test_pertitle_db ./$(TARGET) "$$avp" --case 4 --quiet \
				--option virtualjaguar_true_color=disabled || rc=1; \
			./test/tools/test_pertitle_db ./$(TARGET) "$$avp" --case 6 --quiet || rc=1; \
			exit $$rc; \
		else \
			bash scripts/test-skip.sh record "Per-title defaults (AvP apply/disable/override)" \
				"AvP ROM CRC $$avp_crc != 0xDC187F82 (seed mismatch)"; \
		fi; \
	else \
		bash scripts/test-skip.sh record "Per-title defaults (AvP apply/disable/override)" \
			"no ROM matching 'Alien vs Predator*' in the private corpus"; \
	fi
	@# Non-DB ROM control: no CRC match -> no substitution, [titledb] miss log fires.
	@# yarc.j64 is committed in-tree so this case never skips.
	./test/tools/test_pertitle_db ./$(TARGET) test/roms/yarc.j64 --case 5 --quiet
	@# Enhancement hooks (issue #370).  All four gates run on in-repo public
	@# content, so none of them can skip -- and hook_identity_ab.sh now
	@# ENFORCES that rather than asserting it: it counts the ROMs it actually
	@# compared and fails on zero, instead of exiting 0 having skipped them
	@# all.  The hook array is installed
	@# programmatically via TitleDBSetHooksForTest -- deliberately NOT as a
	@# canary row in the shipped table, which would break --case 5 above.
	./test/tools/test_hook_gate ./$(TARGET) test/roms/yarc.j64 --case on --quiet
	./test/tools/test_hook_gate ./$(TARGET) test/roms/yarc.j64 --case off --quiet
	./test/tools/test_hook_gate ./$(TARGET) test/roms/yarc.j64 --case mismatch --quiet
	@# Stock-path identity: with the gate at its default AND with it turned
	@# on (no shipped row carries a hook), the per-frame framebuffer-hash
	@# CSVs must be byte-identical.  Epic #338's non-negotiable guardrail.
	bash test/tools/hook_identity_ab.sh ./$(TARGET)
	@echo ""
	@echo "Note: test/test_cd_boot, test/test_cd_hle_boot, test/test_cd_bios_boot,"
	@echo "test/test_cd_toc_contract (needs VJ_TOC_DISC=<image>),"
	@echo "test/test_cd_fifo_stream, test/test_cd_second_transfer and test/test_cd_lost_wakeup"
	@echo "(need VJ_FIFO_DISC=<image>),"
	@echo "test/test_cd_ssi_stream (needs VJ_SSI_DISC=<image with an audio track>),"
	@echo "and test/test_blitter (register-readback) are built but not run from"
	@echo "'make test'. The CD sweeps walk every disc in test/roms/private/; the"
	@echo "blitter readback tests probe register read paths that the emulator"
	@echo "does not currently expose. Invoke them directly when validating"
	@echo "regressions in those subsystems."
	@# Skip roll-up.  Every optional check that did not run is listed here by
	@# name and reason, so an inert sentinel cannot hide behind exit 0 the way
	@# the Skyhammer clipping check did.  Non-fatal by default (CI has none of
	@# the private ROMs); VJ_REQUIRE_ROMS=1 makes any skip a hard failure.
	@bash scripts/test-skip.sh summary

test/test_cheat: test/test_cheat.c src/core/cheat.c src/core/cheat.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_cheat.c src/core/cheat.c

test/test_titledb: test/tools/test_titledb.c src/core/titledb.c src/core/crc32.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_titledb.c src/core/titledb.c src/core/crc32.c

# Known cart boot ROM image table unit test (issue #469). Links biosdb.c +
# crc32.c + the two embedded boot ROM blobs directly, same no-dlopen style
# as test_titledb: no core, no private ROMs.
test/test_biosdb: test/tools/test_biosdb.c src/core/biosdb.c src/core/crc32.c \
		src/bios/jagbios.c src/bios/jagbios_m.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_biosdb.c src/core/biosdb.c src/core/crc32.c \
		src/bios/jagbios.c src/bios/jagbios_m.c

# Custom cart boot ROM loader end-to-end test (issue #469): dlopens the
# built core and drives retro_load_game() with a synthetic cart image to
# exercise stage_cart_boot_rom()'s 'custom' path (file present, file
# absent -> embedded-K fallback) plus a k/m-unchanged sanity check.
test/test_cart_bios_loader: test/test_cart_bios_loader.c $(TARGET) \
		src/bios/jagbios.c src/bios/jagbios_m.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cart_bios_loader.c \
		src/bios/jagbios.c src/bios/jagbios_m.c -ldl

# Enhancement-hook applier (issue #370).  Host unit test: no dlopen, no
# ROMs.  titlehook.c's pure half takes the image as a parameter, so this
# links with the same two-file style as test_titledb plus a handful of
# stubbed core globals defined in the test itself.
test/test_titlehook: test/tools/test_titlehook.c src/core/titlehook.c \
		src/core/titledb.c src/core/crc32.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_titlehook.c src/core/titlehook.c \
		src/core/titledb.c src/core/crc32.c

test/test_event_queue: test/test_event_queue.c src/core/event.c src/core/event.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_event_queue.c src/core/event.c

test/test_jlink: test/test_jlink.c src/jerry/jlink.c src/jerry/jlink.h src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_jlink.c src/jerry/jlink.c src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c

test/test_jlink_tcp: test/test_jlink_tcp.c src/jerry/jlink.c src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c src/jerry/jlink.h src/jerry/jlink_tcp.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_jlink_tcp.c src/jerry/jlink.c src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c

test/test_jlink_netpacket: test/test_jlink_netpacket.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_jlink_netpacket.c -ldl

test/test_uart_loopback: test/test_uart_loopback.c src/jerry/uart.c src/jerry/uart.h src/jerry/jlink.c src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c src/core/event.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_uart_loopback.c src/jerry/uart.c src/jerry/jlink.c src/jerry/jlink_tcp.c src/jerry/jlink_netpacket.c src/core/event.c -lm

test/test_uart_core: test/test_uart_core.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/test_uart_core.c test/harness/harness.c -ldl -lm

test/test_fountain_crash: test/test_fountain_crash.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/test_fountain_crash.c test/harness/harness.c -ldl -lm

test/test_netlink_host: test/test_netlink_host.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/test_netlink_host.c test/harness/harness.c -ldl -lm

test/tools/netlink_pair: test/tools/netlink_pair.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/tools/netlink_pair.c test/harness/harness.c -ldl -lm

test/tools/netlink_latency: test/tools/netlink_latency.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/tools/netlink_latency.c test/harness/harness.c -ldl -lm

test/tools/netlink_delay_proxy: test/tools/netlink_delay_proxy.c
	$(CC) -O2 -Wall -o $@ test/tools/netlink_delay_proxy.c

test/test_dram_timing: test/test_dram_timing.c src/core/bus_arbiter.c src/core/bus_arbiter.h
	$(CC) -O2 -Wall -std=c99 -o $@ test/test_dram_timing.c src/core/bus_arbiter.c

test/tools/netlink_game: test/tools/netlink_game.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) -Itest \
		-o $@ test/tools/netlink_game.c test/harness/harness.c -ldl -lm

# Regression guard: textually verifies that JERRYResetPIT1/2,
# TOMResetPIT, and JERRYGetPIT*Frequency schedule using RISC clock
# constants (full system clock).  Catches the recurring "halve PIT
# rate to fix Doom" bug -- see docs/jtrm-clocks-timing.md and
# test/acid/tests/timing/pit_countdown_rate.s for the in-emulation
# equivalent.
test/test_pit_clock_rate: test/test_pit_clock_rate.c \
		src/jerry/jerry.c src/tom/tom.c
	$(CC) -O2 -Wall -std=c99 -o $@ test/test_pit_clock_rate.c

test/test_blitter_mmio: test/test_blitter_mmio.c src/tom/blitter_mmio.c \
		src/tom/blitter_internal.h src/tom/blitter.h src/core/vjag_memory.h \
		src/core/settings.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_blitter_mmio.c src/tom/blitter_mmio.c

test/test_tom_visible_window: test/test_tom_visible_window.c src/tom/tom.c \
		src/tom/tom.h src/core/vjag_memory.h src/core/settings.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_tom_visible_window.c

test/test_blitter_simd: test/test_blitter_simd.c $(BLITTER_SIMD_SRC) src/tom/blitter_simd.h \
	$(BLITTER_SIMD_SRC:.c=.h)
	$(CC) $(CFLAGS) -o $@ test/test_blitter_simd.c $(BLITTER_SIMD_SRC)

test/test_dsp_mac40: test/test_dsp_mac40.c src/jerry/dsp_acc40.h
	$(CC) -O2 -Wall $(INCFLAGS) -o $@ test/test_dsp_mac40.c

test/test_m68k_ops: test/test_m68k_ops.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_m68k_ops.c -ldl

test/test_m68k_irq_ssp: test/test_m68k_irq_ssp.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_m68k_irq_ssp.c -ldl

test/test_gpu_ops: test/test_gpu_ops.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_gpu_ops.c -ldl

test/test_dsp_ops: test/test_dsp_ops.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_dsp_ops.c -ldl

test/test_hle_bios: test/test_hle_bios.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_hle_bios.c -ldl

test/test_dsp_unit: test/test_dsp_unit.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_dsp_unit.c -ldl

test/test_subsystem_init: test/test_subsystem_init.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_subsystem_init.c -ldl

test/test_subsystem_timeline: test/test_subsystem_timeline.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_subsystem_timeline.c -ldl

test/test_irq_cascade: test/test_irq_cascade.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_irq_cascade.c -ldl

test/test_boot_patterns: test/test_boot_patterns.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_boot_patterns.c -ldl

test/test_audio_pipeline: test/test_audio_pipeline.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_pipeline.c -ldl -lm

test/test_audio_clipping: test/test_audio_clipping.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_clipping.c -ldl -lm

test/test_audio_presence: test/test_audio_presence.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_presence.c -ldl -lm

test/test_audio_boundary: test/test_audio_boundary.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_boundary.c -ldl -lm

test/test_audio_rate: test/test_audio_rate.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_rate.c -ldl -lm

test/tools/i2s_lag_probe: test/tools/i2s_lag_probe.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/i2s_lag_probe.c test/harness/harness.c -ldl -lm

test/test_memtrack: test/test_memtrack.c src/core/memtrack.c src/core/memtrack.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_memtrack.c src/core/memtrack.c -lm

test/test_nvmbios: test/test_nvmbios.c src/core/nvmbios.c src/core/nvmbios.h \
		src/core/memtrack.c src/core/memtrack.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_nvmbios.c src/core/nvmbios.c src/core/memtrack.c -lm

test/tools/test_memory_map: test/tools/test_memory_map.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_memory_map.c -ldl

# E2E behaviour test for the per-title enhancement defaults DB (#368):
# apply / disable / user-override contract, driven through the real core
# via the shared harness.  Needs shadowHiresN + shadowFBActive from the
# wide test symbol set (exports-test.list / link-test.T).
test/tools/test_pertitle_db: test/tools/test_pertitle_db.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_pertitle_db.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

# End-to-end enhancement-hook gate wiring (issue #370).  Needs
# TitleDBSetHooksForTest + TitleHookApplyROM + jagMemSpace from the wide
# test symbol set (exports-test.list / link-test.T) -- note TitleHook* is
# NOT covered by the TitleDB* wildcard and is listed separately there.
test/tools/test_hook_gate: test/tools/test_hook_gate.c \
		test/harness/harness.c test/harness/harness.h \
		src/core/titledb.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_hook_gate.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

# $F14000 / $F14002 identity guardrail (#428 input-devices track).  Needs
# the wide test ABI's Joystick* / joypad0Buttons / joypad1Buttons exports.
test/tools/joymatrix_identity: test/tools/joymatrix_identity.c \
		src/jerry/inputdev.h \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/joymatrix_identity.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

test/tools/mouse_decode_test: test/tools/mouse_decode_test.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/mouse_decode_test.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

test/tools/rotary_decode_test: test/tools/rotary_decode_test.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/rotary_decode_test.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

test/test_quadrature: test/test_quadrature.c src/jerry/quadrature.c src/jerry/quadrature.h
	$(CC) -O2 -Wall $(INCFLAGS) -o $@ test/test_quadrature.c src/jerry/quadrature.c

test/tools/test_option_visibility: test/tools/test_option_visibility.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_option_visibility.c -ldl

test/tools/test_op_gpu_object: test/tools/test_op_gpu_object.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_op_gpu_object.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

test/tools/test_dsp_audio_diag: test/tools/test_dsp_audio_diag.c \
		test/harness/harness.c test/harness/harness.h \
		test/harness/dsp_probe.c test/harness/dsp_probe.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_dsp_audio_diag.c \
		test/harness/harness.c test/harness/dsp_probe.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/test_eeprom_lifecycle: test/test_eeprom_lifecycle.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_eeprom_lifecycle.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/test_eeprom_read_race: test/test_eeprom_read_race.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_eeprom_read_race.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/test_framebuffer_integrity: test/test_framebuffer_integrity.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_framebuffer_integrity.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

# Save-state backwards-compatibility regression guard.  Needs DACStateSave
# from the wide test symbol set (DAC* in exports-test.list / link-test.T).
test/test_state_compat: test/test_state_compat.c \
		test/harness/harness.c test/harness/harness.h src/core/state.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_state_compat.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

# Jaguar GameDrive (JagGD) detection + banking.  Synthetic-only: builds
# its own probe cartridge images at runtime, no private ROMs needed.
# Needs jgd*/JGD* from the wide test symbol set.
test/test_jgd: test/test_jgd.c \
		test/harness/harness.c test/harness/harness.h \
		src/core/state.h src/core/jaggd.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_jgd.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/tools/test_runahead_determinism: test/tools/test_runahead_determinism.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_runahead_determinism.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/tools/test_wedge_spin: test/tools/test_wedge_spin.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_wedge_spin.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

test/test_frontend_pacing: test/test_frontend_pacing.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_frontend_pacing.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm

# Drives blitter_blit() through the MMIO path with varied B_CMD words and
# checks destination bytes, plus a save-state check on the command field
# decode (the render-inert half of it) -- see the header comment in the
# test for why both halves are needed.
test/test_blitter_cmd: test/test_blitter_cmd.c \
		test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_blitter_cmd.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm

# CD-specific test harnesses (imported from PR #109).  Tests SKIP gracefully
# when no disc images are present in test/roms/private/, so CI without
# private ROMs still passes.
test/test_butch_cd: test/test_butch_cd.c test/test_framework.h test/mister_ground_truth.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_butch_cd.c -ldl

test/test_bios_config: test/test_bios_config.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_bios_config.c -ldl

test/test_boot_config: test/test_boot_config.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_boot_config.c -ldl

test/test_cart_format: test/test_cart_format.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cart_format.c -ldl

test/test_cart_needs_bios: test/test_cart_needs_bios.c test/harness/harness.c test/harness/harness.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cart_needs_bios.c test/harness/harness.c -ldl -lm

test/test_cd_boot: test/test_cd_boot.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_boot.c -ldl

test/test_cd_hle_boot: test/test_cd_hle_boot.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_hle_boot.c -ldl

test/test_cd_pregap: test/test_cd_pregap.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_pregap.c -ldl

test/test_cd_chd: test/test_cd_chd.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_chd.c -ldl

test/test_cd_synth_read: test/test_cd_synth_read.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_synth_read.c -ldl

test/test_cd_synth_butch: test/test_cd_synth_butch.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_synth_butch.c -ldl

test/test_cd_synth_cdda: test/test_cd_synth_cdda.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_synth_cdda.c -ldl

test/test_cd_synth_subq: test/test_cd_synth_subq.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_synth_subq.c -ldl

test/test_cd_bios_boot: test/test_cd_bios_boot.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_bios_boot.c -ldl

test/test_cd_hle_idempotent: test/test_cd_hle_idempotent.c test/test_framework.h test/cd_assertions.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_hle_idempotent.c -ldl

test/test_cd_toc_contract: test/test_cd_toc_contract.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_toc_contract.c -ldl

test/test_cd_fifo_stream: test/test_cd_fifo_stream.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_fifo_stream.c -ldl

test/test_cd_ssi_stream: test/test_cd_ssi_stream.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_ssi_stream.c -ldl

test/test_cd_second_transfer: test/test_cd_second_transfer.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_second_transfer.c -ldl

test/test_cd_lost_wakeup: test/test_cd_lost_wakeup.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_cd_lost_wakeup.c -ldl

test/test_audio_dac: test/test_audio_dac.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_audio_dac.c -ldl -lm

test/test_blitter: test/test_blitter.c test/test_framework.h
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/test_blitter.c -ldl

# Diagnostic CD harnesses: invoked manually with a CUE/CHD argument.
test/dump_pc: test/dump_pc.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/dump_pc.c -ldl

test/heap_search: test/heap_search.c
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 $(INCFLAGS) \
		-o $@ test/heap_search.c -ldl

# Aggregate target for the manual diagnostic tools.
.PHONY: tools
tools: test/dump_pc test/heap_search test/test_cd_boot tools/jagcd/jagcd-chd-check
endif

.PHONY: clean test lint coverage benchmark acid dsp-diag frame-timing cue2cdi \
        runahead-determinism jaguar-demos jaguar-demos-fetch jaguar-demos-build \
        jaguar-demos-smoke jaguar-demos-full jaguar-demos-baseline
endif

# Standalone libchdr binaries (no core dlopen / no TEST_EXPORTS). Live
# outside the TEST_EXPORTS=1 branch so Windows MSYS2 CI can build them
# without flushing the object tree. An explicit test/test_chd_unit rule
# beats the TEST_EXPORTS!=1 catch-all.
test/test_chd_unit: test/test_chd_unit.c $(SOURCES_LIBCHDR)
	$(CC) -O2 -Wall -Wno-unused-function -Wno-unused-variable -std=c99 \
		$(INCFLAGS) $(LIBCHDR_CFLAGS) $(LIBCHDR_WARNFLAGS) \
		-o $@ test/test_chd_unit.c $(SOURCES_LIBCHDR)

tools/jagcd/jagcd-chd-check: tools/jagcd/jagcd-chd-check.c $(SOURCES_LIBCHDR)
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) $(LIBCHDR_CFLAGS) \
		-o $@ tools/jagcd/jagcd-chd-check.c $(SOURCES_LIBCHDR)

lint:
	@scripts/c89-lint.sh

# `make coverage` -- builds with gcov instrumentation, runs the full
# test suite, and produces a Cobertura XML report at coverage.xml plus
# a textual summary.  See gcovr.cfg for path filters.
coverage:
	$(MAKE) clean
	$(MAKE) COVERAGE=1 TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	@# VJ_INSTRUMENTED_BUILD: the core is built -O0 + --coverage here and
	@# runs below realtime, so wall-clock assertions must skip rather than
	@# report a false failure.  It has to be an env var: the test binaries
	@# compile with $(INCFLAGS) only, so a -D on $(FLAGS) never reaches them.
	VJ_INSTRUMENTED_BUILD=1 $(MAKE) COVERAGE=1 TEST_EXPORTS=1 test
	gcovr --config gcovr.cfg --xml-pretty -o coverage.xml --txt --print-summary

# `make benchmark` -- headless wall-clock perf measurement on a fixed
# ROM.  Boots the core via dlopen, runs $(BENCH_FRAMES) frames after
# $(BENCH_WARMUP) warmup, prints FPS / ms-per-frame.  Use during
# perf-tuning code changes; commit-by-commit deltas are the signal.
#
# Override on the command line:
#   make benchmark BENCH_ROM=test/roms/private/Atari\ Karts.jag
#   make benchmark BENCH_FRAMES=3000 BENCH_WARMUP=120
#   make benchmark BENCH_BLITTER=accurate    # default: fast
BENCH_ROM     ?= test/roms/yarc.j64
BENCH_FRAMES  ?= 600
BENCH_WARMUP  ?= 60
BENCH_BLITTER ?= fast
# BENCH_PROFILE=1 enables src/core/perf_counters.h instrumentation and
# wide-export ABI so test_benchmark can dlsym `perf_counters_dump`.
ifeq ($(BENCH_PROFILE),1)
BENCH_TEST_EXPORTS := TEST_EXPORTS=1
else
BENCH_TEST_EXPORTS :=
endif
benchmark:
	@# Re-invoke make so BENCH_PROFILE / TEST_EXPORTS take effect on the .so/.dylib.
	$(MAKE) $(BENCH_TEST_EXPORTS) BENCH_PROFILE=$(BENCH_PROFILE) -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	@# Build the harness inline; it dlopens the core, so it only needs the retro_* ABI
	@# (plus the optional perf_counters_dump symbol when BENCH_PROFILE=1).
	@# -ldl is Linux-specific; macOS/BSD provide dl* in libSystem/libc.
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o test/tools/test_benchmark test/tools/test_benchmark.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl)
	./test/tools/test_benchmark ./$(TARGET) "$(BENCH_ROM)" $(BENCH_FRAMES) \
		--warmup $(BENCH_WARMUP) --blitter $(BENCH_BLITTER) \
		$(if $(BENCH_STATE),--load-state "$(BENCH_STATE)")

# `make acid` -- builds the core and runs the synthetic acid-test ROMs
# (see test/acid/README.md).  Requires the vasm 68K assembler on $PATH;
# if absent, the assemble step is skipped and only the runner harness
# is built (so CI can still validate the harness compiles).
#
# Forces a BENCH_PROFILE=1 + TEST_EXPORTS=1 build of the core so the
# acid runner can dlsym `perf_counters_find` and report a per-test
# delta (halflines, vblank IRQs, blits, inner-loop iters, ...).
acid:
	$(MAKE) BENCH_PROFILE=1 TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(MAKE) -C test/acid test CORE=$(abspath $(TARGET))

# `make dsp-diag` -- DSP audio diagnostic.  Builds core with TEST_EXPORTS=1,
# compiles the harness + DSP probe, then runs the diagnostic against a ROM.
#
# Usage:
#   make dsp-diag DSP_DIAG_ROM="test/roms/private/Wolfenstein 3D (1994).jag"
#   make dsp-diag DSP_DIAG_ROM=path/to/rom.jag DSP_DIAG_FLAGS="--bios --frames 600"
DSP_DIAG_ROM   ?= test/roms/private/Wolfenstein 3D (1994).jag
DSP_DIAG_FLAGS ?= --dump-on-escape
dsp-diag:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o test/tools/test_dsp_audio_diag \
		test/tools/test_dsp_audio_diag.c \
		test/harness/harness.c test/harness/dsp_probe.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm
	./test/tools/test_dsp_audio_diag ./$(TARGET) "$(DSP_DIAG_ROM)" $(DSP_DIAG_FLAGS)

# `make frame-timing` -- Per-frame timing diagnostic.  Builds core with
# BENCH_PROFILE=1 + TEST_EXPORTS=1, compiles the timing probe + harness,
# then runs the diagnostic against a ROM.  Reports per-frame halfline counts,
# M68K/RISC cycles, VBlank IRQs, wall-clock time, and speed ratio.
#
# Usage:
#   make frame-timing FRAME_TIMING_ROM=test/roms/yarc.j64
#   make frame-timing FRAME_TIMING_ROM="path/to/Doom.jag" FRAME_TIMING_FLAGS="--frames 1200 --csv"
#   make frame-timing FRAME_TIMING_ROM="path/to/rom.jag" FRAME_TIMING_FLAGS="--pal --bios"
FRAME_TIMING_ROM   ?= test/roms/yarc.j64
FRAME_TIMING_FLAGS ?=
frame-timing:
	$(MAKE) BENCH_PROFILE=1 TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o test/tools/test_frame_timing \
		test/tools/test_frame_timing.c \
		test/harness/harness.c test/harness/timing_probe.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm
	./test/tools/test_frame_timing ./$(TARGET) "$(FRAME_TIMING_ROM)" $(FRAME_TIMING_FLAGS)

# `make runahead-determinism` -- Save-state determinism check.  Saves a state,
# replays the same frames after retro_unserialize, and asserts the video and
# audio come back identical.  This is the evidence behind
# `savestate_features = 3` in dist/info/, and behind reporting zero
# serialization quirks: run-ahead, rewind and netplay all assume a state is a
# complete snapshot.
#
# NOT part of `make test`: one assertion (audio_replay_identical) is a known
# failure — a single frame of ~0.05% RMS drift on the first rollback.  See the
# tool header for the full measurement and what has already been ruled out.
#
# Usage:
#   make runahead-determinism RUNAHEAD_ROM="path/to/game.j64"
#   make runahead-determinism RUNAHEAD_ROM="path/to/game.j64" RUNAHEAD_FLAGS="--warmup 600 --frames 300"
RUNAHEAD_ROM   ?= test/roms/yarc.j64
RUNAHEAD_FLAGS ?=
runahead-determinism:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o test/tools/test_runahead_determinism \
		test/tools/test_runahead_determinism.c \
		test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl -lrt) -lm
	./test/tools/test_runahead_determinism ./$(TARGET) "$(RUNAHEAD_ROM)" $(RUNAHEAD_FLAGS)

# Automated visual + audio verification for CD titles: frame-motion timeline,
# audio RMS, periodic screenshots (PPM).  See the tool header for usage.
#   make cd-visual CD_VISUAL_DISC="test/roms/private/Title/Title.cue" \
#        CD_VISUAL_FLAGS="--bios --frames 3000 --outdir /tmp/cdshots"
CD_VISUAL_DISC  ?=
CD_VISUAL_FLAGS ?= --bios --frames 3000
cd-visual:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o test/tools/cd_visual_verify \
		test/tools/cd_visual_verify.c test/harness/harness.c \
		$(if $(filter Linux,$(shell uname -s)),-ldl) -lm
	@if [ -n "$(CD_VISUAL_DISC)" ]; then \
		VJ_EXPECT_BUILD=$$(./scripts/build-id.sh) \
		./test/tools/cd_visual_verify ./$(TARGET) "$(CD_VISUAL_DISC)" $(CD_VISUAL_FLAGS); \
	else \
		echo "built test/tools/cd_visual_verify -- pass CD_VISUAL_DISC=<image> to run"; \
	fi

# `make cue2cdi` -- build the standalone CUE/BIN -> DiscJuggler CDI
# converter (host-side tool, libc only; no core, no libretro deps).
# Not part of the default build or `make test`; build it on demand.
#
# Usage:
#   make cue2cdi
#   ./test/tools/cue2cdi game.cue --verify
#   ./test/tools/cue2cdi --batch --verify path/to/dumps/
.PHONY: cue2cdi
cue2cdi:
	$(CC) -O2 -Wall -std=c99 -o test/tools/cue2cdi test/tools/cue2cdi.c

# ---------------------------------------------------------------------------
# JaguarDemos corpus (https://codeberg.org/42Bastian/JaguarDemos)
#
# On-demand clone + cart_boot_probe sweep.  NOT part of `make test`.
# Smoke (curated list) is what PR CI runs; full is for release PRs / tags.
# See test/jaguar-demos/README.md.  The clone is gitignored under
# test/vendor/JaguarDemos — not a submodule, so a normal checkout stays
# small and CI pins the SHA in test/jaguar-demos/PIN.
# ---------------------------------------------------------------------------
.PHONY: jaguar-demos jaguar-demos-fetch jaguar-demos-build \
        jaguar-demos-smoke jaguar-demos-full jaguar-demos-baseline
jaguar-demos-fetch:
	bash test/jaguar-demos/run.sh fetch

jaguar-demos-build: jaguar-demos-fetch
	bash test/jaguar-demos/run.sh build

jaguar-demos-smoke jaguar-demos: export VJ_EXPECT_BUILD := $(shell ./scripts/build-id.sh)
jaguar-demos-smoke jaguar-demos:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	bash test/jaguar-demos/run.sh smoke ./$(TARGET)

jaguar-demos-full: export VJ_EXPECT_BUILD := $(shell ./scripts/build-id.sh)
jaguar-demos-full:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	bash test/jaguar-demos/run.sh build
	bash test/jaguar-demos/run.sh full ./$(TARGET)

jaguar-demos-baseline: export VJ_EXPECT_BUILD := $(shell ./scripts/build-id.sh)
jaguar-demos-baseline:
	$(MAKE) TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	bash test/jaguar-demos/run.sh build
	bash test/jaguar-demos/run.sh baseline ./$(TARGET)

print-%:
	@echo '$*=$($*)'
