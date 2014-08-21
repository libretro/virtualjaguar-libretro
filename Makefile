DEBUG = 0

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

ifeq ($(platform), unix)
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
else ifeq ($(platform), osx)
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib

ifeq ($(arch),ppc)
	FLAGS += -DMSB_FIRST
	OLD_GCC = 1
endif
OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   fpic += -mmacosx-version-min=10.5
endif
else ifeq ($(platform), ios)
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib

   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   SHARED += -miphoneos-version-min=5.0
   CC +=  -miphoneos-version-min=5.0
   CXX +=  -miphoneos-version-min=5.0
endif
else ifeq ($(platform), qnx)
   TARGET := $(TARGET_NAME)_libretro_qnx.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
	CC = qcc -Vgcc_ntoarmv7le
	CXX = QCC -Vgcc_ntoarmv7le_cpp
else ifeq ($(platform), ps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   STATIC_LINKING = 1
	FLAGS += -DMSB_FIRST
	OLD_GCC = 1
else ifeq ($(platform), sncps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   STATIC_LINKING = 1
	FLAGS += -DMSB_FIRST
	NO_GCC = 1
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_psp1.a
	CC = psp-gcc$(EXE_EXT)
	CXX = psp-g++$(EXE_EXT)
	AR = psp-ar$(EXE_EXT)
   STATIC_LINKING = 1
	FLAGS += -G0 -DLSB_FIRST
else
   TARGET := $(TARGET_NAME)_libretro.dll
   CC = gcc
   CXX = g++
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
endif
VIRTUALJAGUAR_DIR := src

VIRTUALJAGUAR_SOURCES := $(VIRTUALJAGUAR_DIR)/blitter.cpp \
	$(VIRTUALJAGUAR_DIR)/cdintf.cpp \
	$(VIRTUALJAGUAR_DIR)/cdrom.cpp \
	$(VIRTUALJAGUAR_DIR)/crc32.cpp \
	$(VIRTUALJAGUAR_DIR)/dac.cpp \
	$(VIRTUALJAGUAR_DIR)/dsp.cpp \
	$(VIRTUALJAGUAR_DIR)/eeprom.cpp \
	$(VIRTUALJAGUAR_DIR)/event.cpp \
	$(VIRTUALJAGUAR_DIR)/file.cpp \
	$(VIRTUALJAGUAR_DIR)/filedb.cpp \
	$(VIRTUALJAGUAR_DIR)/gpu.cpp \
	$(VIRTUALJAGUAR_DIR)/jagbios.cpp \
	$(VIRTUALJAGUAR_DIR)/jagbios2.cpp \
	$(VIRTUALJAGUAR_DIR)/jagcdbios.cpp \
	$(VIRTUALJAGUAR_DIR)/jagdasm.cpp \
	$(VIRTUALJAGUAR_DIR)/jagdevcdbios.cpp \
	$(VIRTUALJAGUAR_DIR)/jagstub1bios.cpp \
	$(VIRTUALJAGUAR_DIR)/jagstub2bios.cpp \
	$(VIRTUALJAGUAR_DIR)/jaguar.cpp \
	$(VIRTUALJAGUAR_DIR)/jerry.cpp \
	$(VIRTUALJAGUAR_DIR)/joystick.cpp \
	$(VIRTUALJAGUAR_DIR)/log.cpp \
	$(VIRTUALJAGUAR_DIR)/memory.cpp \
	$(VIRTUALJAGUAR_DIR)/mmu.cpp \
	$(VIRTUALJAGUAR_DIR)/op.cpp \
	$(VIRTUALJAGUAR_DIR)/settings.cpp \
	$(VIRTUALJAGUAR_DIR)/state.cpp \
	$(VIRTUALJAGUAR_DIR)/tom.cpp \
	$(VIRTUALJAGUAR_DIR)/universalhdr.cpp \
	$(VIRTUALJAGUAR_DIR)/wavetable.cpp

LIBRETRO_SOURCES := libretro.cpp

SOURCES_C := $(VIRTUALJAGUAR_DIR)/m68000/cpustbl.c \
	$(VIRTUALJAGUAR_DIR)/m68000/cpudefs.c \
	$(VIRTUALJAGUAR_DIR)/m68000/cpuemu.c \
	$(VIRTUALJAGUAR_DIR)/m68000/cpuextra.c \
	$(VIRTUALJAGUAR_DIR)/m68000/gencpu.c \
	$(VIRTUALJAGUAR_DIR)/m68000/m68kdasm.c \
	$(VIRTUALJAGUAR_DIR)/m68000/m68kinterface.c \
	$(VIRTUALJAGUAR_DIR)/m68000/readcpu.c

SOURCES := $(LIBRETRO_SOURCES) $(VIRTUALJAGUAR_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

ifeq ($(DEBUG),1)
FLAGS += -O0 -g
else
FLAGS += -O3 -ffast-math -fomit-frame-pointer -DNDEBUG
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS += $(fpic) 
FLAGS += -I. -Isrc -Isrc/m68000

ifeq ($(OLD_GCC), 1)
WARNINGS := -Wall
else ifeq ($(NO_GCC), 1)
WARNINGS :=
else
WARNINGS := -Wall \
	-Wno-narrowing \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	-Wno-unused-result \
	-Wno-strict-aliasing \
	-Wno-overflow \
	-fno-strict-overflow
endif

FLAGS += -D__LIBRETRO__ $(WARNINGS)

CXXFLAGS += $(FLAGS) -D__GCCUNIX__
CFLAGS += $(FLAGS)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $^ $(LDFLAGS)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
