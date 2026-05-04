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
CORE_BASE_VERSION := v2.2.0

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
else
LINK_SCRIPT := link.T
MACHO_EXPORTS := exports.list
endif
MACHO_EXPORTS_FLAGS := -Wl,-exported_symbols_list,$(MACHO_EXPORTS)

# Unix
ifeq ($(platform), unix)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	ifneq ($(findstring SunOS,$(shell uname -a)),)
		SHARED := -shared -z defs -z gnu-version-script-compat
	else
		SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
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
	CFLAGS += -Ofast \
	-flto=4 -fwhole-program -fuse-linker-plugin \
	-fdata-sections -ffunction-sections -Wl,--gc-sections \
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
	CC = qcc -Vgcc_ntoarmv7le
	CXX = QCC -Vgcc_ntoarmv7le_cpp

# ARM
else ifneq (,$(findstring armv,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,--no-undefined -Wl,--version-script=$(LINK_SCRIPT)
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
	LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm

endif

CORE_DIR     := .

include Makefile.common

OBJECTS := $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o)

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
$(LIBRARY_NAME)_CFLAGS += $(CFLAGS) $(COMMON_FLAGS)
$(LIBRARY_NAME)_CXXFLAGS += $(CXXFLAGS) $(COMMON_FLAGS)
${LIBRARY_NAME}_FILES = $(SOURCES_CXX) $(SOURCES_C)
include $(THEOS_MAKE_PATH)/library.mk
else
all: $(TARGET)
$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(LD) $(LINKOUT)$@ $^ $(LDFLAGS)
endif

# version.h dependency hook (must come after `all:` so Make 3.81 on
# stock macOS doesn't latch onto libretro.o as the default goal).
$(CORE_DIR)/libretro.o: $(VERSION_H)

clean:
	rm -f $(TARGET) $(OBJECTS) \
		test/test_cheat test/test_event_queue test/test_blitter_simd \
		test/test_dsp_mac40 test/test_m68k_ops test/test_gpu_ops \
		test/test_dsp_ops test/test_dsp_unit test/test_hle_bios \
		test/test_subsystem_init test/test_subsystem_timeline \
		test/test_irq_cascade test/test_boot_patterns test/test_audio_pipeline \
		test/test_audio_clipping test/test_pit_clock_rate \
		test/test_blitter_mmio test/tools/test_memory_map

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
else
test: test/test_cheat test/test_event_queue test/test_blitter_simd test/test_dsp_mac40 \
		$(TARGET) test/test_m68k_ops test/test_gpu_ops test/test_dsp_ops \
		test/test_dsp_unit test/test_hle_bios test/test_subsystem_init \
		test/test_subsystem_timeline test/test_irq_cascade test/test_boot_patterns \
		test/test_audio_pipeline test/test_audio_clipping test/test_pit_clock_rate \
		test/test_blitter_mmio test/tools/test_memory_map
	./test/test_cheat
	./test/test_event_queue
	./test/test_blitter_mmio
	./test/test_pit_clock_rate
	./test/test_blitter_simd
	./test/test_dsp_mac40
	./test/test_m68k_ops
	./test/test_gpu_ops
	./test/test_dsp_ops
	./test/test_dsp_unit
	./test/test_hle_bios
	./test/test_subsystem_init ./$(TARGET)
	./test/test_subsystem_timeline ./$(TARGET)
	./test/test_irq_cascade ./$(TARGET)
	./test/test_boot_patterns
	./test/test_audio_pipeline ./$(TARGET)
	./test/test_audio_clipping ./$(TARGET) --self-test
	@# Negative control: healthy boot should not trip the clipping detector.
	@if [ -f "test/roms/private/Atari Karts (1995).jag" ]; then \
		./test/test_audio_clipping ./$(TARGET) "test/roms/private/Atari Karts (1995).jag" --label "Atari Karts (negative control)" --quiet; \
	else \
		echo "  SKIP: Atari Karts ROM (private) not available"; \
	fi
	@# Known-broken titles: --expect-clipping makes the test pass while the
	@# bug is still there, but flips red the day a DSP-side fix lands and
	@# clipping disappears — forces this manifest to be updated.
	@if [ -f "test/roms/private/Skyhammer_(1999).jag" ]; then \
		./test/test_audio_clipping ./$(TARGET) "test/roms/private/Skyhammer_(1999).jag" --label Skyhammer --expect-clipping --quiet; \
	else \
		echo "  SKIP: Skyhammer ROM (private) not available"; \
	fi
	@if [ -f "test/roms/private/Iron Soldier 2 (World).j64" ]; then \
		./test/test_audio_clipping ./$(TARGET) "test/roms/private/Iron Soldier 2 (World).j64" --label "Iron Soldier 2" --expect-clipping --quiet; \
	else \
		echo "  SKIP: Iron Soldier 2 ROM (private) not available"; \
	fi
	./test/tools/test_memory_map ./$(TARGET)

test/test_cheat: test/test_cheat.c src/core/cheat.c src/core/cheat.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_cheat.c src/core/cheat.c

test/test_event_queue: test/test_event_queue.c src/core/event.c src/core/event.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_event_queue.c src/core/event.c

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
		src/tom/blitter_internal.h src/tom/blitter.h src/core/vjag_memory.h
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/test_blitter_mmio.c src/tom/blitter_mmio.c

test/test_blitter_simd: test/test_blitter_simd.c $(BLITTER_SIMD_SRC) src/tom/blitter_simd.h
	$(CC) $(CFLAGS) -o $@ test/test_blitter_simd.c $(BLITTER_SIMD_SRC)

test/test_dsp_mac40: test/test_dsp_mac40.c src/jerry/dsp_acc40.h
	$(CC) -O2 -Wall $(INCFLAGS) -o $@ test/test_dsp_mac40.c

test/test_m68k_ops: test/test_m68k_ops.c
	$(CC) -O2 -Wall -Wno-unused-function -std=c99 $(INCFLAGS) \
		-o $@ test/test_m68k_ops.c -ldl

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

test/tools/test_memory_map: test/tools/test_memory_map.c
	$(CC) -O2 -Wall -std=c99 $(INCFLAGS) \
		-o $@ test/tools/test_memory_map.c -ldl
endif

.PHONY: clean test lint coverage benchmark acid
endif

lint:
	@scripts/c89-lint.sh

# `make coverage` -- builds with gcov instrumentation, runs the full
# test suite, and produces a Cobertura XML report at coverage.xml plus
# a textual summary.  See gcovr.cfg for path filters.
coverage:
	$(MAKE) clean
	$(MAKE) COVERAGE=1 TEST_EXPORTS=1 -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
	$(MAKE) COVERAGE=1 TEST_EXPORTS=1 test
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

print-%:
	@echo '$*=$($*)'

