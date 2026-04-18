#!/usr/bin/env python3
"""
Headless test runner for the virtualjaguar libretro core.

Drives the built `virtualjaguar_libretro.dylib` (or .so/.dll) via
JesseTG/libretro.py — a Python binding designed for testing libretro cores.
This is a local equivalent of running the core in RetroArch, but completely
headless, deterministic, and scriptable. Use it instead of round-tripping
test logs through a phone or desktop frontend.

Setup (one-time):
    python3.12 -m venv .venv-libretropy
    source .venv-libretropy/bin/activate
    pip install 'libretro.py[cli]'

Usage:
    source .venv-libretropy/bin/activate
    python test/headless.py <content.cue|.j64|.cdi|.iso> [--frames N] [--cd-bios retail|dev]

The core is auto-detected from the repo root. The system_dir defaults to
test/roms/private/ (where BIOSes are kept). Adjust via --system-dir.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CORE_NAMES = {
    "darwin": "virtualjaguar_libretro.dylib",
    "linux":  "virtualjaguar_libretro.so",
    "win32":  "virtualjaguar_libretro.dll",
}


def detect_core() -> Path:
    name = CORE_NAMES.get(sys.platform, "virtualjaguar_libretro.so")
    candidate = REPO_ROOT / name
    if not candidate.exists():
        sys.exit(f"Core not found at {candidate}. Run `make` first.")
    return candidate


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("content", help="Path to game content (.cue, .j64, .cdi, .iso, etc.)")
    p.add_argument("--frames", type=int, default=600, help="Frames to run (default: 600)")
    p.add_argument("--cd-bios", choices=["retail", "dev"], default="retail",
                   help="CD BIOS variant (default: retail)")
    p.add_argument("--core", type=Path, default=None, help="Override core path")
    p.add_argument("--system-dir", type=Path, default=REPO_ROOT / "test" / "roms" / "private",
                   help="Directory containing BIOS files")
    p.add_argument("--save-dir", type=Path, default=Path("/tmp/vj_save"),
                   help="Directory for SRAM/save files")
    p.add_argument("--progress-every", type=int, default=60,
                   help="Print frame progress every N frames (0 = silent)")
    p.add_argument("--screenshot", type=Path, default=None,
                   help="Save final frame as PPM image to this path")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    try:
        from libretro import SessionBuilder
        from libretro.drivers import PathDriver
    except ImportError:
        sys.exit(
            "libretro.py is not installed. Set up a Python 3.12+ venv and run:\n"
            "    pip install 'libretro.py[cli]'"
        )

    core = args.core or detect_core()
    content = Path(args.content).resolve()
    if not content.exists():
        sys.exit(f"Content not found: {content}")

    args.save_dir.mkdir(parents=True, exist_ok=True)
    if not args.system_dir.exists():
        sys.exit(f"system_dir not found: {args.system_dir}")

    class FixedPathDriver(PathDriver):
        def __init__(self, system: Path, save: Path, corepath: Path):
            self._system = str(system).encode()
            self._save = str(save).encode()
            self._core = str(corepath).encode()

        @property
        def system_dir(self): return self._system
        @property
        def libretro_path(self): return self._core
        @property
        def core_assets_dir(self): return self._system
        @property
        def save_dir(self): return self._save
        @property
        def playlist_dir(self): return self._save
        @property
        def file_browser_start_dir(self): return self._system
        @property
        def content_dir(self): return self._system
        @property
        def username(self): return b"libretropy"
        @property
        def language(self): return None

    options = {
        "virtualjaguar_bios": "enabled",
        "virtualjaguar_usefastblitter": "enabled",
        "virtualjaguar_cd_bios_type": args.cd_bios,
    }

    paths = FixedPathDriver(args.system_dir, args.save_dir, core)
    builder = (
        SessionBuilder.defaults(str(core))
        .with_content(str(content))
        .with_options(options)
        .with_paths(paths)
    )

    print(f"Core:    {core}", file=sys.stderr)
    print(f"Content: {content}", file=sys.stderr)
    print(f"Frames:  {args.frames}", file=sys.stderr)

    with builder.build() as session:
        for i in range(args.frames):
            session.run()
            if args.progress_every and i % args.progress_every == 0:
                print(f"frame {i}", file=sys.stderr)

        if args.screenshot:
            shot = session.video.screenshot()
            if shot is None:
                print("No frame captured (core has not yet rendered).", file=sys.stderr)
            else:
                # PPM P6 = simple portable RGB. Strip alpha from ABGR.
                w, h = shot.width, shot.height
                with open(args.screenshot, "wb") as f:
                    f.write(f"P6\n{w} {h}\n255\n".encode())
                    pixels = bytearray(w * h * 3)
                    src = shot.data
                    for j in range(w * h):
                        # ArrayVideoDriver writes ABGR
                        pixels[j*3+0] = src[j*4+2]  # R from B
                        pixels[j*3+1] = src[j*4+1]  # G
                        pixels[j*3+2] = src[j*4+0]  # B from A? actually ABGR -> RGB
                    f.write(bytes(pixels))
                print(f"Screenshot saved: {args.screenshot} ({w}x{h})", file=sys.stderr)

    print(f"Done. Ran {args.frames} frames.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
