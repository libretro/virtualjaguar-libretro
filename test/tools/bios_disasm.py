#!/usr/bin/env python3
"""
Jaguar CD BIOS Jump Table Reverse Engineering Tool

Loads the Jaguar CD BIOS ROM (mapped at $800000) and reverse-engineers
the calling conventions for the jump table entries at $3000-$3DFF.

The BIOS copies code from ROM $8084A6 (retail) / $8084FC (developer) to
RAM $3000-$39FF.  The jump table has 18 entries of 6 bytes each
(BRA.W + NOP), except entry 0 which uses BRA.B + NOP + 2 padding bytes.
Games call via JSR $3000+n*6.

Usage:
    python3 bios_disasm.py [bios_file.j64]

If no file is given, uses the default retail BIOS path.
"""

import struct
import sys
import os

try:
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False
    print("ERROR: capstone is required. Install with: pip install capstone")
    sys.exit(1)

ROM_BASE = 0x800000
JUMP_TABLE_RAM = 0x3000

# Retail BIOS: jump table ROM source is at $8084A6
# Developer BIOS: jump table ROM source is at $8084FC
# The difference is 0x56 bytes of extra data tables in the developer BIOS.
# Both produce identical code at RAM $3000.

ENTRY_NAMES = {
    0:  "CD_setup_audio_isr",
    1:  "CD_wait_dsa_response",
    2:  "CD_wait_dsa_response2",  # same code as entry 1
    3:  "CD_i2s_enable",
    4:  "CD_spin_up",
    5:  "CD_stop_drive",
    6:  "CD_set_volume_mute",
    7:  "CD_set_volume_max",
    8:  "CD_pause",
    9:  "CD_unpause",
    10: "CD_read",
    11: "CD_fifo_disable",
    12: "CD_hw_reset",
    13: "CD_poll",
    14: "CD_set_dac_mode",
    15: "CD_read_toc",
    16: "CD_setup_cdrom_isr",
    17: "CD_setup_data_isr",
}


def load_bios(path):
    with open(path, 'rb') as f:
        data = f.read()
    return data


def read16(data, off):
    return struct.unpack('>H', data[off:off+2])[0]


def read32(data, off):
    return struct.unpack('>L', data[off:off+4])[0]


def find_jt_rom_base(data):
    """
    Find where the jump table source is in ROM by looking at the
    entry populator code at $802000.

    The populator does:
        LEA $80xxxx, A0     ; source
        LEA $3000.W, A1     ; dest
        LEA $80yyyy, A2     ; end
        copy loop
    """
    # Look for LEA $3000.W at offset $2044
    off = 0x2044
    word = read16(data, off)
    if word == 0x43F8:  # LEA (xxx).W, A1
        dest = read16(data, off + 2)
        if dest == 0x3000:
            # Previous instruction is LEA (xxx).L, A0
            src_word = read16(data, off - 6)
            if (src_word & 0xF1FF) == 0x41F9:
                src_addr = read32(data, off - 4)
                return src_addr
    # Fallback: try common values
    # Check retail first ($8084A6)
    off_84a6 = 0x84A6
    if off_84a6 + 12 < len(data):
        w = read16(data, off_84a6)
        if (w & 0xFF00) == 0x6000:  # BRA.B or BRA.W
            return ROM_BASE + off_84a6
    # Check developer ($8084FC)
    off_84fc = 0x84FC
    if off_84fc + 12 < len(data):
        w = read16(data, off_84fc)
        if (w & 0xFF00) == 0x6000:
            return ROM_BASE + off_84fc
    return None


def parse_jump_table(data, jt_rom_base):
    """Parse all 18+ jump table entries."""
    jt_file_off = jt_rom_base - ROM_BASE
    entries = {}

    for i in range(20):
        ram_addr = JUMP_TABLE_RAM + i * 6
        file_off = jt_file_off + i * 6
        if file_off + 6 > len(data):
            break

        w1 = read16(data, file_off)

        if w1 == 0x6000:  # BRA.W
            disp = struct.unpack('>h', data[file_off+2:file_off+4])[0]
            target_ram = ram_addr + 2 + disp
            target_rom = jt_rom_base + (target_ram - JUMP_TABLE_RAM)
            entries[i] = (ram_addr, target_ram, target_rom)
        elif (w1 & 0xFF00) == 0x6000 and (w1 & 0xFF) != 0:  # BRA.B
            disp = struct.unpack('b', bytes([w1 & 0xFF]))[0]
            target_ram = ram_addr + 2 + disp
            target_rom = jt_rom_base + (target_ram - JUMP_TABLE_RAM)
            entries[i] = (ram_addr, target_ram, target_rom)
        else:
            break  # End of table

    return entries


def disasm_routine(data, rom_addr, max_bytes=512):
    """Disassemble a 68K routine until RTS/RTE."""
    md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
    md.detail = True

    file_off = rom_addr - ROM_BASE
    if file_off < 0 or file_off >= len(data):
        return []

    code = data[file_off:file_off + max_bytes]
    result = []
    for insn in md.disasm(code, rom_addr):
        result.append(insn)
        if insn.mnemonic.lower() in ('rts', 'rte'):
            break
    return result


def print_disasm(instructions, jt_rom_base=None):
    """Print disassembled instructions with RAM address annotation."""
    for insn in instructions:
        hex_bytes = ' '.join(f'{b:02X}' for b in insn.bytes)
        ram_str = ""
        if jt_rom_base:
            ram = JUMP_TABLE_RAM + (insn.address - jt_rom_base)
            ram_str = f"RAM ${ram:06X} | "
        print(f"  {ram_str}${insn.address:06X}: {hex_bytes:<30s} {insn.mnemonic:<10s} {insn.op_str}")


def analyze_bios(path):
    """Full BIOS analysis."""
    data = load_bios(path)
    basename = os.path.basename(path)
    print(f"BIOS: {basename}")
    print(f"Size: {len(data)} bytes (0x{len(data):X})")

    jt_rom = find_jt_rom_base(data)
    if not jt_rom:
        print("ERROR: Could not find jump table ROM base")
        return

    print(f"Jump table ROM base: ${jt_rom:06X}")
    entries = parse_jump_table(data, jt_rom)
    print(f"Entries found: {len(entries)}")
    print()

    # Print jump table
    print("=" * 70)
    print("JUMP TABLE")
    print("=" * 70)
    for idx, (ram_addr, target_ram, target_rom) in sorted(entries.items()):
        name = ENTRY_NAMES.get(idx, f"entry_{idx}")
        print(f"  ${ram_addr:04X}  [{idx:2d}]  {name:30s}  -> RAM ${target_ram:06X}  ROM ${target_rom:06X}")

    # Disassemble key entries
    key_entries = [
        (0, 256, "Entry 0: CD_setup_audio_isr -- Sets up GPU ISR for audio CD mode"),
        (3, 128, "Entry 3: CD_i2s_enable -- Enable/disable I2S + FIFO"),
        (5, 128, "Entry 5: CD_stop_drive -- Send STOP command to drive"),
        (6, 128, "Entry 6: CD_set_volume_mute -- Set volume to 0"),
        (7, 128, "Entry 7: CD_set_volume_max -- Set volume to max"),
        (8, 128, "Entry 8: CD_pause -- Pause playback"),
        (9, 128, "Entry 9: CD_unpause -- Unpause playback"),
        (10, 512, "Entry 10: CD_read -- *** Main CD read function ***"),
        (11, 128, "Entry 11: CD_fifo_disable -- Disable I2S FIFO"),
        (12, 128, "Entry 12: CD_hw_reset -- Hardware reset of BUTCH/I2S"),
        (13, 128, "Entry 13: CD_poll -- *** Poll transfer progress ***"),
        (14, 128, "Entry 14: CD_set_dac_mode -- Set DAC oversampling"),
        (15, 512, "Entry 15: CD_read_toc -- Read table of contents"),
        (16, 256, "Entry 16: CD_setup_cdrom_isr -- Sets up GPU ISR for CD-ROM mode"),
        (17, 256, "Entry 17: CD_setup_data_isr -- Sets up GPU ISR variant"),
    ]

    for idx, max_bytes, desc in key_entries:
        if idx not in entries:
            continue
        ram_addr, target_ram, target_rom = entries[idx]
        print()
        print("=" * 70)
        print(desc)
        print("=" * 70)
        instructions = disasm_routine(data, target_rom, max_bytes)
        print_disasm(instructions, jt_rom)

    # Also disassemble the DSA_tx_wait subroutine (called by CD_read)
    # It's right after CD_read ends
    if 10 in entries:
        _, _, cd_read_rom = entries[10]
        cd_read_insns = disasm_routine(data, cd_read_rom, 512)
        if cd_read_insns:
            last = cd_read_insns[-1]
            # Find the BSR target near the end
            for insn in cd_read_insns:
                if insn.mnemonic.lower() == 'bsr':
                    try:
                        target = int(insn.op_str.replace('$', '').replace('#', ''), 16)
                        print()
                        print("=" * 70)
                        print(f"DSA_tx_wait subroutine at ${target:06X}")
                        print("=" * 70)
                        sub_insns = disasm_routine(data, target, 128)
                        print_disasm(sub_insns, jt_rom)
                        break
                    except ValueError:
                        pass

    # Print calling convention summary
    print()
    print("=" * 70)
    print("CALLING CONVENTION SUMMARY")
    print("=" * 70)
    print("""
CD_read ($303C / entry 10):
  INPUTS:
    D0.L = MSF seek position + flags
           Bit 31: if set, skip hardware init, just re-seek
           Bits 23:16: minutes (hex, NOT BCD)
           Bits 15:8:  seconds (hex)
           Bits 7:0:   frames (hex)
    A0.L = Destination buffer address (decremented by 4 internally;
           GPU ISR pre-increments before storing)
    A1.L = Transfer size in bytes (stored to GPU data area [+4])
    D1.L = Secondary param (CD-ROM mode: stored to GPU data [+16])
    D2.L = Speed/mode (CD-ROM mode only: patches GPU ISR MOVEI opcodes
           at GPU_RAM+$BC and +$C4)

  OUTPUTS: none (asynchronous -- data arrives via GPU ISR)

  FLOW:
    1. If D0 bit 31 clear (full init):
       a. Disable BUTCH interrupts (clear low 16 bits of $DFFF00)
       b. Clear Jerry external int ($F10020 = $0101)
       c. Disable I2S FIFO (clear bit 2 of $DFFF10)
       d. Store A0 -> GPU_DATA[+0], A1 -> GPU_DATA[+4], 0 -> GPU_DATA[+8]
       e. If CD-ROM mode ([$3072] bit 7): patch GPU ISR with D2, store D1
       f. Drain FIFO until empty
       g. Enable BUTCH (master + FIFO half-full IRQ: $DFFF00 |= $21)
    2. Send DSA seek commands (always, regardless of bit 31):
       $10MM -> DS_DATA ($DFFF0A) -- goto minutes
       $11SS -> DS_DATA           -- goto seconds
       $12FF -> DS_DATA           -- goto frames (triggers seek)
    3. RTS (data arrives asynchronously via GPU ISR)

CD_poll ($304E / entry 13):
  INPUTS: none

  OUTPUTS:
    A0.L = Current RAM write pointer (updated by GPU ISR)
    A1.L = Bytes transferred so far (from GPU data area [+8])

  The GPU data area base is at [$3074] (set by entry 0/16/17).
  CD_poll reads:
    A0 = [[$3074] + 0]   ; current write pointer
    A1 = [[$3074] + 8]   ; bytes transferred

GPU DATA AREA (address in [$3074]):
  +$00: Current RAM write pointer (live, updated by GPU ISR)
  +$04: Transfer limit (A1 from CD_read)
  +$08: Bytes transferred counter
  +$0C: Mode flags ($10 in CD-ROM mode)
  +$10: D1 parameter from CD_read

MSF FORMAT:
  D0 = 0x00MMSSFF  (minutes << 16 | seconds << 8 | frames)
  Values are hex (NOT BCD): e.g., 75 frames/sec = $4B, 60 sec/min = $3C
  The BIOS MSF converter at $80313E subtracts 6 frames before seeking.
""")


def main():
    default_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "roms", "private", "[BIOS] Atari Jaguar CD (World).j64"
    )

    path = sys.argv[1] if len(sys.argv) > 1 else default_path

    if not os.path.exists(path):
        print(f"ERROR: BIOS file not found: {path}")
        sys.exit(1)

    analyze_bios(path)


if __name__ == '__main__':
    main()
