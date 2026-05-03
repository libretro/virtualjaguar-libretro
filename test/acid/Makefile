#
# test/acid/Makefile - assembles the synthetic acid-test ROMs.
#
# Toolchain: vasm (motorola syntax) + vlink.  Get them from
# http://sun.hasenbraten.de/vasm/ and http://sun.hasenbraten.de/vlink/.
# Build vasm with `make CPU=m68k SYNTAX=mot`.
#
# If `vasmm68k_mot` is not on $PATH this Makefile prints a one-line
# warning and skips the assemble step entirely.
#

SRCDIR     := tests
INCDIR     := include
RUNNER_BIN := acid_run

VASM       ?= vasmm68k_mot

VASM_FLAGS := -Fbin -m68000 -spaces -I$(INCDIR)

SOURCES    := $(shell find $(SRCDIR) -name '*.s' -type f 2>/dev/null)
ROMS       := $(SOURCES:.s=.jag)

VASM_PRESENT := $(shell command -v $(VASM) 2>/dev/null)

ifeq ($(VASM_PRESENT),)
ROMS_TO_BUILD :=
else
ROMS_TO_BUILD := $(ROMS)
endif

.PHONY: all clean check-vasm test lint regs baseline check-baseline

# Regenerate the auto-derived include/jaguar_regs.s.  It depends on the
# C source files it parses; if any of them change, the .s file rebuilds.
REGS_OUT := include/jaguar_regs.s
REGS_SRC := \
   ../../src/tom/blitter.c \
   ../../src/tom/tom.h \
   ../../src/tom/gpu.h \
   ../../src/jerry/jerry.h \
   ../../src/jerry/dsp.h \
   scripts/gen-jaguar-regs.py

$(REGS_OUT): $(REGS_SRC)
	@python3 scripts/gen-jaguar-regs.py

regs: $(REGS_OUT)

# Lint test sources for blitter cmd-bit + hard-coded address mistakes.
# Catches the kind of bug Copilot review caught the first time round
# (wrong LFU position, wrong DSTEN bit, wrong PIT setup address).
lint: $(REGS_OUT)
	@python3 scripts/lint-acid.py

all: $(REGS_OUT) $(RUNNER_BIN) $(ROMS_TO_BUILD)
	@$(MAKE) -s check-vasm
	@$(MAKE) -s lint || true

check-vasm:
ifeq ($(VASM_PRESENT),)
	@echo "** $(VASM) not found on PATH"
	@echo "** Skipped assembling acid-test ROMs."
	@echo "** See test/acid/README.md for vasm install instructions."
endif

# .s -> .jag: assemble flat binary at the program's org address ($800000),
# then pad to 1 MB so retro_load_game sees a normal-sized cart.
%.jag: %.s
	@mkdir -p $(dir $@)
	$(VASM) $(VASM_FLAGS) -o $@ $<
	@actual=$$(wc -c < $@); \
	target=1048576; \
	if [ $$actual -lt $$target ]; then \
	   dd if=/dev/zero bs=1 count=$$(($$target - $$actual)) >> $@ 2>/dev/null; \
	fi
	@echo "  ASM   $< -> $@ ($$(wc -c < $@) bytes)"

clean:
	rm -f $(ROMS) $(RUNNER_BIN)

# Build the harness (separate from the .jag ROMs themselves).
$(RUNNER_BIN): run.c
	$(CC) -O2 -Wall -std=c99 \
	   -I../../libretro-common/include \
	   -o $@ $< \
	   $(if $(filter Linux,$(shell uname -s)),-ldl)

# Run all built tests through the harness.  CORE points at the libretro
# core .dylib/.so (defaults to the project root build).
CORE ?= $(firstword $(wildcard ../../virtualjaguar_libretro.dylib ../../virtualjaguar_libretro.so))

test: all
	@if [ -z "$(CORE)" ]; then \
	   echo "ERROR: set CORE=path/to/virtualjaguar_libretro.{dylib,so}"; \
	   exit 2; \
	fi
	@if [ -z "$(ROMS_TO_BUILD)" ]; then \
	   echo "Nothing to run (no .jag ROMs assembled)."; \
	   exit 0; \
	fi
	@fail=0; total=0; \
	for rom in $(ROMS_TO_BUILD); do \
	   total=$$((total+1)); \
	   if ! ./$(RUNNER_BIN) "$(CORE)" "$$rom"; then fail=$$((fail+1)); fi; \
	done; \
	echo "----"; \
	echo "Acid tests: $$((total-fail)) / $$total passed"; \
	exit $$fail

# `make baseline` -- regenerate test/acid/BASELINE.txt from a fresh
# run.  Run this when (a) you add new tests that legitimately FAIL and
# want them recorded as known FAILs, OR (b) you fix an emulator bug
# and a test moves FAIL -> PASS (the next CI will then catch any
# regression that brings it back to FAIL).
baseline: all
	@if [ -z "$(CORE)" ]; then \
	   echo "ERROR: set CORE=path/to/virtualjaguar_libretro.{dylib,so}"; \
	   exit 2; \
	fi
	@$(MAKE) -s test CORE=$(CORE) > /tmp/acid_baseline.log 2>&1 || true
	@grep -E '^\[(PASS|FAIL|NOT-RUN-YET)' /tmp/acid_baseline.log \
	   | awk '{print $$1, $$3}' | sort > BASELINE.txt
	@wc -l BASELINE.txt
	@echo "Updated BASELINE.txt -- review the diff before committing."

# `make check-baseline` -- run the suite and compare against BASELINE.txt.
# Exits non-zero if any previously-PASSing test now FAILs.  Used by CI.
check-baseline: all
	@if [ -z "$(CORE)" ]; then \
	   echo "ERROR: set CORE=path/to/virtualjaguar_libretro.{dylib,so}"; \
	   exit 2; \
	fi
	@$(MAKE) -s test CORE=$(CORE) > /tmp/acid_run.log 2>&1 || true
	@python3 scripts/check-baseline.py /tmp/acid_run.log BASELINE.txt
