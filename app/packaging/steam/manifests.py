#!/usr/bin/env python3
"""Generate SteamPipe scripts for three already-qualified desktop payloads."""
import argparse
import json
import os
from pathlib import Path


def quoted(value):
    value = str(value)
    if any(ord(c) < 32 for c in value):
        raise ValueError("Control characters are not allowed in SteamPipe values")
    return json.dumps(value, ensure_ascii=False)


def positive_id(value):
    if not value.isascii() or not value.isdigit() or int(value) <= 0:
        raise argparse.ArgumentTypeError("Use the positive numeric ID assigned by Steamworks")
    return str(int(value))


def inside(path, parent):
    return path == parent or parent in path.parents


def validate_payload(platform, root):
    required = {
        "windows": ["virtualjaguar.exe", "LICENSE.txt", "THIRD-PARTY-NOTICES.txt"],
        "macos": ["Virtual Jaguar.app/Contents/MacOS/virtualjaguar",
                  "Virtual Jaguar.app/Contents/Resources/LICENSE.txt",
                  "Virtual Jaguar.app/Contents/Resources/THIRD-PARTY-NOTICES.txt"],
        "linux": ["AppRun", "usr/bin/virtualjaguar",
                  "usr/share/doc/virtualjaguar/LICENSE.txt",
                  "usr/share/doc/virtualjaguar/THIRD-PARTY-NOTICES.txt"],
    }
    for name in required[platform]:
        if not (root / name).is_file():
            raise ValueError(f"{platform}: missing {name}")
    if os.name != "nt" and platform in ("linux", "macos"):
        launchers = (["AppRun", "usr/bin/virtualjaguar"] if platform == "linux" else
                     ["Virtual Jaguar.app/Contents/MacOS/virtualjaguar"])
        for name in launchers:
            if not os.access(root / name, os.X_OK):
                raise ValueError(f"{platform}: launcher is not executable: {name}")
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        # Data, credentials and portable mode must never become depot content.
        if any(part.lower() in {".git", ".steam", "portable.txt", "config.vdf",
                                "loginusers.vdf", "ssfn", ".env"}
               or part.lower().startswith("ssfn") for part in relative.parts):
            raise ValueError(f"{platform}: forbidden depot content: {relative}")
        if path.is_symlink() and not inside(path.resolve(strict=True), root):
            raise ValueError(f"{platform}: symlink leaves the payload: {relative}")


def generate(app_id, depot_ids, roots, output, description, upload=False):
    if len(set([app_id, *depot_ids.values()])) != 4:
        raise ValueError("The app and all three depot IDs must be distinct")
    output = output.resolve()
    roots = {platform: root.resolve(strict=True) for platform, root in roots.items()}
    if output.exists():
        raise ValueError("Use a fresh output directory")
    for platform, root in roots.items():
        if inside(output, root) or inside(root, output):
            raise ValueError("Scripts/output must be separate from depot payloads")
        for other_platform, other in roots.items():
            if platform != other_platform and (inside(root, other) or inside(other, root)):
                raise ValueError("Each OS needs its own non-overlapping payload")
        validate_payload(platform, root)
    # Validate strings before writing anything.
    quoted(description)
    for root in roots.values():
        quoted(root.as_posix())
    output.mkdir(parents=True)
    depots = []
    for platform, root in roots.items():
        depot_id = depot_ids[platform]
        filename = f"depot_build_{depot_id}.vdf"
        (output / filename).write_text(
            '"DepotBuild"\n{\n'
            f'  "DepotID" {quoted(depot_id)}\n'
            f'  "ContentRoot" {quoted(root.as_posix())}\n'
            '  "FileMapping"\n  {\n'
            '    "LocalPath" "*"\n    "DepotPath" "."\n    "Recursive" "1"\n'
            '  }\n}\n', encoding="utf-8")
        depots.append(f"    {quoted(depot_id)} {quoted(filename)}")
    (output / f"app_build_{app_id}.vdf").write_text(
        '"AppBuild"\n{\n'
        f'  "AppID" {quoted(app_id)}\n  "Desc" {quoted(description)}\n'
        f'  "BuildOutput" {quoted((output / "build-output").as_posix())}\n'
        f'  "Preview" "{0 if upload else 1}"\n'
        '  "SetLive" ""\n  "Depots"\n  {\n' + "\n".join(depots) + '\n  }\n}\n',
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-id", type=positive_id, required=True)
    for platform in ("windows", "macos", "linux"):
        parser.add_argument(f"--{platform}-depot", type=positive_id, required=True)
        parser.add_argument(f"--{platform}-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--description", required=True)
    parser.add_argument("--upload", action="store_true",
                        help="Generate upload scripts instead of local preview scripts; does not run SteamCMD")
    args = parser.parse_args()
    try:
        generate(args.app_id,
                 {os: getattr(args, f"{os}_depot") for os in ("windows", "macos", "linux")},
                 {os: getattr(args, f"{os}_root") for os in ("windows", "macos", "linux")},
                 args.output, args.description, args.upload)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
