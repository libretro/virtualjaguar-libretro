#!/usr/bin/env python3
"""
Jaguar CD BIOS GPU ISR Disassembler

Disassembles the GPU RISC ISR code used by the CD BIOS for CD-ROM data transfer.
The BIOS entry $3060 (CD_setup_cdrom_isr) copies $150 bytes of GPU code from
ROM to GPU RAM. This ISR reads I2S FIFO data, matches a sync sentinel, and
transfers CD-ROM data to main RAM.

GPU instruction encoding (from VirtualJaguar gpu.c):
  Bits [15:10] = opcode (0-63)
  Bits [9:5]   = first_parameter (reg1/IMM_1: source register, immediate, or
                 for JR: signed offset; for JUMP: target register)
  Bits [4:0]   = second_parameter (reg2/IMM_2: destination register, or
                 for JR/JUMP: condition code)

Condition code (bits [4:0] for JR/JUMP, AND logic):
  Bit 0: require Z=0 (NE)
  Bit 1: require Z=1 (EQ)
  Bit 2 with bit4=0: require C=0 (CC) / with bit4=1: require N=0 (PL)
  Bit 3 with bit4=0: require C=1 (CS) / with bit4=1: require N=1 (MI)
  Bit 4: switches bits 2-3 between Carry and Negative flag testing
  0 = T (always), conflicting bits = NEVER
"""

import struct
import sys
import os

OPCODES = {
    # Matches VirtualJaguar gpu.c gpu_opcode[64] array exactly
    0:  ("ADD",     "rr"),     1:  ("ADDC",    "rr"),
    2:  ("ADDQ",    "ir"),     3:  ("ADDQT",   "ir"),
    4:  ("SUB",     "rr"),     5:  ("SUBC",    "rr"),
    6:  ("SUBQ",    "ir"),     7:  ("SUBQT",   "ir"),
    8:  ("NEG",     "rr"),     9:  ("AND",     "rr"),
    10: ("OR",      "rr"),     11: ("XOR",     "rr"),
    12: ("NOT",     "rr"),     13: ("BTST",    "ir"),
    14: ("BSET",    "ir"),     15: ("BCLR",    "ir"),
    16: ("MULT",    "rr"),     17: ("IMULT",   "rr"),
    18: ("IMULTN",  "rr"),     19: ("RESMAC",  "r"),
    20: ("IMACN",   "rr"),     21: ("DIV",     "rr"),
    22: ("ABS",     "rr"),     23: ("SH",      "rr"),
    24: ("SHLQ",    "ir"),     25: ("SHRQ",    "ir"),
    26: ("SHA",     "rr"),     27: ("SHARQ",   "ir"),
    28: ("ROR",     "rr"),     29: ("RORQ",    "ir"),
    30: ("CMP",     "rr"),     31: ("CMPQ",    "ir5s"),
    32: ("SAT8",    "rr"),     33: ("SAT16",   "rr"),
    34: ("MOVE",    "rr"),     35: ("MOVEQ",   "ir"),
    36: ("MOVETA",  "rr"),     37: ("MOVEFA",  "rr"),
    38: ("MOVEI",   "i32r"),   39: ("LOADB",   "mr"),
    40: ("LOADW",   "mr"),     41: ("LOAD",    "mr"),
    42: ("LOADP",   "mr"),     43: ("LD_R14I", "mr14"),  # LOAD (R14+n), Rn
    44: ("LD_R15I", "mr15"),   # LOAD (R15+n), Rn
    45: ("STOREB",  "rm"),     46: ("STOREW",  "rm"),
    47: ("STORE",   "rm"),     48: ("STOREP",  "rm"),
    49: ("ST_R14I", "r14m"),   # STORE Rn, (R14+n)
    50: ("ST_R15I", "r15m"),   # STORE Rn, (R15+n)
    51: ("MOVEPC",  "rr"),
    52: ("JUMP",    "jmp"),    53: ("JR",      "jr"),
    54: ("MMULT",   "rr"),     55: ("MTOI",    "rr"),
    56: ("NORMI",   "rr"),     57: ("NOP",     ""),
    58: ("LD_R14R", "mr14r"),  # LOAD (R14+Rn), Rn
    59: ("LD_R15R", "mr15r"),  # LOAD (R15+Rn), Rn
    60: ("ST_R14R", "r14mr"),  # STORE Rn, (R14+Rn)
    61: ("ST_R15R", "r15mr"),  # STORE Rn, (R15+Rn)
    62: ("SAT24",   "rr"),     63: ("PACK",    "rr"),
}

HW_REGS = {
    0xF02100: "G_FLAGS",    0xF02114: "G_CTRL",
    0xF02110: "G_PC",       0xF03000: "GPU_RAM",
    0xF1A100: "D_FLAGS",    0xF1A148: "L_I2S",
    0xF1A14C: "R_I2S",      0xDFFF00: "BUTCH",
    0xDFFF04: "DSCNTRL",    0xDFFF0A: "DS_DATA",
    0xDFFF10: "I2SCNTRL",   0xDFFF24: "FIFO_DATA",
    0xDFFF28: "I2SDAT2",    0xF10020: "JERRY_INT",
}

ROM_BASE = 0x800000
JT_ROM_BASE = 0x8084A6


def read16(d, o): return struct.unpack('>H', d[o:o+2])[0]
def read32(d, o): return struct.unpack('>L', d[o:o+4])[0]
def sign5(v): return v - 32 if v & 0x10 else v
def addq_v(n): return 32 if n == 0 else n


def decode_cc(j):
    """Decode condition code (AND logic)."""
    if j == 0: return "T"
    parts = []
    if (j & 1) and (j & 2): return "NEVER"
    elif j & 1: parts.append("NE")
    elif j & 2: parts.append("EQ")
    bit4 = (j >> 4) & 1
    if bit4 == 0:
        if (j & 4) and (j & 8): return "NEVER"
        elif j & 4: parts.append("CC")
        elif j & 8: parts.append("CS")
    else:
        if (j & 4) and (j & 8): return "NEVER"
        elif j & 4: parts.append("PL")
        elif j & 8: parts.append("MI")
    return "+".join(parts) if parts else "T"


def disasm_gpu(code_bytes, base_addr, data_base=None):
    """Disassemble Jaguar GPU RISC code with correct JR/JUMP encoding."""
    result = []
    i = 0
    while i < len(code_bytes):
        addr = base_addr + i
        if i + 2 > len(code_bytes): break
        w = struct.unpack('>H', code_bytes[i:i+2])[0]
        op = (w >> 10) & 0x3F
        reg1 = (w >> 5) & 0x1F   # first_parameter / IMM_1
        reg2 = w & 0x1F          # second_parameter / IMM_2
        hx = f'{w:04X}'
        cmt = ""

        if op not in OPCODES:
            result.append((addr, hx, f"???{op}", f"${w:04X}", "")); i += 2; continue

        name, fmt = OPCODES[op]

        if fmt == "i32r":
            if i + 6 > len(code_bytes):
                result.append((addr, hx, name, "???", "")); i += 2; continue
            lo = struct.unpack('>H', code_bytes[i+2:i+4])[0]
            hi = struct.unpack('>H', code_bytes[i+4:i+6])[0]
            imm = (hi << 16) | lo
            hx = f'{w:04X} {lo:04X} {hi:04X}'
            operands = f'#${imm:08X}, R{reg2:02d}'
            if imm in HW_REGS: cmt = HW_REGS[imm]
            elif data_base and data_base <= imm < data_base + 0x200:
                off = imm - data_base
                dl = {0:"write_ptr", 4:"xfer_limit", 8:"bytes_done",
                      0xC:"mode_flags", 0x10:"sentinel"}
                cmt = f"DATA+${off:02X} ({dl.get(off, '')})"
            i += 6; result.append((addr, hx, name, operands, cmt)); continue

        elif fmt == "jr":
            # JR: reg1(bits[9:5])=signed offset, reg2(bits[4:0])=condition
            cc = reg2
            offset = sign5(reg1)
            cc_name = decode_cc(cc)
            target = addr + 2 + (offset * 2)
            if cc_name == "T":
                operands = f'${target:08X}'
                cmt = "always"
            elif cc_name == "NEVER":
                operands = f'${target:08X}'
                cmt = "NEVER (delay slot only)"
            else:
                operands = f'{cc_name}, ${target:08X}'

        elif fmt == "jmp":
            # JUMP: reg1(bits[9:5])=target register, reg2(bits[4:0])=condition
            cc = reg2
            treg = reg1
            cc_name = decode_cc(cc)
            if cc_name == "T":
                operands = f'T, (R{treg:02d})'
                cmt = "always"
            elif cc_name == "NEVER":
                operands = f'NEVER, (R{treg:02d})'
                cmt = "delay slot only"
            else:
                operands = f'{cc_name}, (R{treg:02d})'

        elif fmt == "mr":
            operands = f'(R{reg1:02d}), R{reg2:02d}'
        elif fmt == "rm":
            operands = f'R{reg2:02d}, (R{reg1:02d})'
        elif fmt == "mr14":
            operands = f'(R14+{reg1}), R{reg2:02d}'
        elif fmt == "mr15":
            operands = f'(R15+{reg1}), R{reg2:02d}'
        elif fmt == "r14m":
            operands = f'R{reg2:02d}, (R14+{reg1})'
        elif fmt == "r15m":
            operands = f'R{reg2:02d}, (R15+{reg1})'
        elif fmt == "mr14r":
            operands = f'(R14+R{reg1:02d}), R{reg2:02d}'
        elif fmt == "mr15r":
            operands = f'(R15+R{reg1:02d}), R{reg2:02d}'
        elif fmt == "r14mr":
            operands = f'R{reg2:02d}, (R14+R{reg1:02d})'
        elif fmt == "r15mr":
            operands = f'R{reg2:02d}, (R15+R{reg1:02d})'
        elif fmt == "rr":
            operands = f'R{reg1:02d}, R{reg2:02d}'
        elif fmt in ("ir", "ir5s"):
            if op in (2,3,6,7): operands = f'#{addq_v(reg1)}, R{reg2:02d}'
            elif op == 24: operands = f'#{32-reg1}, R{reg2:02d}'
            elif op in (25,27,29): operands = f'#{reg1 if reg1 else 32}, R{reg2:02d}'
            elif op == 31: operands = f'#{sign5(reg1)}, R{reg2:02d}'
            elif op == 35: operands = f'#{reg1}, R{reg2:02d}'
            else: operands = f'#{reg1}, R{reg2:02d}'
        elif fmt == "r":
            operands = f'R{reg2:02d}'
        elif fmt == "":
            operands = ""
        else:
            operands = f'R{reg1:02d}, R{reg2:02d}'

        i += 2
        result.append((addr, hx, name, operands, cmt))
    return result


def print_68k_setup(data):
    """Disassemble entry 16 68K setup."""
    try:
        from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
    except ImportError:
        print("(capstone unavailable)"); return
    jt_off = JT_ROM_BASE - ROM_BASE
    e16_off = jt_off + 16 * 6
    disp = struct.unpack('>h', data[e16_off+2:e16_off+4])[0]
    ram = 0x3060 + 2 + disp
    rom = JT_ROM_BASE + (ram - 0x3000)
    off = rom - ROM_BASE
    md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
    code = data[off:off+256]

    print("=" * 90)
    print("68K SETUP: CD_setup_cdrom_isr (entry 16 / JSR $3060)")
    print("=" * 90)
    for insn in md.disasm(code, rom):
        r = 0x3000 + (insn.address - JT_ROM_BASE)
        hx = ' '.join(f'{b:02X}' for b in insn.bytes)
        cmt = ""
        o = insn.op_str
        if '$3074' in o: cmt = "; store GPU data area ptr"
        elif '#$14' in o: cmt = "; offset to ISR code start"
        elif '#$ffff' in o: cmt = "; mask to 16 bits"
        elif '#$981e' in o: cmt = "; MOVEI R30 opcode word"
        elif '$f03010' in o: cmt = "; patch GPU RAM +$10"
        elif '#$f0d3c0' in o: cmt = "; MOVEI imm hi=$00F0, then JUMP T,(R30)"
        elif '$f03014' in o: cmt = "; GPU RAM +$14"
        elif '#$e400' in o: cmt = "; NOP NOP (two GPU NOPs)"
        elif '$f03018' in o: cmt = "; GPU RAM +$18"
        elif '#$3382' in o: cmt = "; source of ISR template in RAM"
        elif '#$150' in o: cmt = "; copy 336 bytes"
        elif 'dfff0a' in o: cmt = "; flush DS_DATA"
        elif 'dfff04' in o: cmt = "; flush DSCNTRL"
        elif '$3072' in o: cmt = "; CD-ROM mode flag byte"
        elif '#$ff' in o.lower() and 'move' in insn.mnemonic.lower(): cmt = "; $FF = CD-ROM mode"
        elif '$f02100' in o: cmt = "; G_FLAGS"
        elif '#$20' in o and 'or' in insn.mnemonic.lower(): cmt = "; set REGPAGE bit"
        print(f"  ${r:06X}: {hx:<30s} {insn.mnemonic:<8s} {o:<35s} {cmt}")
        if insn.mnemonic.lower() in ('rts','rte'): break


def main():
    paths = [
        os.path.join(os.getcwd(), "test", "roms", "private",
                     "[BIOS] Atari Jaguar CD (World).j64"),
        "test/roms/private/[BIOS] Atari Jaguar CD (World).j64",
    ]
    path = None
    for p in paths:
        if os.path.exists(p): path = p; break
    if len(sys.argv) > 1: path = sys.argv[1]
    if not path or not os.path.exists(path):
        print("ERROR: BIOS not found"); sys.exit(1)
    with open(path, 'rb') as f:
        data = f.read()
    print(f"BIOS: {os.path.basename(path)}, {len(data)} bytes")
    print()

    # 68K setup
    print_68k_setup(data)

    # GPU ISR code: $150 bytes at ROM $808828 (RAM $3382)
    gpu_off = 0x8828
    gpu_code = data[gpu_off:gpu_off+0x150]
    gpu_base = 0xF03000

    # Data area
    print()
    print("=" * 90)
    print("GPU DATA AREA (first $14 bytes at A0 = GPU_RAM base)")
    print("=" * 90)
    for i in range(0, 0x14, 4):
        v = struct.unpack('>L', gpu_code[i:i+4])[0]
        lbl = {0:"write_ptr",4:"xfer_limit",8:"bytes_done",
               0xC:"mode_flags",0x10:"sentinel_D1"}.get(i,"")
        print(f"  +${i:02X}: ${v:08X}  ; {lbl}")

    # Disassemble ISR
    isr_code = gpu_code[0x14:]
    isr_base = gpu_base + 0x14
    insns = disasm_gpu(isr_code, isr_base, data_base=gpu_base)

    # Build jump targets for labels
    targets = set()
    for addr, _, mnem, operands, _ in insns:
        if mnem in ("JR", "JUMP"):
            for part in operands.replace(',', ' ').split():
                if part.startswith('$'):
                    try: targets.add(int(part[1:], 16))
                    except: pass

    print()
    print("=" * 90)
    print("GPU ISR DISASSEMBLY -- CD-ROM MODE (entry 16)")
    print("ISR entry: $F03010 (GPU ext IRQ vector)")
    print("ISR code:  $F03014 (after 2 words of zero = NOP NOP)")
    print("Data area: $F03000 - $F03013")
    print("=" * 90)
    print()
    print(f"{'Addr':>10s}  {'Off':>5s}  {'Hex':16s}  {'Instruction':<42s}  {'Comment'}")
    print("-" * 120)

    for addr, hx, mnem, operands, cmt in insns:
        off = addr - gpu_base
        if addr in targets:
            print(f"\nL_{off:04X}:")
        instr = f"{mnem:<8s} {operands}"
        c = f"  ; {cmt}" if cmt else ""
        print(f"  ${addr:08X}  +${off:04X}  {hx:16s}  {instr:<42s}{c}")

    # Summary tables
    print()
    print("=" * 90)
    print("INSTRUCTION SUMMARY")
    print("=" * 90)

    for category, filter_fn in [
        ("MOVEI (immediate loads)", lambda m,_: m=="MOVEI"),
        ("LOAD/LOADW/LOADB (memory reads)", lambda m,_: m.startswith("LOAD")),
        ("STORE/STOREW/STOREB/STOREP (memory writes)", lambda m,_: m.startswith("STORE")),
        ("CMP/CMPQ (comparisons)", lambda m,_: m.startswith("CMP")),
        ("JR/JUMP (branches)", lambda m,_: m in ("JR","JUMP")),
        ("BTST (bit tests)", lambda m,_: m=="BTST"),
        ("BSET/BCLR (bit set/clear)", lambda m,_: m in ("BSET","BCLR")),
    ]:
        print(f"\n  {category}:")
        for addr, hx, mnem, operands, cmt in insns:
            if filter_fn(mnem, operands):
                c = f"  ; {cmt}" if cmt else ""
                print(f"    ${addr:08X}: {mnem:<8s} {operands}{c}")

    # Annotated pseudocode
    print()
    print("=" * 90)
    print("ANNOTATED PSEUDOCODE -- CD-ROM GPU ISR FLOW")
    print("=" * 90)
    print("""
=== ARCHITECTURE ===

The GPU ISR is triggered by Jerry's external interrupt when the CD FIFO
is half-full. The BIOS sets REGPAGE in G_FLAGS so the GPU swaps to the
alternate register bank on interrupt entry.

The ISR entry at GPU_RAM+$10 is patched by the 68K setup (entry 16) to:
  +$10: MOVEI  #(GPU_RAM+$14), R30     ; load ISR code address
  +$14: JUMP   T, (R30)                ; jump to ISR body
  +$18: NOP
  +$1A: NOP

=== PROLOGUE (+$0014 - +$003A) ===

  MOVEI  #$F02100, R30         ; R30 = G_FLAGS address
  LOAD   (R30), R29            ; R29 = current G_FLAGS (saved for epilogue)
  [push R25, R24, R27, R26, R23, R22 to stack via R31]

  MOVEI  #$DFFF00, R24         ; R24 = BUTCH base address
  LOAD   (R24), R27            ; R27 = BUTCH master control register

=== SELF-LOCATION (+$003C - +$004E) ===

  The ISR uses MOVEPC to determine its own position in GPU RAM.
  MOVEPC stores (PC - 2) = address of the MOVEPC instruction itself.

  MOVEPC R00, R23              ; R23 = addr of this instr = GPU_RAM+$3C
  MOVEI  #$3C, R28             ; offset of MOVEPC from data area base
  SUB    R28, R23              ; R23 = GPU_RAM+$3C - $3C = GPU_RAM = data area base

  MOVEPC R00, R25              ; R25 = addr of this instr = GPU_RAM+$46
  MOVEI  #$88, R26
  ADD    R26, R25              ; R25 = GPU_RAM+$46+$88 = GPU_RAM+$CE = epilogue addr

=== BUTCH IRQ CHECK & ACKNOWLEDGE (+$0050 - +$0070) ===

  BTST   #13, R27              ; test BUTCH bit 13 (FIFO half-full IRQ pending)
  JR     EQ, +$0072            ; if bit 13 CLEAR (no FIFO IRQ), skip to mode check
  BCLR   #5, R27               ; [delay] clear bit 5 (FIFO IRQ acknowledge)

  -- FIFO IRQ is pending: acknowledge and handle --
  BSET   #1, R27               ; set bit 1
  STORE  R27, (R24)            ; write back to BUTCH ($DFFF00)
  ADDQ   #16, R24              ; R24 -> $DFFF10 (I2SCNTRL)
  LOAD   (R24), R27            ; read I2SCNTRL
  BSET   #2, R27               ; set bit 2 (enable FIFO)
  STORE  R27, (R24)            ; write I2SCNTRL

  -- Read DSA status --
  SUBQ   #12, R24              ; R24 -> $DFFF04 (DSCNTRL)
  LOAD   (R24), R26            ; R26 = DSCNTRL value
  ADDQ   #6, R24               ; R24 -> $DFFF0A (DS_DATA)
  LOADW  (R24), R27            ; R27 = DS_DATA (16-bit DSA response word)
  BTST   #10, R27              ; test bit 10 of DSA response
  JR     NE, +$008C            ; if bit 10 SET, jump to DSA error handler
  OR     R26, R26              ; [delay] test DSCNTRL (sets flags)

  -- Normal path: jump to epilogue (R25) to exit ISR --
  JUMP   T, (R25)              ; unconditional jump to epilogue ($F030CE)

=== MODE CHECK (+$0072 - +$007A) ===

  Reached when FIFO half-full IRQ is NOT pending (bit 13 clear).

  ADDQ   #12, R23              ; [delay from JR at +$0052] R23 -> DATA+$0C
  LOAD   (R23), R26            ; R26 = DATA+$0C (mode_flags)
  CMPQ   #0, R26               ; test if mode == 0
  JR     EQ, +$0086            ; if mode == 0, jump to audio-mode/DSA handler
  SUBQ   #12, R23              ; [delay] R23 restored to data area base

  -- Mode != 0: CD-ROM mode active --

=== COMPUTE SENTINEL SCAN ADDRESS (+$007C - +$0084) ===

  MOVE   R25, R28              ; R28 = epilogue addr (GPU_RAM+$CE)
  ADDQ   #28, R28              ; R28 += $1C
  ADDQ   #12, R23              ; R23 -> DATA+$0C (mode_flags)
  ADDQ   #28, R28              ; R28 += $1C = GPU_RAM+$CE+$38 = GPU_RAM+$106
  JUMP   T, (R28)              ; unconditional jump to sentinel scan (+$0106)
                                ; The BTST #14 at +$0086 executes as delay slot
                                ; but its result is discarded since jump is taken.

=== AUDIO-MODE / DSA HANDLER (+$0086 - +$008A) ===

  Reached when mode == 0 (audio mode) from +$0078.

  BTST   #14, R27              ; test bit 14 of DSA response
  JR     EQ, +$009C            ; if bit 14 clear, jump to byte-count compare
  BSET   #31, R27              ; [delay] set bit 31

=== DSA ERROR HANDLER (+$008C - +$009A) ===

  Reached when DSA bit 10 set (from +$006C) or bit 14 set (from +$0086).

  ADDQ   #16, R24              ; R24 -> $DFFF1A (DS_DATA + $10?)
  LOAD   (R24), R28            ; read value
  OR     R28, R28              ; test it (set flags)
  SUBQ   #16, R24              ; R24 restored

  LOAD   (R23), R28            ; R28 = DATA+$0C (mode_flags)
  ADDQ   #8, R23               ; R23 -> DATA+$14 (overlaps ISR code)
  STORE  R28, (R23)            ; copy mode_flags to DATA+$14 (scratch/backup)
  SUBQ   #8, R23               ; R23 -> DATA+$0C

=== TRANSFER BYTE-COUNT COMPARE (+$009C - +$00AA) ===

  LOAD   (R23), R26            ; R26 = DATA+$0C (mode_flags or byte count)
  ADDQ   #4, R23               ; R23 -> DATA+$10 (sentinel/limit)
  LOAD   (R23), R28            ; R28 = DATA+$10
  SUBQ   #4, R23               ; R23 -> DATA+$0C

  CMP    R26, R28              ; compare: R28 (DATA+$10) vs R26 (DATA+$0C)
  JR     PL, +$00AC            ; if R28 >= R26 (unsigned), jump to FIFO drain
  BCLR   #0, R27               ; [delay] clear bit 0 of DSA word
  STORE  R27, (R24)            ; write modified DSA word back

=== FIFO DRAIN LOOP (+$00AC - +$00CC) ===

  This loop reads and stores FIFO data to RAM, 4 iterations per IRQ.
  Each iteration reads BOTH the right ($DFFF28) and left ($DFFF24) channels.

  MOVEI  #$DFFF24, R27         ; R27 = $DFFF24 (left FIFO)
  MOVE   R27, R25              ; R25 = $DFFF24 (left FIFO, kept constant)
  ADDQ   #4, R27               ; R27 = $DFFF28 (right FIFO)
  MOVEQ  #3, R24               ; R24 = 3 (loop counter: 4 iterations)

  -- Inner loop (4x): read R+L pair, store to sequential RAM --
  LOAD   (R27), R28            ; R28 = right channel FIFO ($DFFF28) [32-bit]
  LOAD   (R25), R30            ; R30 = left channel FIFO ($DFFF24) [32-bit]
  ADDQ   #4, R26               ; advance RAM pointer
  BCLR   #0, R26               ; word-align pointer
  STORE  R28, (R26)            ; store RIGHT word to RAM
  ADDQ   #4, R26               ; advance RAM pointer
  BCLR   #0, R26               ; word-align pointer
  SUBQ   #1, R24               ; decrement loop counter
  JR     PL, +$00B8            ; if counter >= 0, loop back
  STORE  R30, (R26)            ; [delay] store LEFT word to RAM

  STORE  R26, (R23)            ; save updated write pointer to DATA+$00
                                ; NOTE: R23 was pointing to DATA+$0C, but this
                                ; must be correct -- the code tracks the write
                                ; pointer through the DATA structure somehow.

=== INTERRUPT CLEAR (+$00CE - +$00D8) ===

  R25 points here (epilogue entry point, computed at +$004E).

  MOVEI  #$F10020, R24         ; Jerry interrupt control
  MOVEQ  #1, R28               ;
  BSET   #8, R28               ; R28 = $0101
  STOREW R28, (R24)            ; clear Jerry external interrupt (write $0101)

=== EPILOGUE (+$00DA - +$0104) ===

  [restore R22, R23, R26, R27, R24, R25 from stack (reverse push order)]
  MOVEI  #$F02100, R30         ; G_FLAGS
  BCLR   #3, R29               ; clear IMASK in saved flags
  BSET   #10, R29              ; set INT_CLR1 (clear ext IRQ latch)
  LOAD   (R31), R28            ; restore return address from stack
  ADDQ   #2, R28               ; adjust return PC (+2 for pipeline delay)
  ADDQ   #4, R31               ; pop stack
  JUMP   T, (R28)              ; return from interrupt
  STORE  R29, (R30)            ; [delay] write G_FLAGS (acknowledge interrupt)

=== SENTINEL SCAN PHASE (+$0106 - +$0134) ===

  Reached via JUMP T,(R28) from +$0084 when mode != 0.
  R28 = GPU_RAM+$106 (computed as epilogue+$38).
  R23 = DATA+$0C (mode_flags field).

  MOVEI  #$DFFF24, R27         ; R27 = FIFO_DATA address
  MOVEQ  #3, R30               ; R30 = 3
  MOVEQ  #9, R22               ; R22 = 9 (per-IRQ scan counter)
  SHLQ   #2, R30               ; R30 = 12 = $0C (XOR mask: $DFFF24 ^ $0C = $DFFF28)
  ADDQ   #4, R23               ; R23 -> DATA+$10 (sentinel field)
  LOAD   (R23), R24            ; R24 = SENTINEL value (from CD_read D1 parameter)
  SUBQ   #4, R23               ; R23 -> DATA+$0C
  JR     +$0120                ; skip first-time init, jump into scan loop
  NOP                           ; [delay]

  -- First-time init (reached on sentinel mismatch from +$012C) --
  L_011C:
  MOVEQ  #16, R26              ; R26 = $10 (scanning mode marker)
  STORE  R26, (R23)            ; DATA+$0C = $10 (reset scanning state)

  -- Scan loop (up to 10 reads per IRQ invocation) --
  L_0120:
  SUBQ   #1, R22               ; decrement per-IRQ counter
  JUMP   EQ, (R25)             ; if counter exhausted, exit to epilogue
  NOP                           ; [delay]
  XOR    R30, R27              ; toggle FIFO address: $DFFF24 <-> $DFFF28
  LOAD   (R27), R28            ; read 32-bit FIFO data from current L/R channel
  CMP    R28, R24              ; compare: R24 (sentinel) - R28 (FIFO data)
  JR     NE, L_011C            ; if NO match, reset mode to $10, loop again
  NOP                           ; [delay]

  -- Sentinel MATCHED --
  SUBQ   #1, R26               ; decrement mode counter (was $10, now $0F, etc.)
  JR     NE, L_0120            ; if mode counter != 0, need more matches -- loop
  STORE  R26, (R23)            ; [delay] save decremented mode to DATA+$0C

  The ISR requires 16 ($10) CONSECUTIVE sentinel matches to transition
  from scanning mode to data transfer mode. Any mismatch resets the
  counter back to $10. This ensures the sync marker is robust.

  When R26 reaches 0 (16 consecutive matches), fall through:

=== DATA TRANSFER PHASE (+$0136 - +$014E) ===

  After 16 consecutive sentinel matches confirm sync, begin data transfer.
  R23 was at DATA+$0C, R22 still has remaining per-IRQ counter.

  SUBQ   #12, R23              ; R23 -> DATA+$00 (write pointer field)
  LOAD   (R23), R26            ; R26 = current RAM write pointer

  -- Transfer loop: read FIFO words and store to sequential RAM --
  L_013A:
  XOR    R30, R27              ; toggle FIFO L/R address
  ADDQT  #4, R26               ; advance write pointer (no flag change)
  LOAD   (R27), R28            ; read 32-bit FIFO data
  STORE  R28, (R26)            ; store to RAM at write pointer
  SUBQ   #1, R22               ; decrement per-IRQ counter
  JR     NE, L_013A            ; if counter != 0, loop
  NOP                           ; [delay]

  -- Done for this IRQ --
  STORE  R26, (R23)            ; save updated write pointer to DATA+$00
  JUMP   T, (R25)              ; jump to epilogue ($F030CE)
  NOP                           ; [delay]

=== KEY FINDINGS ===

1. SELF-LOCATION:
   Uses MOVEPC (opcode 51), NOT PACK. MOVEPC loads the current GPU PC
   into a register, giving the ISR its own address. The ISR subtracts
   a known offset ($3C) to find the data area base, and adds $88 to
   find the epilogue entry point at GPU_RAM+$CE.

2. SENTINEL MATCHING:
   The sentinel value comes from D1 register passed to CD_read ($303C).
   It is stored at DATA+$10 in the GPU data area.
   The ISR loads it into R24 and compares it against raw 32-bit FIFO reads
   at +$012A: CMP R28, R24.

3. FIFO READING:
   The ISR alternates between $DFFF24 (left/FIFO_DATA) and $DFFF28 (right/I2SDAT2)
   using XOR with $0C to toggle the address.
   Each FIFO read is a 32-bit GPU LOAD = GPUReadLong() = two 16-bit reads.

4. NO BYTE-SWAPPING:
   CMP compares the raw 32-bit FIFO word directly against the sentinel.
   No byte or word swapping occurs between reading the FIFO and comparing.
   In VirtualJaguar: GPUReadLong($DFFF24) calls JaguarReadWord($DFFF24)<<16 |
   JaguarReadWord($DFFF26), each of which returns (cdBuf[ptr]<<8|cdBuf[ptr+1]).
   So the sentinel must match 4 consecutive bytes from cdBuf in big-endian order.

5. CONSECUTIVE MATCH REQUIREMENT:
   The scan loop at +$0120-$0134 uses R26 as a match counter, initialized
   to $10 (16) at L_011C on any mismatch. On each sentinel match, R26 is
   decremented. If R26 reaches 0, the ISR transitions to data transfer.
   Any mismatch resets R26 back to $10.

   IMPORTANT: R26 is NOT loaded from DATA+$0C at the start of sentinel
   scan. It enters the scan with whatever value bank 0 R26 had from the
   main GPU context (typically 0 after GPU reset). The ISR saves/restores
   R26 on the stack, so modifications during scanning do NOT persist
   across IRQ boundaries. Only DATA+$0C (written by STORE) persists.

   Per-IRQ budget is 9 reads (R22 starts at 9, decremented before first
   read). Since R26 starts uninitialized each IRQ and must count down from
   $10 to 0 (16 matches), the sentinel scan CANNOT complete in a single
   IRQ. This suggests the sentinel scan phase primarily DRAINS the FIFO
   while looking for sync, and the actual data transfer may begin through
   a different mechanism (e.g., the FIFO drain loop at +$00AC after the
   mode flag transitions).

6. DUAL-PATH ISR:
   The ISR has two main branches at +$0050:
   a. FIFO half-full (BUTCH bit 13 set): acknowledge IRQ, read DSA status,
      then jump to epilogue. Does NOT read any FIFO data.
   b. FIFO NOT half-full (bit 13 clear): check mode_flags at DATA+$0C.
      If mode==0: go to audio/byte-count path at +$0086/+$009C.
      If mode!=0: jump to sentinel scan at +$0106.

   The FIFO drain loop at +$00AC is reached from the byte-count compare
   at +$009C (via JR PL). This is the actual data transfer path.
   The sentinel scan at +$0106 scans for the sync marker to confirm
   the seek position is correct.

7. STATE MACHINE:
   DATA+$0C (mode_flags) controls ISR behavior:
   - $00 = audio mode (ISR goes to audio path at +$0086)
   - $10 = CD-ROM scanning mode (ISR scans for sentinel)
   - $01-$0F = partial sentinel match in progress
   - $00 after scan = transition to data transfer (16 matches done)

8. DATA TRANSFER:
   Once scanning completes (R26=0), the ISR switches to reading FIFO data
   and storing it sequentially to RAM via the write pointer at DATA+$00.
   Each IRQ transfers up to (remaining R22 count) 32-bit words.
   The write pointer is saved after each IRQ for continuation.
""")


if __name__ == '__main__':
    main()
