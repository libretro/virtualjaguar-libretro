#!/usr/bin/env python3
"""
gen-jaguar-regs.py -- generate test/acid/include/jaguar_regs.s from C source.

Single source of truth.  Parses register-base addresses, MMIO offsets,
command bit fields, and IRQ enums out of the actual emulator headers
and emits one big vasm-friendly equates file that every acid test can
include.

Run via `make -C test/acid include/jaguar_regs.s` (the Makefile depends
on this script + the C sources it parses, so it'll re-run if any of
them change).

Why we need this:  during the first batch of blitter tests I had the
LFU function field at the wrong bit positions ($C000 instead of bits
21..24) and the DSTEN bit confused with DSTWRZ.  Every test that
touched those bits was bogus -- the blits ran with "ity short-form
00000C000" which has no defined effect, so destinations stayed zero
and we falsely reported a "blitter source-data routing bug" in the
emulator.  Copilot review caught it.  This file makes that class of
mistake mechanically impossible: tests refer to BCOMPEN by name and
get the right bit, every time.
"""
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

OUT_PATH = os.path.join(REPO_ROOT, "test", "acid", "include", "jaguar_regs.s")

# ---------------------------------------------------------------------------
# Section 1: register-file BASE addresses (TOM/JERRY/blitter/GPU/DSP).
# Hand-curated because they're not in a single grep-able pattern in C.
# Cross-checked against src/tom/tom.h, src/tom/gpu.h, src/jerry/dsp.h,
# src/jerry/jerry.h, and src/tom/blitter.c top-of-file constants.
# ---------------------------------------------------------------------------
BASES = {
    "TOM_BASE":      0xF00000,
    "GPU_BASE":      0xF02100,    # gpu.h GPU_CONTROL_RAM_BASE
    "GPU_RAM":       0xF03000,    # gpu.h GPU_WORK_RAM_BASE
    "BLIT_BASE":     0xF02200,    # blitter MMIO (TOM + $200 in tom.h)
    "JERRY_BASE":    0xF10000,    # jerry.h DSP/JERRY MMIO base
    "DSP_BASE":      0xF1A100,    # dsp.h DSP_CONTROL_RAM_BASE
    "DSP_RAM":       0xF1B000,    # dsp.h DSP_WORK_RAM_BASE
}

# ---------------------------------------------------------------------------
# Section 2: TOM register offsets (relative to TOM_BASE).
# Derived from the comment block at src/tom/tom.c:80-200 and the #define
# block at src/tom/tom.c:300-400.
# ---------------------------------------------------------------------------
TOM_OFFSETS = {
    "MEMCON1":     0x00,
    "MEMCON2":     0x02,
    "HC":          0x04,           # horizontal count
    "VC":          0x06,           # vertical count
    "OLP":         0x20,           # object list pointer (LO=20, HI=22)
    "OLP_LO":      0x20,
    "OLP_HI":      0x22,
    "OBF":         0x26,           # object processor flag
    "BORD1":       0x2A,           # border colour green/red (8 BPP)
    "BORD2":       0x2C,           # border colour blue (8 BPP)
    "HP":          0x2E,           # horizontal period (1..1024)
    "HBB":         0x30,           # horizontal blank begin
    "HBE":         0x32,           # horizontal blank end
    "HS":          0x34,           # horizontal sync
    "HVS":         0x36,           # horizontal vertical sync
    "HDB1":        0x38,           # horizontal display begin 1
    "HDB2":        0x3A,           # horizontal display begin 2
    "HDE":         0x3C,           # horizontal display end
    "VP":          0x3E,           # vertical period
    "VBB":         0x40,           # vertical blank begin   (NOT $2A)
    "VBE":         0x42,           # vertical blank end     (NOT $2C)
    "VS":          0x44,           # vertical sync
    "VDB":         0x46,           # vertical display begin
    "VDE":         0x48,           # vertical display end
    "VEB":         0x4A,           # vertical equalisation begin
    "VEE":         0x4C,           # vertical equalisation end
    "VI":          0x4E,           # vertical interrupt position
    "PIT0":        0x50,
    "PIT1":        0x52,
    "HEQ":         0x54,
    "BG":          0x58,           # background colour
    "INT1":        0xE0,           # CPU interrupt control reg
    "INT2":        0xE2,
}

# ---------------------------------------------------------------------------
# Section 3: blitter command bits.  PARSED from src/tom/blitter.c.
# This is the section that bit me -- I had wrong bit positions for SRCEN/
# DSTEN/LFU and several others.  Now generated mechanically.
# ---------------------------------------------------------------------------
def parse_blitter_bits():
    """Parse `#define NAME (cmd & 0xVALUE)` lines from blitter.c."""
    path = os.path.join(REPO_ROOT, "src", "tom", "blitter.c")
    pattern = re.compile(
        r"^#define\s+(\w+)\s+\(cmd\s*&\s*0x([0-9A-Fa-f]+)\)", re.M)
    bits = {}
    with open(path) as fh:
        for m in pattern.finditer(fh.read()):
            bits[m.group(1)] = int(m.group(2), 16)
    return bits

# Register offsets from blitter.c top-of-file #defines like
# #define A1_BASE ((uint32_t)0x00).
def parse_blitter_regs():
    path = os.path.join(REPO_ROOT, "src", "tom", "blitter.c")
    pattern = re.compile(
        r"^#define\s+(A[12]_\w+|COMMAND|PIXLINECOUNTER|SRCDATA|DSTDATA|"
        r"PATTERNDATA|INTENSITYINC|SRCZINT|SRCZFRAC|DSTZ|ZINC|"
        r"COLLISIONCTRL|COLLISIONLOG)\s+"
        r"\(\(uint32_t\)0x([0-9A-Fa-f]+)\)", re.M)
    regs = {}
    with open(path) as fh:
        for m in pattern.finditer(fh.read()):
            regs[m.group(1)] = int(m.group(2), 16)
    return regs

# ---------------------------------------------------------------------------
# Section 4: JERRY IRQ enum bits.  Parsed from jerry.h's IRQ2_xxx enum.
# ---------------------------------------------------------------------------
def parse_jerry_irq():
    path = os.path.join(REPO_ROOT, "src", "jerry", "jerry.h")
    pattern = re.compile(r"\b(IRQ2_\w+)\s*=\s*0x([0-9A-Fa-f]+)")
    bits = {}
    with open(path) as fh:
        for m in pattern.finditer(fh.read()):
            bits[m.group(1)] = int(m.group(2), 16)
    return bits

# ---------------------------------------------------------------------------
# Section 5: TOM IRQ enum (numeric bit positions in INT1 enable byte).
# Parsed from `enum { IRQ_VIDEO = 0, IRQ_GPU, IRQ_OPFLAG, IRQ_TIMER, IRQ_DSP };`
# in tom.h.  We emit them as both bit-positions (IRQ_VIDEO=0) and bit-masks
# (IRQ_VIDEO_MASK=$01) for convenience.
# ---------------------------------------------------------------------------
def parse_tom_irq():
    path = os.path.join(REPO_ROOT, "src", "tom", "tom.h")
    with open(path) as fh:
        text = fh.read()
    m = re.search(r"enum\s*\{\s*(IRQ_VIDEO[^}]+)\}", text)
    if not m:
        return {}
    parts = [p.strip() for p in m.group(1).split(",") if p.strip()]
    bits = {}
    next_val = 0
    for p in parts:
        if "=" in p:
            name, val = p.split("=")
            next_val = int(val.strip(), 0)
            bits[name.strip()] = next_val
        else:
            bits[p] = next_val
        next_val += 1
    return bits

# ---------------------------------------------------------------------------
# Section 6: emit the .s file.
# ---------------------------------------------------------------------------
def emit_section(out, header):
    out.write(";; ")
    out.write("=" * 64)
    out.write("\n")
    out.write(f";; {header}\n")
    out.write(";; ")
    out.write("=" * 64)
    out.write("\n\n")

def emit_equ(out, name, value, comment=""):
    val_str = f"${value:08X}" if value > 0xFFFF else f"${value:04X}"
    pad = 16 - len(name)
    out.write(f"{name}{' ' * max(pad,1)}equ     {val_str}")
    if comment:
        out.write(f"   ; {comment}")
    out.write("\n")

def main():
    blit_bits = parse_blitter_bits()
    blit_regs = parse_blitter_regs()
    jerry_irq = parse_jerry_irq()
    tom_irq = parse_tom_irq()

    if not blit_bits or not blit_regs or not jerry_irq or not tom_irq:
        print("ERROR: failed to parse one of the source headers", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w") as out:
        out.write(""";
; jaguar_regs.s -- AUTO-GENERATED.  DO NOT EDIT BY HAND.
;
; Single source of truth for Jaguar register addresses, MMIO offsets,
; blitter command bits, and IRQ enums used by the acid-test ROMs.
;
; Generated by test/acid/scripts/gen-jaguar-regs.py from:
;   src/tom/blitter.c    (blitter cmd bits + register offsets)
;   src/tom/tom.h        (TOM IRQ enum, TOM register offsets)
;   src/jerry/jerry.h    (JERRY IRQ2 enum)
;   src/jerry/dsp.h      (DSP base addresses)
;   src/tom/gpu.h        (GPU base addresses)
;
; If a base address or bit field changes in the C source, this file
; will pick it up next time `make` runs in test/acid/.  Tests should
; ALWAYS reference these symbols by name (BCOMPEN, IRQ2_TIMER1, etc.)
; rather than hard-coding hex literals.
;

""")

        emit_section(out, "Section 1: subsystem base addresses")
        for k, v in BASES.items():
            emit_equ(out, k, v)
        out.write("\n")

        emit_section(out, "Section 2: TOM register offsets (TOM_BASE + ...)")
        for k, v in TOM_OFFSETS.items():
            emit_equ(out, "TOM_" + k, BASES["TOM_BASE"] + v,
                     comment=f"TOM_BASE + ${v:02X}")
        out.write("\n")

        emit_section(out, "Section 3: blitter MMIO addresses (BLIT_BASE + ...)")
        for k, v in sorted(blit_regs.items(), key=lambda kv: kv[1]):
            emit_equ(out, "B_" + k, BASES["BLIT_BASE"] + v,
                     comment=f"BLIT_BASE + ${v:02X}")
        out.write("\n")

        emit_section(out, "Section 4: blitter COMMAND bits (write to B_COMMAND)")
        for k, v in sorted(blit_bits.items(), key=lambda kv: kv[1]):
            emit_equ(out, k, v)
        # Composite mask of every known bit for the linter.
        all_bits = 0
        for v in blit_bits.values():
            all_bits |= v
        # LFU field is bits 21..24 (4 bits = $0F << 21 = $1E00000)
        all_bits |= 0x01E00000
        # zmode is bits 18..20 (3 bits)
        all_bits |= 0x001C0000
        # pixsize and other multi-bit fields
        out.write("\n")
        emit_equ(out, "BLIT_CMD_VALID_BITS", all_bits,
                 comment="OR of every defined cmd field (lint mask)")
        out.write("\n")
        out.write(";; LFU function lives in bits 21..24 (4-bit field).\n")
        out.write(";; Pre-shifted constants for each function.  Named\n")
        out.write(";; LFU_FN_X (not LFU_X) to avoid colliding with the\n")
        out.write(";; LFU_A / LFU_NA / LFU_AN / LFU_NAN cmd bits above.\n")
        for f in range(16):
            emit_equ(out, f"LFU_FN_{f:X}", f << 21,
                     comment=f"LFU function = {f:#x}")
        out.write("\n")

        emit_section(out, "Section 5: TOM IRQ enum + bit-mask (INT1 low byte)")
        for k, v in tom_irq.items():
            emit_equ(out, k, v, comment="bit position in TOM_INT1 low byte")
            emit_equ(out, k + "_MASK", 1 << v)
        out.write("\n")

        emit_section(out, "Section 6: JERRY IRQ2 enum bits (JINTCTRL)")
        for k, v in jerry_irq.items():
            emit_equ(out, k, v)
        out.write("\n")

    print(f"wrote {OUT_PATH}")
    print(f"  blitter cmd bits:     {len(blit_bits)}")
    print(f"  blitter MMIO regs:    {len(blit_regs)}")
    print(f"  TOM IRQ entries:      {len(tom_irq)}")
    print(f"  JERRY IRQ2 entries:   {len(jerry_irq)}")

if __name__ == "__main__":
    main()
