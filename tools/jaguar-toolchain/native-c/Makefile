CROSS ?= m68k-elf-
PYTHON ?= python3
VJ ?= virtualjaguar
BUILD ?= ../../../build/jaguar-native-c

.DEFAULT_GOAL := all
.PHONY: all run smoke
.DELETE_ON_ERROR:

all: $(BUILD)/badge.abs

$(BUILD):
	mkdir -p "$@"

$(BUILD)/start.o: start.S | $(BUILD)
	$(CROSS)gcc -m68000 -c "$<" -o "$@"

$(BUILD)/badge.o: badge.c badge.inc | $(BUILD)
	$(CROSS)gcc -m68000 -std=c89 -pedantic -Wall -Wextra -Os -ffreestanding \
		-fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
		-c "$<" -o "$@"

$(BUILD)/badge.elf: $(BUILD)/start.o $(BUILD)/badge.o jaguar.ld Makefile
	$(CROSS)ld --build-id=none -z noexecstack -T jaguar.ld -o "$@" $(filter %.o,$^)

$(BUILD)/badge.bin: $(BUILD)/badge.elf
	$(CROSS)objcopy -O binary -j .image "$<" "$@"

$(BUILD)/badge.abs: $(BUILD)/badge.bin $(BUILD)/badge.elf pack-abs.py
	$(PYTHON) pack-abs.py "$(CROSS)nm" "$(BUILD)/badge.elf" "$<" "$@"

run: all
	"$(VJ)" "$(BUILD)/badge.abs"

smoke: all
	"$(VJ)" --headless --emu-thread off --frames 60 --break drawmark \
		--data-dir "$(abspath $(BUILD))/smoke-data" --expect-hash e6c98081 \
		--dump-frame "$(BUILD)/badge-frame" "$(BUILD)/badge.abs" \
		> "$(BUILD)/smoke.log" 2>&1 || { cat "$(BUILD)/smoke.log"; exit 1; }
	cat "$(BUILD)/smoke.log"
	grep -q '^\[debug\] halt: 68000 .* (breakpoint)' "$(BUILD)/smoke.log"
