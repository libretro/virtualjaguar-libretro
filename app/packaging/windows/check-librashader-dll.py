#!/usr/bin/env python3
"""Check a Windows librashader.dll the way the app will meet it (issue #1153).

    check-librashader-dll.py <librashader.dll> --arch x64|arm64 [--require-load] [--no-load]

Static checks (any host, stdlib only, no pefile):
  * it is a PE image for the requested CPU (x64 = IMAGE_FILE_MACHINE_AMD64, arm64 = ARM64);
  * every DLL in its import table ships with Windows itself.  This is what decides whether
    LoadLibrary succeeds on a stock install: upstream's prebuilt v0.12.0 DLL imports D3DX9_43.dll
    (the June 2010 DirectX SDK redistributable, absent from Windows 10/11 and from GitHub's
    runners) and the VC++ 2015-2022 redistributable (VCRUNTIME140.dll, MSVCP140.dll), which is
    why `shaders_library` skipped on the v3.11.0 Windows legs.  Delay-loaded imports are only
    printed: they do not run at load time.
  * it exports the librashader C API the app resolves (ABI probe + the OpenGL filter chain).

Load check (Windows host of the same CPU; otherwise reported as skipped, a failure with
--require-load): LoadLibrary it, call libra_instance_abi_version() (must be 2, what the app's
headers were built against) and print libra_instance_api_version().

Exit status 0 when every check passed, 1 otherwise; problems are printed as ::error:: lines so
GitHub Actions annotates them.
"""
import argparse
import ctypes
import fnmatch
import os
import platform
import struct
import sys

MACHINE = {0x8664: "x64", 0xAA64: "arm64", 0x14C: "x86", 0x1C4: "arm"}
EXPECTED_ABI = 2
# What the app resolves (src/shaders.cpp): the version probe first, then the OpenGL chain.
REQUIRED_EXPORTS = [
    "libra_instance_abi_version", "libra_instance_api_version",
    "libra_preset_create_with_options", "libra_preset_free",
    "libra_gl_filter_chain_create", "libra_gl_filter_chain_frame", "libra_gl_filter_chain_free",
]
# DLLs every supported Windows (10 and 11) has.  The Universal CRT (ucrtbase + api-ms-win-crt-*)
# has been an OS component since Windows 10, so it is allowed; the VC++ runtime is not.
SYSTEM_DLLS = {
    "kernel32.dll", "kernelbase.dll", "ntdll.dll", "advapi32.dll", "user32.dll", "shell32.dll",
    "ole32.dll", "oleaut32.dll", "combase.dll", "ws2_32.dll", "userenv.dll", "bcrypt.dll",
    "bcryptprimitives.dll", "synchronization.dll", "ucrtbase.dll", "gdi32.dll", "shlwapi.dll",
    "version.dll", "crypt32.dll", "secur32.dll", "iphlpapi.dll", "psapi.dll", "dbghelp.dll",
    "d3dcompiler_47.dll",   # in System32 since Windows 8.1; harmless, though the OpenGL-only build has no use for it
}
SYSTEM_DLL_GLOBS = ["api-ms-win-core-*.dll", "api-ms-win-crt-*.dll", "ext-ms-win-*.dll"]
# Known non-system imports, with the reason they would break a stock install.
KNOWN_BAD = [
    ("d3dx9_*.dll", "the June 2010 DirectX SDK redistributable, not part of Windows: build without librashader's runtime-d3d9"),
    ("vcruntime*.dll", "the VC++ redistributable, not part of Windows: build with -C target-feature=+crt-static"),
    ("msvcp*.dll", "the VC++ redistributable, not part of Windows: build with -C target-feature=+crt-static"),
    ("msvcr*.dll", "an old VC++ redistributable, not part of Windows"),
    ("concrt*.dll", "the VC++ redistributable, not part of Windows"),
    ("dxcompiler.dll", "the DirectX Shader Compiler, not part of Windows; the OpenGL-only build does not need it"),
    ("d3d1*.dll", "a Direct3D runtime the OpenGL-only build does not need"),
    ("vulkan-1.dll", "the Vulkan loader, which the OpenGL-only build does not need"),
    ("libstdc++*.dll", "the MinGW C++ runtime: link it statically"),
    ("libgcc_s*.dll", "the MinGW runtime: link it statically"),
    ("libwinpthread*.dll", "the MinGW runtime: link it statically"),
    ("libc++.dll", "the MinGW/LLVM C++ runtime: link it statically"),
    ("libunwind.dll", "the MinGW/LLVM runtime: link it statically"),
]


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE image (no MZ header)")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("not a PE image (no PE signature)")
        coff = e_lfanew + 4
        self.machine, nsections, _, _, _, opt_size, self.characteristics = struct.unpack_from("<HHIIIHH", data, coff)
        opt = coff + 20
        magic = struct.unpack_from("<H", data, opt)[0]
        if magic == 0x20B:      # PE32+
            ndirs = struct.unpack_from("<I", data, opt + 108)[0]
            dirs = opt + 112
        elif magic == 0x10B:    # PE32
            ndirs = struct.unpack_from("<I", data, opt + 92)[0]
            dirs = opt + 96
        else:
            raise ValueError("unknown optional header magic 0x%x" % magic)
        self.dirs = [struct.unpack_from("<II", data, dirs + 8 * i) for i in range(ndirs)]
        sec = opt + opt_size
        self.sections = []
        for i in range(nsections):
            name, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, sec + 40 * i)
            self.sections.append((name.rstrip(b"\0").decode("ascii", "replace"), vsize, vaddr, rawsize, rawptr))

    def off(self, rva):
        for _, vsize, vaddr, rawsize, rawptr in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rva - vaddr + rawptr
        raise ValueError("RVA 0x%x is in no section" % rva)

    def cstr(self, rva):
        o = self.off(rva)
        end = self.data.index(b"\0", o)
        return self.data[o:end].decode("ascii", "replace")

    def directory(self, index):
        if index < len(self.dirs) and self.dirs[index][0] and self.dirs[index][1]:
            return self.dirs[index]
        return None

    def imports(self):
        d = self.directory(1)
        if not d:
            return []
        out, o = [], self.off(d[0])
        while True:
            ilt, _, _, name, iat = struct.unpack_from("<IIIII", self.data, o)
            if not (ilt or name or iat):
                break
            out.append(self.cstr(name))
            o += 20
        return out

    def delay_imports(self):
        d = self.directory(13)
        if not d:
            return []
        out, o = [], self.off(d[0])
        while True:
            attrs, name = struct.unpack_from("<II", self.data, o)
            if not name:
                break
            # Attributes bit 0: RVAs (the only form a 64-bit image has); older images used VAs.
            out.append(self.cstr(name) if attrs & 1 else "<VA-form delay import>")
            o += 32
        return out

    def exports(self):
        d = self.directory(0)
        if not d:
            return []
        o = self.off(d[0])
        nnames, names_rva = struct.unpack_from("<I", self.data, o + 24)[0], struct.unpack_from("<I", self.data, o + 32)[0]
        no = self.off(names_rva)
        return [self.cstr(struct.unpack_from("<I", self.data, no + 4 * i)[0]) for i in range(nnames)]


def is_system(name):
    n = name.lower()
    return n in SYSTEM_DLLS or any(fnmatch.fnmatch(n, g) for g in SYSTEM_DLL_GLOBS)


def why_bad(name):
    n = name.lower()
    for glob, why in KNOWN_BAD:
        if fnmatch.fnmatch(n, glob):
            return why
    return "not a DLL that ships with Windows"


def host_arch():
    m = platform.machine().lower()
    if m in ("amd64", "x86_64"):
        return "x64"
    if m in ("arm64", "aarch64"):
        return "arm64"
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll")
    ap.add_argument("--arch", required=True, choices=["x64", "arm64"])
    ap.add_argument("--no-load", action="store_true", help="static checks only")
    ap.add_argument("--require-load", action="store_true", help="a skipped load check is a failure")
    args = ap.parse_args()

    errors = []

    def error(msg):
        errors.append(msg)
        print("::error::%s" % msg)

    path = os.path.abspath(args.dll)
    print("checking %s" % path)
    if not os.path.isfile(path):
        error("%s is missing" % path)
        return 1
    if os.path.islink(path):
        error("%s is a symlink; packages must carry the file" % path)
    size = os.path.getsize(path)
    print("size: %d bytes" % size)
    with open(path, "rb") as f:
        data = f.read()
    try:
        pe = PE(data)
    except ValueError as e:
        error("%s: %s" % (path, e))
        return 1

    machine = MACHINE.get(pe.machine, "0x%04x" % pe.machine)
    print("machine: %s (want %s)" % (machine, args.arch))
    if machine != args.arch:
        error("%s is a %s image, expected %s" % (path, machine, args.arch))
    if not pe.characteristics & 0x2000:
        error("%s is not a DLL (IMAGE_FILE_DLL not set)" % path)

    imports = pe.imports()
    print("imports (%d):" % len(imports))
    for name in sorted(imports, key=str.lower):
        ok = is_system(name)
        print("  %s%s" % (name, "" if ok else "   <-- " + why_bad(name)))
        if not ok:
            error("%s imports %s: %s; LoadLibrary would fail on a stock Windows" % (os.path.basename(path), name, why_bad(name)))
    delayed = pe.delay_imports()
    if delayed:
        print("delay-loaded (not needed to load the DLL): %s" % ", ".join(sorted(delayed, key=str.lower)))

    exports = pe.exports()
    libra = [e for e in exports if e.startswith("libra_")]
    print("exports: %d, %d of them libra_*" % (len(exports), len(libra)))
    for name in REQUIRED_EXPORTS:
        if name not in exports:
            error("%s does not export %s" % (os.path.basename(path), name))

    if args.no_load:
        print("load: skipped (--no-load)")
    elif os.name != "nt":
        print("load: skipped (not a Windows host)")
        if args.require_load:
            error("--require-load on a non-Windows host")
    elif host_arch() != args.arch:
        print("load: skipped (this host is %s, the DLL is %s)" % (host_arch(), args.arch))
        if args.require_load:
            error("--require-load but this %s host cannot load a %s DLL" % (host_arch(), args.arch))
    elif errors:
        print("load: skipped (static checks failed)")
    else:
        try:
            # SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX: a bad image must fail, not pop a dialog.
            ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x8000)
            lib = ctypes.CDLL(path)
            lib.libra_instance_abi_version.restype = ctypes.c_size_t
            lib.libra_instance_api_version.restype = ctypes.c_size_t
            abi, api = lib.libra_instance_abi_version(), lib.libra_instance_api_version()
            print("load: OK, librashader ABI %d, API %d" % (abi, api))
            if abi != EXPECTED_ABI:
                error("%s has librashader ABI %d; the app needs %d" % (os.path.basename(path), abi, EXPECTED_ABI))
        except OSError as e:
            error("LoadLibrary(%s) failed: %s" % (path, e))

    print("check-librashader-dll: %s" % ("FAIL (%d problem%s)" % (len(errors), "s" if len(errors) != 1 else "") if errors else "OK"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
