
DEBUG = 0

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
endif
endif

ifeq ($(platform), unix)
   TARGET := libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS +=
else ifeq ($(platform), osx)
   TARGET := libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS +=
else
   TARGET := retro.dll
   CC = gcc
   CXX = g++
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS +=
endif


VIRTUALJAGUAR_DIR := ..
SOURCE_DIR := $(VIRTUALJAGUAR_DIR)/src

SOURCES_CPP := $(SOURCE_DIR)/blitter.cpp \
	$(SOURCE_DIR)/cdintf.cpp \
	$(SOURCE_DIR)/cdrom.cpp \
	$(SOURCE_DIR)/crc32.cpp \
	$(SOURCE_DIR)/dac.cpp \
	$(SOURCE_DIR)/dsp.cpp \
	$(SOURCE_DIR)/eeprom.cpp \
	$(SOURCE_DIR)/event.cpp \
	$(SOURCE_DIR)/file.cpp \
	$(SOURCE_DIR)/filedb.cpp \
	$(SOURCE_DIR)/gpu.cpp \
	$(SOURCE_DIR)/jagbios.cpp \
	$(SOURCE_DIR)/jagcdbios.cpp \
	$(SOURCE_DIR)/jagdasm.cpp \
	$(SOURCE_DIR)/jagdevcdbios.cpp \
	$(SOURCE_DIR)/jagstub1bios.cpp \
	$(SOURCE_DIR)/jagstub2bios.cpp \
	$(SOURCE_DIR)/jaguar.cpp \
	$(SOURCE_DIR)/jerry.cpp \
	$(SOURCE_DIR)/joystick.cpp \
	$(SOURCE_DIR)/log.cpp \
	$(SOURCE_DIR)/memory.cpp \
	$(SOURCE_DIR)/mmu.cpp \
	$(SOURCE_DIR)/op.cpp \
	$(SOURCE_DIR)/settings.cpp \
	$(SOURCE_DIR)/state.cpp \
	$(SOURCE_DIR)/tom.cpp \
	$(SOURCE_DIR)/universalhdr.cpp \
	$(SOURCE_DIR)/unzip.cpp \
	$(SOURCE_DIR)/virtualjaguar.cpp \
	$(SOURCE_DIR)/wavetable.cpp

LIBRETRO_SOURCES := libretro.cpp

M68K_SOURCES := $(SOURCE_DIR)/m68000/cpustbl.c \
	$(SOURCE_DIR)/m68000/cpudefs.c \
	$(SOURCE_DIR)/m68000/cpuemu.c \
	$(SOURCE_DIR)/m68000/cpuextra.c \
	$(SOURCE_DIR)/m68000/gencpu.c \
	$(SOURCE_DIR)/m68000/m68kdasm.c \
	$(SOURCE_DIR)/m68000/m68kinterface.c \
	$(SOURCE_DIR)/m68000/readcpu.c

SOURCES := $(LIBRETRO_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_CPP:.cpp=.o) $(M68K_SOURCES:.c=.o)

all: $(TARGET)

ifeq ($(DEBUG), 0)
   FLAGS += -O3 -ffast-math -fomit-frame-pointer
else
   FLAGS += -O0 -g
endif

LDFLAGS += $(fpic) -lz $(SHARED)
FLAGS += $(fpic)
FLAGS += -I. -I../src -I../src/m68000

WARNINGS := -Wall \
	-Wno-narrowing \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	-Wno-unused-result \
	-Wno-strict-aliasing \
	-Wno-overflow

FLAGS += -D__LIBRETRO__ $(ENDIANNESS_DEFINES) $(WARNINGS)

CXXFLAGS += $(FLAGS)
CFLAGS += $(FLAGS) -std=gnu99

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
