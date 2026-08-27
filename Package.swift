// swift-tools-version:5.9
//
// Swift Package Manager manifest for the Virtual Jaguar libretro core.
//
// This exists so Apple frontends can consume the core as a normal package
// dependency instead of vendoring it as a submodule and re-declaring every
// source file in their own manifest.  It builds the C core ONLY -- no Swift,
// no frontend glue.  Whatever wraps it supplies that.
//
// It is inert for every other build system.  `make`, `jni/Android.mk` and the
// MSVC projects never read this file, and nothing here changes a flag they use.
//
// ---------------------------------------------------------------------------
// The source lists below MIRROR Makefile.common, which stays the single source
// of truth.  scripts/check-package-sources.sh diffs the two and CI fails on
// divergence -- hand-maintaining a second copy is exactly how the downstream
// wrapper drifted by 27 files over seven releases.  If you add a .c file to
// Makefile.common, add it here too, or CI will tell you.
// ---------------------------------------------------------------------------
//
// Target names are prefixed.  SPM requires them to be unique across the WHOLE
// package graph of the consuming app, and generic names collide: a frontend
// carrying two cores that each vendor CHD cannot build if both call the target
// "libchdr".  That is not hypothetical -- it happened.

import PackageDescription

// Mirrors SOURCES_C under $(CORE_DIR) in Makefile.common.
let coreSources: [String] = [
    "src/bios/jagbios.c",
    "src/bios/jagbios_m.c",
    "src/bios/jagcdbios.c",
    "src/bios/jagdevcdbios.c",
    "src/cd/cdintf.c",
    "src/cd/cdrom.c",
    "src/cd/jagcd_bios.c",
    "src/cd/jagcd_cart.c",
    "src/cd/jagcd_hle.c",
    "src/core/biosdb.c",
    "src/core/bus_arbiter.c",
    "src/core/cheat.c",
    "src/core/crash_detect.c",
    "src/core/crc32.c",
    "src/core/event.c",
    "src/core/file.c",
    "src/core/filedb.c",
    "src/core/jaggd.c",
    "src/core/jaguar.c",
    "src/core/memtrack.c",
    "src/core/nvmbios.c",
    "src/core/perf_counters.c",
    "src/core/perf_iface.c",
    "src/core/settings.c",
    "src/core/titledb.c",
    "src/core/titlehook.c",
    "src/core/universalhdr.c",
    "src/core/vjag_memory.c",
    "src/core/vjtrace.c",
    "src/jerry/axistune.c",
    "src/jerry/dac.c",
    "src/jerry/dsp.c",
    "src/jerry/eeprom.c",
    "src/jerry/inputdev.c",
    "src/jerry/jerry.c",
    "src/jerry/jlink.c",
    "src/jerry/jlink_discover.c",
    "src/jerry/jlink_netpacket.c",
    "src/jerry/jlink_tcp.c",
    "src/jerry/joystick.c",
    "src/jerry/paddle.c",
    "src/jerry/quadrature.c",
    "src/jerry/uart.c",
    "src/jerry/voicechat.c",
    "src/jerry/voicemodem.c",
    "src/jerry/wavetable.c",
    "src/m68000/cpudefs.c",
    "src/m68000/cpuemu.c",
    "src/m68000/cpuextra.c",
    "src/m68000/cpustbl.c",
    "src/m68000/m68kinterface.c",
    "src/m68000/readcpu.c",
    "src/tom/blit_memo.c",
    "src/tom/blitter.c",
    "src/tom/blitter_compare.c",
    "src/tom/blitter_mmio.c",
    "src/tom/blitter_simd_neon.c",
    "src/tom/blitter_simd_scalar.c",
    "src/tom/blitter_simd_sse2.c",
    "src/tom/gpu.c",
    "src/tom/op.c",
    "src/tom/shadowfb.c",
    "src/tom/texdump.c",
    "src/tom/texreplace.c",
    "src/tom/tom.c",
    "libretro.c",
]

// Mirrors SOURCES_C under $(LIBRETRO_COMM_DIR) in Makefile.common.
let libretroCommonSources: [String] = [
    "compat/compat_posix_string.c",
    "compat/compat_snprintf.c",
    "compat/compat_strcasestr.c",
    "compat/compat_strl.c",
    "compat/fopen_utf8.c",
    "encodings/encoding_utf.c",
    "file/file_path.c",
    "file/file_path_io.c",
    "streams/file_stream.c",
    "streams/file_stream_transforms.c",
    "string/stdstring.c",
    "time/rtime.c",
    "vfs/vfs_implementation.c",
]

// Defines Makefile.common passes to every core translation unit.
//
// BLITTER_SIMD_AUTODETECT is the one that is NOT in the Makefile, and it is
// load-bearing.  Makefile.common compiles exactly ONE blitter_simd_<arch>.c and
// passes a matching -DBLITTER_SIMD_<ARCH>; SPM has no per-target source
// selection, so it compiles all three and each guards itself via
// src/tom/blitter_simd_arch.h.  Without this define blitter_simd.h would fall
// through to its scalar branch and inline the SCALAR ops into the blitter hot
// path on hardware that has NEON -- silently, because the blitter_simd_ops
// vtable would still be NEON and only the core's own tests read that vtable.
let coreDefines: [CSetting] = [
    .define("__LIBRETRO__", to: "1"),
    .define("INLINE", to: "inline"),
    .define("BLITTER_SIMD_AUTODETECT", to: "1"),
]

let package = Package(
    name: "VirtualJaguar",
    products: [
        .library(name: "VirtualJaguar", targets: ["virtualjaguar"]),
    ],
    targets: [
        // The core.  path "." because libretro.c sits at the repo root and SPM
        // requires sources to live under the target path; everything else is
        // under src/.  The explicit `sources` list means SPM compiles only what
        // is named, so the rest of the repo (docs, tests, scripts) is ignored.
        .target(
            name: "virtualjaguar",
            dependencies: ["virtualjaguar-libretro-common", "virtualjaguar-libchdr"],
            path: ".",
            // libretro-common and deps are owned by the other two targets;
            // SPM rejects overlapping target paths without excluding them.
            //
            // tools/ is here for a different reason: SPM scans the whole target
            // path for RESOURCES independently of the `sources` list above, and
            // tools/vendor/jaguar-toolchain (fetched by tools/jaguar-toolchain/
            // setup.sh, gitignored) ships a macOS helper .app carrying an
            // English.lproj.  SPM reads that as a localized resource and fails
            // the manifest with "defaultLocalization not set" -- so `swift
            // build` breaks for anyone who has fetched the Jaguar toolchain,
            // and only for them.
            exclude: ["libretro-common", "deps", "tools"],
            sources: coreSources,
            publicHeadersPath: "src/core",
            cSettings: coreDefines + [
                .headerSearchPath("."),
                .headerSearchPath("src"),
                .headerSearchPath("src/core"),
                .headerSearchPath("src/tom"),
                .headerSearchPath("src/jerry"),
                .headerSearchPath("src/cd"),
                .headerSearchPath("src/bios"),
                .headerSearchPath("src/m68000"),
                .headerSearchPath("libretro-common/include"),
                // src/cd/cdintf.c includes <libchdr/chd.h> unconditionally.
                .headerSearchPath("deps/libchdr/include"),
            ]
        ),

        .target(
            name: "virtualjaguar-libretro-common",
            path: "libretro-common",
            sources: libretroCommonSources,
            publicHeadersPath: "include",
            cSettings: coreDefines + [
                .headerSearchPath("include"),
            ]
        ),

        // Vendored CHD reader, built the way Makefile.common builds it: one
        // unity translation unit, with miniz's compressor left on because
        // src/tom/texdump.c needs tdefl_write_image_to_png_file_in_memory_ex
        // for its preview PNGs.
        //
        // No -std=c99 as the Makefile passes: SPM compiles C as gnu11, a
        // superset, so the flag is unnecessary rather than missing.
        .target(
            name: "virtualjaguar-libchdr",
            path: "deps/libchdr",
            sources: ["unity.c"],
            publicHeadersPath: "include",
            cSettings: [
                .define("_7ZIP_ST", to: "1"),
                .define("MINIZ_DEFLATE_APIS", to: "1"),
                .define("WANT_RAW_DATA_SECTOR", to: "1"),
                .define("WANT_SUBCODE", to: "1"),
                .define("VERIFY_BLOCK_CRC", to: "1"),
                .headerSearchPath("include"),
                .headerSearchPath("deps/lzma-25.01/include"),
                .headerSearchPath("deps/miniz-3.1.1"),
                .headerSearchPath("deps/zstd-1.5.7"),
                // No .unsafeFlags here, deliberately. SwiftPM REFUSES to resolve a
                // versioned dependency (`from: "x.y.z"`) on any package whose
                // targets carry unsafe flags -- only path and branch/revision
                // dependencies are exempt. That would break the exact workflow
                // this manifest exists to enable, and an in-repo `swift build`
                // cannot detect it because the root package is exempt too.
                // The flags only silenced unused-function/variable warnings in
                // the unity TU; warnings are not worth that. The spm-consumer CI
                // job resolves this package by version so the restriction is
                // tested rather than remembered.
            ]
        ),
    ],
    cLanguageStandard: .gnu11
)
