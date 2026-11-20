#!/usr/bin/env python3
"""Reject mislabeled/non-production Windows ARM64 cores without loading a DLL."""
import struct
import sys
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    assert data[:2] == b'MZ', 'not a PE file'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0', 'bad PE signature'
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    assert machine == 0xaa64, f'expected ARM64 (0xaa64), got {machine:#x}'
    opt = pe + 24
    assert struct.unpack_from('<H', data, opt)[0] == 0x20b, 'expected PE32+'
    sections = opt + struct.unpack_from('<H', data, pe + 20)[0]

    def offset(rva):
        for i in range(count):
            size, address, raw_size, raw = struct.unpack_from('<IIII', data, sections + i * 40 + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise AssertionError(f'unmapped RVA {rva:#x}')

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')

    export_rva = struct.unpack_from('<I', data, opt + 112)[0]
    exports = offset(export_rva)
    length = struct.unpack_from('<I', data, exports + 24)[0]
    names = struct.unpack_from('<I', data, exports + 32)[0]
    names = offset(names)
    exported = {string(struct.unpack_from('<I', data, names + i * 4)[0]) for i in range(length)}
    assert exported and all(n.startswith('retro_') for n in exported), exported
    required = {
        'retro_init', 'retro_deinit', 'retro_api_version', 'retro_get_system_info',
        'retro_get_system_av_info', 'retro_set_environment', 'retro_set_video_refresh',
        'retro_set_audio_sample', 'retro_set_audio_sample_batch', 'retro_set_input_poll',
        'retro_set_input_state', 'retro_set_controller_port_device', 'retro_reset',
        'retro_run', 'retro_serialize_size', 'retro_serialize', 'retro_unserialize',
        'retro_cheat_reset', 'retro_cheat_set', 'retro_load_game', 'retro_load_game_special',
        'retro_unload_game', 'retro_get_region', 'retro_get_memory_data', 'retro_get_memory_size',
    }
    assert required <= exported, f'missing libretro exports: {required - exported}'
    imported = []
    import_rva = struct.unpack_from('<I', data, opt + 120)[0]
    if import_rva:
        entry = offset(import_rva)
        while any(data[entry:entry + 20]):
            imported.append(string(struct.unpack_from('<I', data, entry + 12)[0]).lower())
            entry += 20
    assert not any(n.startswith(('libgcc', 'libstdc++', 'libc++', 'libunwind', 'libwinpthread')) for n in imported), imported
    print(f'{path}: ARM64 PE32+, {len(exported)} production retro_* exports, imports {imported}')


if __name__ == '__main__':
    check(sys.argv[1])
