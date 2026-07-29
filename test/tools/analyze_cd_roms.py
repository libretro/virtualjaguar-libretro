#!/usr/bin/env python3
"""
Analyze Jaguar CD bypass/override ROMs for BIOS jump table calling conventions.

Disassembles 68K big-endian binaries and finds references to the BIOS jump table
at $3000. Focuses on understanding how CD bypass programs call CD_read ($303C)
and other BIOS functions.
"""

import struct
import sys
import os
from collections import defaultdict

# ── BIOS Jump Table Addresses ──────────────────────────────────────────────
BIOS_FUNCTIONS = {
    0x3006: "CD_init",
    0x300C: "CD_ack",
    0x3012: "CD_jeri",
    0x301E: "CD_stop",
    0x303C: "CD_read",
    0x3042: "CD_reset",
    0x3048: "CD_setup / CD_mode",
    0x304E: "CD_poll",
    0x305A: "CD_osamp",
    0x3060: "GPU_ISR_setup",
}

# Extend with more known BIOS entry points seen in code
BIOS_RANGE = range(0x3000, 0x3E00)

# ── 68K Instruction Patterns ──────────────────────────────────────────────

# Register names
DREGS = ["D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"]
AREGS = ["A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7/SP"]

def decode_ea_mode(mode, reg):
    """Decode 68K effective address mode/register fields."""
    if mode == 0: return f"D{reg}"
    if mode == 1: return f"A{reg}"
    if mode == 2: return f"(A{reg})"
    if mode == 3: return f"(A{reg})+"
    if mode == 4: return f"-(A{reg})"
    if mode == 5: return f"d16(A{reg})"
    if mode == 6: return f"d8(A{reg},Xn)"
    if mode == 7:
        if reg == 0: return "abs.w"
        if reg == 1: return "abs.l"
        if reg == 2: return "d16(PC)"
        if reg == 3: return "d8(PC,Xn)"
        if reg == 4: return "#imm"
    return f"?{mode}/{reg}"


class M68KDisassembler:
    """Minimal 68K disassembler focused on the instructions we care about."""

    def __init__(self, data, base_addr=0):
        self.data = data
        self.base = base_addr

    def read16(self, offset):
        if offset + 2 > len(self.data):
            return None
        return struct.unpack(">H", self.data[offset:offset+2])[0]

    def read32(self, offset):
        if offset + 4 > len(self.data):
            return None
        return struct.unpack(">I", self.data[offset:offset+4])[0]

    def disasm_one(self, offset):
        """Disassemble one instruction at offset. Returns (text, size, info_dict)."""
        w = self.read16(offset)
        if w is None:
            return None, 0, {}

        info = {}

        # ── JSR ────────────────────────────────────────────────────────
        # 4E80-4EBF: JSR <ea>
        if (w & 0xFFC0) == 0x4E80:
            mode = (w >> 3) & 7
            reg = w & 7
            if mode == 7 and reg == 0:  # JSR (abs.w)
                addr = self.read16(offset + 2)
                if addr is not None:
                    # Sign-extend 16-bit to 32-bit
                    if addr & 0x8000:
                        addr32 = addr | 0xFFFF0000
                    else:
                        addr32 = addr
                    name = BIOS_FUNCTIONS.get(addr, "")
                    info = {"type": "JSR", "target": addr, "target32": addr32, "name": name}
                    return f"JSR ($%04X).w  ; {name}" % addr, 4, info
            elif mode == 7 and reg == 1:  # JSR (abs.l)
                addr = self.read32(offset + 2)
                if addr is not None:
                    name = BIOS_FUNCTIONS.get(addr & 0xFFFFFF, "")
                    info = {"type": "JSR", "target": addr, "name": name}
                    return f"JSR ($%08X).l  ; {name}" % addr, 6, info
            elif mode == 2:  # JSR (An)
                info = {"type": "JSR", "target": f"(A{reg})"}
                return f"JSR (A{reg})", 2, info
            elif mode == 5:  # JSR d16(An)
                d16 = self.read16(offset + 2)
                if d16 is not None:
                    if d16 & 0x8000:
                        d16s = d16 - 0x10000
                    else:
                        d16s = d16
                    info = {"type": "JSR", "target": f"{d16s}(A{reg})"}
                    return f"JSR {d16s}(A{reg})", 4, info
            else:
                ea = decode_ea_mode(mode, reg)
                return f"JSR {ea}", 2 if mode < 5 else 4, info

        # ── JMP ────────────────────────────────────────────────────────
        if (w & 0xFFC0) == 0x4EC0:
            mode = (w >> 3) & 7
            reg = w & 7
            if mode == 7 and reg == 0:  # JMP (abs.w)
                addr = self.read16(offset + 2)
                if addr is not None:
                    name = BIOS_FUNCTIONS.get(addr, "")
                    info = {"type": "JMP", "target": addr, "name": name}
                    return f"JMP ($%04X).w  ; {name}" % addr, 4, info
            elif mode == 7 and reg == 1:  # JMP (abs.l)
                addr = self.read32(offset + 2)
                if addr is not None:
                    name = BIOS_FUNCTIONS.get(addr & 0xFFFFFF, "")
                    info = {"type": "JMP", "target": addr, "name": name}
                    return f"JMP ($%08X).l  ; {name}" % addr, 6, info
            elif mode == 2:  # JMP (An)
                info = {"type": "JMP", "target": f"(A{reg})"}
                return f"JMP (A{reg})", 2, info
            else:
                ea = decode_ea_mode(mode, reg)
                return f"JMP {ea}", 2 if mode < 5 else 4, info

        # ── BSR ────────────────────────────────────────────────────────
        if (w & 0xFF00) == 0x6100:
            disp = w & 0xFF
            if disp == 0:
                d16 = self.read16(offset + 2)
                if d16 is not None:
                    if d16 & 0x8000:
                        d16 -= 0x10000
                    target = (self.base + offset + 2 + d16) & 0xFFFFFFFF
                    info = {"type": "BSR", "target": target}
                    return f"BSR.W $%06X" % target, 4, info
            else:
                if disp & 0x80:
                    disp -= 0x100
                target = (self.base + offset + 2 + disp) & 0xFFFFFFFF
                info = {"type": "BSR", "target": target}
                return f"BSR.B $%06X" % target, 2, info

        # ── MOVEQ ─────────────────────────────────────────────────────
        if (w & 0xF100) == 0x7000:
            dreg = (w >> 9) & 7
            imm = w & 0xFF
            if imm & 0x80:
                imm -= 0x100
            info = {"type": "MOVEQ", "reg": f"D{dreg}", "value": imm & 0xFF}
            return f"MOVEQ #$%02X,D{dreg}" % (imm & 0xFF), 2, info

        # ── MOVE.L #imm,Dn (or An) ────────────────────────────────────
        # 2x3C = MOVE.L #imm,Dn  (opcode 0010 xxx0 0011 1100)
        if (w & 0xF1FF) == 0x203C:
            dreg = (w >> 9) & 7
            imm = self.read32(offset + 2)
            if imm is not None:
                info = {"type": "MOVE.L_IMM", "reg": f"D{dreg}", "value": imm}
                return f"MOVE.L #$%08X,D{dreg}" % imm, 6, info

        # ── MOVE.W #imm (to various) ──────────────────────────────────
        # 303C = MOVE.W #imm,D0 (etc)
        if (w & 0xF1FF) == 0x303C:
            dreg = (w >> 9) & 7
            imm = self.read16(offset + 2)
            if imm is not None:
                info = {"type": "MOVE.W_IMM", "reg": f"D{dreg}", "value": imm}
                return f"MOVE.W #$%04X,D{dreg}" % imm, 4, info

        # ── MOVE.B #imm ───────────────────────────────────────────────
        if (w & 0xF1FF) == 0x103C:
            dreg = (w >> 9) & 7
            imm = self.read16(offset + 2)
            if imm is not None:
                info = {"type": "MOVE.B_IMM", "reg": f"D{dreg}", "value": imm & 0xFF}
                return f"MOVE.B #$%02X,D{dreg}" % (imm & 0xFF), 4, info

        # ── LEA addr,An ───────────────────────────────────────────────
        # LEA (abs.l),An = 41F9/43F9/45F9/47F9/49F9/4BF9/4DF9/4FF9
        if (w & 0xF1FF) == 0x41F9:
            areg = (w >> 9) & 7
            addr = self.read32(offset + 2)
            if addr is not None:
                info = {"type": "LEA", "reg": f"A{areg}", "value": addr}
                return f"LEA ($%08X).l,A{areg}" % addr, 6, info

        # LEA (abs.w),An = 41F8/43F8/45F8/47F8/49F8/4BF8/4DF8/4FF8
        if (w & 0xF1FF) == 0x41F8:
            areg = (w >> 9) & 7
            addr = self.read16(offset + 2)
            if addr is not None:
                if addr & 0x8000:
                    addr32 = addr | 0xFFFF0000
                else:
                    addr32 = addr
                info = {"type": "LEA_W", "reg": f"A{areg}", "value": addr32}
                return f"LEA ($%04X).w,A{areg}" % addr, 4, info

        # ── MOVEA.L ────────────────────────────────────────────────────
        # 207C = MOVEA.L #imm,A0; 227C = A1; etc.
        if (w & 0xF1FF) == 0x207C:
            areg = (w >> 9) & 7
            imm = self.read32(offset + 2)
            if imm is not None:
                info = {"type": "MOVEA.L", "reg": f"A{areg}", "value": imm}
                return f"MOVEA.L #$%08X,A{areg}" % imm, 6, info

        # ── MOVE.L abs,abs (23FC = MOVE.L #imm,abs.l) ────────────────
        if w == 0x23FC:
            imm = self.read32(offset + 2)
            addr = self.read32(offset + 6)
            if imm is not None and addr is not None:
                info = {"type": "MOVE.L_ABS", "value": imm, "addr": addr}
                return f"MOVE.L #$%08X,($%08X).l" % (imm, addr), 10, info

        # ── MOVE.W #imm,abs.l (33FC) ────────────────────────────────
        if w == 0x33FC:
            imm = self.read16(offset + 2)
            addr = self.read32(offset + 4)
            if imm is not None and addr is not None:
                info = {"type": "MOVE.W_ABS", "value": imm, "addr": addr}
                return f"MOVE.W #$%04X,($%08X).l" % (imm, addr), 8, info

        # ── ADDA.L #imm,An (D1FC) ────────────────────────────────────
        if (w & 0xF1FF) == 0xD1FC:
            areg = (w >> 9) & 7
            imm = self.read32(offset + 2)
            if imm is not None:
                info = {"type": "ADDA.L", "reg": f"A{areg}", "value": imm}
                return f"ADDA.L #$%08X,A{areg}" % imm, 6, info

        # ── MOVE.L Dn/An,abs (23Cx) ──────────────────────────────────
        if (w & 0xFFC0) == 0x23C0:
            sreg = w & 0xF
            addr = self.read32(offset + 2)
            if addr is not None:
                rname = DREGS[sreg] if sreg < 8 else AREGS[sreg - 8]
                info = {"type": "MOVE_REG_ABS", "reg": rname, "addr": addr}
                return f"MOVE.L {rname},($%08X).l" % addr, 6, info

        # ── RTS ────────────────────────────────────────────────────────
        if w == 0x4E75:
            return "RTS", 2, {"type": "RTS"}

        # ── NOP ────────────────────────────────────────────────────────
        if w == 0x4E71:
            return "NOP", 2, {"type": "NOP"}

        # ── RTE ────────────────────────────────────────────────────────
        if w == 0x4E73:
            return "RTE", 2, {"type": "RTE"}

        # ── SR manipulation ────────────────────────────────────────────
        if w == 0x46FC:
            imm = self.read16(offset + 2)
            if imm is not None:
                return f"MOVE #$%04X,SR" % imm, 4, {"type": "MOVE_SR"}

        # ── Bcc / BRA ─────────────────────────────────────────────────
        cond_names = {0:"BRA",1:"BSR",2:"BHI",3:"BLS",4:"BCC",5:"BCS",
                      6:"BNE",7:"BEQ",8:"BVC",9:"BVS",10:"BPL",11:"BMI",
                      12:"BGE",13:"BLT",14:"BGT",15:"BLE"}
        cc = (w >> 8) & 0xF
        if cc in cond_names and (w & 0xFF00) in [x << 8 for x in range(0x60, 0x70)] and cc != 1:
            disp = w & 0xFF
            cname = cond_names[cc]
            if disp == 0:
                d16 = self.read16(offset + 2)
                if d16 is not None:
                    if d16 & 0x8000:
                        d16 -= 0x10000
                    target = (self.base + offset + 2 + d16) & 0xFFFFFFFF
                    return f"{cname}.W $%06X" % target, 4, {"type": "BCC", "target": target}
            else:
                if disp & 0x80:
                    disp -= 0x100
                target = (self.base + offset + 2 + disp) & 0xFFFFFFFF
                return f"{cname}.B $%06X" % target, 2, {"type": "BCC", "target": target}

        # ── Fallback ──────────────────────────────────────────────────
        return f"DC.W $%04X" % w, 2, {"type": "unknown", "word": w}


def analyze_file(filepath):
    """Analyze a single ROM file."""
    basename = os.path.basename(filepath)
    with open(filepath, "rb") as f:
        data = f.read()

    size = len(data)
    print(f"\n{'='*78}")
    print(f"FILE: {basename}")
    print(f"Size: {size} bytes ({size:#x})")
    print(f"{'='*78}")

    # ── Determine load address and file type ──────────────────────────
    # Check first bytes for header patterns
    first4 = struct.unpack(">I", data[:4])[0] if len(data) >= 4 else 0
    first2 = struct.unpack(">H", data[:2])[0] if len(data) >= 2 else 0

    load_addr = 0
    entry_point = None

    # .prg files: typically raw code, no header. Check for initial instructions
    # .abs files: Atari DRI format or raw code
    # .rom files: typically loaded at $800000 for cart ROMs

    if basename.endswith(".prg"):
        # PRG files are typically loaded into RAM
        # First instruction 4FF9 = LEA (xxx).l,A7 - stack setup
        # This is raw code, load at $000000 or wherever it executes
        load_addr = 0x000000  # typically runs from low RAM
        print(f"Type: .prg (program file, raw 68K code)")
    elif basename.endswith(".abs"):
        # Check for DRI/COFF header (magic 0x601A or 0x0150/0x0107)
        if first2 == 0x601A:
            # DRI header: text_size at +2, data_size at +6, bss at +10, ...
            text_size = struct.unpack(">I", data[2:6])[0]
            data_size = struct.unpack(">I", data[6:10])[0]
            bss_size = struct.unpack(">I", data[10:14])[0]
            entry = struct.unpack(">I", data[14:18])[0]
            print(f"Type: .abs (DRI header: text={text_size}, data={data_size}, bss={bss_size}, entry=${entry:08X})")
            load_addr = entry
            entry_point = entry
            data = data[0x1C:]  # skip DRI header
        else:
            # Raw code - first instruction is 46FC (MOVE #imm,SR)
            load_addr = 0x000000
            print(f"Type: .abs (raw 68K code, no standard header)")
    elif basename.endswith(".rom") or basename.endswith(".j64"):
        if size == 131072 or size == 262144 or size == 1048576 or size == 2097152:
            # Standard Jaguar cart/BIOS ROM sizes
            # Check for boot ROM signature
            if first4 == 0x00000000:
                # Possible boot ROM (starts with stack pointer at 0)
                second4 = struct.unpack(">I", data[4:8])[0] if len(data) >= 8 else 0
                if second4 & 0x00E00000 == 0x00E00000:
                    load_addr = 0xE00000
                    print(f"Type: .rom (boot ROM, loads at $E00000)")
                else:
                    load_addr = 0x800000
                    print(f"Type: .rom (cart ROM, loads at $800000)")
            elif first2 == 0xF620 or (first4 & 0xFFFF0000) == 0xF6420000:
                # Encrypted/scrambled ROM (CD BIOS pattern)
                load_addr = 0x800000
                print(f"Type: .rom (encrypted/scrambled, loads at $800000)")
            else:
                load_addr = 0x800000
                print(f"Type: .rom (cart ROM, loads at $800000)")
        else:
            load_addr = 0x800000
            print(f"Type: .rom (loads at $800000)")

    print(f"Load address: ${load_addr:08X}")
    if entry_point is not None:
        print(f"Entry point: ${entry_point:08X}")

    # ── Scan for BIOS jump table references ──────────────────────────
    disasm = M68KDisassembler(data, load_addr)
    bios_calls = []
    all_instructions = []
    butch_refs = []

    offset = 0
    while offset < len(data) - 1:
        text, size, info = disasm.disasm_one(offset)
        if text is None:
            break
        if size == 0:
            size = 2

        addr = load_addr + offset
        all_instructions.append((offset, addr, text, size, info))

        # Check for BIOS function calls
        if info.get("type") in ("JSR", "JMP"):
            target = info.get("target")
            if isinstance(target, int):
                target_low = target & 0xFFFFFF
                if 0x3000 <= target_low < 0x3E00:
                    bios_calls.append((offset, addr, text, info))

        # Check for BUTCH register references
        if info.get("type") in ("MOVE.L_ABS", "MOVE.W_ABS"):
            ref_addr = info.get("addr", 0)
            if 0xDFFF00 <= ref_addr <= 0xDFFF30:
                butch_refs.append((offset, addr, text, info))
        if info.get("type") == "LEA":
            ref_val = info.get("value", 0)
            if 0xDFFF00 <= ref_val <= 0xDFFF30:
                butch_refs.append((offset, addr, text, info))

        offset += size

    # ── Print BIOS call summary ──────────────────────────────────────
    print(f"\nBIOS Jump Table Calls Found: {len(bios_calls)}")
    print("-" * 70)

    for call_offset, call_addr, call_text, call_info in bios_calls:
        target = call_info.get("target", 0)
        if isinstance(target, int):
            func_name = BIOS_FUNCTIONS.get(target & 0xFFFF, BIOS_FUNCTIONS.get(target, f"unknown_${target:04X}"))
        else:
            func_name = "indirect"

        print(f"\n  ${call_addr:06X}: {call_text}")
        print(f"  Target: {func_name}")

        # Look at preceding instructions for register setup
        print(f"  Context (preceding instructions):")
        # Find index of this instruction
        idx = None
        for i, (o, a, t, s, inf) in enumerate(all_instructions):
            if o == call_offset:
                idx = i
                break

        if idx is not None:
            # Show 12 preceding instructions
            start = max(0, idx - 12)
            for i in range(start, idx + 1):
                o, a, t, s, inf = all_instructions[i]
                marker = ">>>" if i == idx else "   "
                print(f"    {marker} ${a:06X}: {t}")

    # ── BUTCH register references ────────────────────────────────────
    if butch_refs:
        print(f"\nBUTCH Register References: {len(butch_refs)}")
        print("-" * 70)
        for o, a, t, info in butch_refs:
            print(f"  ${a:06X}: {t}")

    # ── Scan for raw 16-bit values matching BIOS addresses ───────────
    # Also find them as data references (not instructions)
    raw_bios_refs = []
    for off in range(0, len(data) - 1, 2):
        w = struct.unpack(">H", data[off:off+2])[0]
        if w in BIOS_FUNCTIONS and w >= 0x3000:
            # Check if preceded by 4EB8/4EF8 (we already caught those)
            if off >= 2:
                prev = struct.unpack(">H", data[off-2:off])[0]
                if prev in (0x4EB8, 0x4EF8):
                    continue  # already found as JSR/JMP
            raw_bios_refs.append((off, w, BIOS_FUNCTIONS[w]))

    if raw_bios_refs:
        print(f"\nRaw BIOS Address References (in data): {len(raw_bios_refs)}")
        print("-" * 70)
        for off, val, name in raw_bios_refs:
            print(f"  offset ${off:06X} (addr ${load_addr+off:06X}): ${val:04X} = {name}")

    # ── First 32 instructions ────────────────────────────────────────
    print(f"\nFirst 40 Instructions (entry point):")
    print("-" * 70)
    for i, (o, a, t, s, inf) in enumerate(all_instructions[:40]):
        print(f"  ${a:06X}: {t}")

    return bios_calls, all_instructions


def analyze_cd_read_patterns(bios_calls, all_instructions, filename):
    """Analyze the calling convention for CD_read ($303C) calls."""
    print(f"\n{'='*78}")
    print(f"CD_READ ($303C) CALLING CONVENTION ANALYSIS — {filename}")
    print(f"{'='*78}")

    cd_read_calls = [c for c in bios_calls
                     if isinstance(c[3].get("target"), int) and
                     (c[3]["target"] & 0xFFFF) == 0x303C]

    if not cd_read_calls:
        print("  No CD_read calls found.")
        return

    for call_offset, call_addr, call_text, call_info in cd_read_calls:
        print(f"\n  CD_read call at ${call_addr:06X}:")

        # Find index
        idx = None
        for i, (o, a, t, s, inf) in enumerate(all_instructions):
            if o == call_offset:
                idx = i
                break

        if idx is None:
            continue

        # Analyze register setup before the call
        reg_state = {}
        # Scan backwards looking for register writes
        for i in range(idx - 1, max(0, idx - 30), -1):
            o, a, t, s, inf = all_instructions[i]
            itype = inf.get("type", "")

            # Stop at labels/branches that might mean we're in a different block
            if itype in ("RTS", "RTE"):
                break

            reg = inf.get("reg")
            if reg and reg not in reg_state:
                if itype in ("MOVEQ", "MOVE.L_IMM", "MOVE.W_IMM", "MOVE.B_IMM",
                             "LEA", "LEA_W", "MOVEA.L"):
                    val = inf.get("value")
                    if val is not None:
                        reg_state[reg] = (val, a, t)

        print(f"  Register state before call:")
        for reg in ["D0", "D1", "D2", "D3", "A0", "A1", "A2"]:
            if reg in reg_state:
                val, at_addr, at_text = reg_state[reg]
                print(f"    {reg} = ${val:08X}  (set at ${at_addr:06X}: {at_text})")
            else:
                print(f"    {reg} = <unknown/dynamic>")

        # Look at what happens after the call
        print(f"  Instructions after call:")
        for i in range(idx + 1, min(len(all_instructions), idx + 8)):
            o, a, t, s, inf = all_instructions[i]
            print(f"    ${a:06X}: {t}")


def analyze_bypass_mechanism(all_instructions, filename):
    """Analyze how the bypass program works around CD auth."""
    print(f"\n{'='*78}")
    print(f"BYPASS MECHANISM ANALYSIS — {filename}")
    print(f"{'='*78}")

    # Look for cart ROM reads ($800000-$8FFFFF)
    cart_refs = []
    # Look for GPU RAM writes ($F03000 area — auth magic)
    gpu_auth_refs = []
    # Look for string patterns
    for i, (o, a, t, s, inf) in enumerate(all_instructions):
        itype = inf.get("type", "")
        if itype in ("LEA", "MOVEA.L"):
            val = inf.get("value", 0)
            if 0x800000 <= val <= 0x8FFFFF:
                cart_refs.append((a, t, val))
            if 0xF03000 <= val <= 0xF03FFF:
                gpu_auth_refs.append((a, t, val))
        if itype == "MOVE.L_IMM":
            val = inf.get("value", 0)
            if val == 0x03D0DEAD:
                gpu_auth_refs.append((a, f"{t}  ; GPU_AUTH_MAGIC!", val))
            # Check for string constants
            try:
                b = struct.pack(">I", val)
                if all(32 <= c < 127 for c in b):
                    s_str = b.decode("ascii")
                    if s_str in ("ATRI", "_NVM", "ATAR"):
                        print(f"  String reference at ${a:06X}: \"{s_str}\" — {t}")
            except:
                pass

    if cart_refs:
        print(f"\n  Cart ROM references ($800000-$8FFFFF):")
        for a, t, v in cart_refs:
            print(f"    ${a:06X}: {t}")

    if gpu_auth_refs:
        print(f"\n  GPU RAM / Auth references ($F03000+):")
        for a, t, v in gpu_auth_refs:
            print(f"    ${a:06X}: {t}")


def scan_for_jump_table_at(data, base_addr, offset, name):
    """Scan for a BRA-based jump table structure."""
    print(f"\n  Scanning for BRA jump table near offset ${offset:04X}:")
    # The BIOS jump table has 6-byte entries: BRA.W (6000 xxxx) + NOP (4E71)
    found = 0
    for i in range(offset, min(offset + 0x200, len(data) - 5), 6):
        w = struct.unpack(">H", data[i:i+2])[0]
        if w == 0x6000:  # BRA.W
            disp = struct.unpack(">h", data[i+2:i+4])[0]
            target = base_addr + i + 2 + disp
            nop = struct.unpack(">H", data[i+4:i+6])[0]
            nop_ok = "(NOP)" if nop == 0x4E71 else f"(${nop:04X})"
            entry_addr = base_addr + i
            func_name = BIOS_FUNCTIONS.get(entry_addr & 0xFFFF, "")
            print(f"    ${entry_addr:06X}: BRA.W ${target:06X} {nop_ok}  {func_name}")
            found += 1
        else:
            if found > 2:
                break
    if found == 0:
        print("    No BRA.W table found at this offset.")


def main():
    rom_dir = "/Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro/test/roms/private"

    files = [
        "CDBYPASS (Symmetry of TNG 2003).prg",
        "CDBYPASS_jiffi.rom",
        "CD Encryption Utility v1.6 (19xx)(BLJ)(PD).abs",
        "CD Verification Utility v.0.5.rom",
        "jagboot.rom",
    ]

    all_results = {}
    for fname in files:
        path = os.path.join(rom_dir, fname)
        if not os.path.exists(path):
            print(f"\nWARNING: File not found: {path}")
            continue

        bios_calls, instructions = analyze_file(path)
        all_results[fname] = (bios_calls, instructions)

        if "CDBYPASS" in fname or "CD " in fname:
            analyze_cd_read_patterns(bios_calls, instructions, fname)
            analyze_bypass_mechanism(instructions, fname)

    # ── Cross-reference summary ──────────────────────────────────────
    print(f"\n{'='*78}")
    print("CROSS-REFERENCE SUMMARY: BIOS Function Usage")
    print(f"{'='*78}")

    func_usage = defaultdict(list)
    for fname, (calls, _) in all_results.items():
        for _, addr, text, info in calls:
            target = info.get("target")
            if isinstance(target, int):
                func_name = BIOS_FUNCTIONS.get(target & 0xFFFF,
                            BIOS_FUNCTIONS.get(target, f"${target:04X}"))
                func_usage[func_name].append((fname, addr))

    for func, usages in sorted(func_usage.items()):
        print(f"\n  {func}:")
        for fname, addr in usages:
            print(f"    {fname} @ ${addr:06X}")

    # ── Final analysis ───────────────────────────────────────────────
    print(f"\n{'='*78}")
    print("CALLING CONVENTION SUMMARY")
    print(f"{'='*78}")
    print("""
Based on analysis of the BIOS disassembly and these programs:

CD_init ($3006):
  Input:  D0.W = mode (e.g., $0002 for audio, $0003 for data/CD-ROM)
  Output: none

CD_mode ($3048 / CD_setup):
  Input:  D0.W = mode flags
  Output: none

CD_read ($303C):
  Input:  D0.L = packed MSF position
               bits 23-16: minutes (BCD or binary depending on implementation)
               bits 15-8:  seconds
               bits  7-0:  frames
          A0 = destination buffer address in Jaguar RAM
          A1 = end address (A0 + transfer_length)
  Output: none (asynchronous — use CD_poll to check)

CD_poll ($304E):
  Input:  none
  Output: A0 = current transfer position
          A1 = error status (0 = ok)
          (transfer complete when A0 >= A1 from the original CD_read call)

CD_stop ($301E):
  Input:  none
  Output: none

CD_osamp ($305A):
  Input:  A0 = buffer address
  Output: none (sets up oversampling)

GPU_ISR_setup ($3060):
  Input:  (internal — sets up GPU ISR for CD FIFO drain)
  Output: none
""")


if __name__ == "__main__":
    main()
