#!/usr/bin/env python3
# Stage the MSYS2 Mesa dependency closure and matching app copy for automatic fallback.
import os, pathlib, re, shutil, subprocess, sys
def windows_path(path):
    return pathlib.Path(subprocess.check_output(['cygpath', '-m', str(path)], text=True).strip())
source = windows_path(os.environ['MINGW_PREFIX'] + '/bin')
bindir = pathlib.Path(sys.argv[1]) / 'bin'
target = bindir / 'software-opengl'
if target.exists():
    raise SystemExit('Choose a fresh install prefix: software OpenGL is already staged')
target.mkdir()
objdump = shutil.which('llvm-objdump') or shutil.which('objdump')
if not objdump:
    raise SystemExit('MSYS2 objdump is required to stage the software renderer')
pending = ['opengl32.dll']
copied = set()
owners = set()
while pending:
    name = pending.pop()
    if name.lower() in copied:
        continue
    path = source / name
    if not path.exists():
        continue  # system DLL, not an MSYS2 dependency
    copied.add(name.lower())
    shutil.copy2(path, target / path.name)
    imports = subprocess.check_output([objdump, '-p', str(path)], text=True)
    pending.extend(re.findall(r'DLL Name:\s*(\S+)', imports))
    unix_path = subprocess.check_output(['cygpath', '-u', str(path)], text=True).strip()
    owners.add(subprocess.check_output(['pacman', '-Qqo', unix_path], text=True).strip())
assert (target / 'libgallium_wgl.dll').exists(), 'Mesa WGL driver missing'
notices = target / 'licenses'
for owner in sorted(owners):
    for line in subprocess.check_output(['pacman', '-Ql', owner], text=True).splitlines():
        unix_path = line.split(' ', 1)[1]
        if '/share/licenses/' not in unix_path or unix_path.endswith('/'):
            continue
        path = windows_path(unix_path)
        if path.is_file():
            dest = notices / owner / path.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
(target / 'PACKAGES.txt').write_text(subprocess.check_output(['pacman', '-Q', *sorted(owners)], text=True))
# WGL/GDI must find the Mesa frontend by its standard name next to the
# executable. Keep a separate full app copy so normal graphics stays clean.
for path in list(bindir.iterdir()):
    if path == target:
        continue
    if path.is_dir():
        shutil.copytree(path, target / path.name, dirs_exist_ok=True)
    else:
        shutil.copy2(path, target / path.name)

