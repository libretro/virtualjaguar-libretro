#!/usr/bin/env python3
"""Exercise a published candidate in an isolated user profile on disposable CI hosts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--sha', required=True)
parser.add_argument('--powershell', default='pwsh')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
root = Path(os.environ.get('RUNNER_TEMP', tempfile.gettempdir()))/'vj-published-qualification'
root.mkdir(exist_ok=False)
print('Evidence directory:', root, flush=True)
report = {'tag': args.tag, 'sha': args.sha, 'os': platform.platform(), 'checks': []}
env = dict(os.environ)
for key, value in {'HOME': root/'home', 'APPDATA': root/'roaming', 'LOCALAPPDATA': root/'local',
                   'XDG_DATA_HOME': root/'data', 'XDG_CONFIG_HOME': root/'config'}.items():
    value.mkdir(parents=True, exist_ok=True)
    env[key] = str(value)
env['VJ_INSTALL_NO_TTY'] = '1'
env['APPIMAGE_EXTRACT_AND_RUN'] = '1'
# No alternate release host or fake platform is allowed during qualification.
for key in ['VJ_INSTALL_BASE_URL', 'VJ_INSTALL_ALLOW_ANY_OS', 'VJ_INSTALL_RETROARCH']:
    env.pop(key, None)

def run(cmd, label):
    print(label, flush=True)
    proc = subprocess.run([str(c) for c in cmd], env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=600)
    (root/(label+'.log')).write_bytes(proc.stdout)
    if proc.returncode:
        print(proc.stdout.decode(errors='replace')[-6000:], flush=True)
        raise RuntimeError(f'{label}: exit {proc.returncode}')
    report['checks'].append(label)
    return proc.stdout.decode(errors='replace')

def snapshot(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}

headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'vj-package-qualification'}
token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
if token:
    headers['Authorization'] = 'Bearer '+token
request = urllib.request.Request('https://api.github.com/repos/libretro/virtualjaguar-libretro/releases/tags/'+args.tag, headers=headers)
with urllib.request.urlopen(request, timeout=60) as response:
    release = json.load(response)
if release['target_commitish'] != args.sha:
    raise RuntimeError('Published release does not identify the requested candidate SHA')
report['release_id'] = release['id']
windows = platform.system() == 'Windows'
for selection in ['core', 'app', 'both']:
    case = root/selection
    cores, apps = case/'cores', case/'apps'
    cores.mkdir(parents=True); apps.mkdir()
    if windows:
        cmd = [args.powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', repo/'tools/install.ps1',
               '-What', selection, '-Channel', args.tag, '-Yes', '-NoShortcut', '-CoreDir', cores, '-AppDest', apps]
        dry, force = '-DryRun', '-Force'
    else:
        cmd = ['sh', repo/'tools/install.sh', '--what', selection, '--channel', args.tag, '--yes',
               '--dest', cores, '--app-dest', apps]
        dry, force = '--dry-run', '--force'
    before = {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']}
    run(cmd+[dry], selection+'-dry-before')
    assert {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']} == before, 'dry-run changed install destinations'
    run(cmd, selection+'-install')
    if selection in ['core', 'both']:
        assert any(p.is_file() and p.stat().st_size > 100000 for p in cores.iterdir()), 'core missing'
    if selection in ['app', 'both']:
        if windows:
            exe = apps/'virtualjaguar.exe'
            user_data = apps/'data'
            assert (apps/'portable.txt').is_file()
        elif platform.system() == 'Darwin':
            bundle = apps/'Virtual Jaguar.app'
            exe = bundle/'Contents/MacOS/virtualjaguar'
            user_data = Path(env['HOME'])/'Library/Application Support/VirtualJaguar'
            run(['codesign', '--verify', '--deep', '--strict', bundle], selection+'-codesign')
            run(['spctl', '--assess', '--type', 'execute', '--verbose=4', bundle], selection+'-gatekeeper')
            run(['xcrun', 'stapler', 'validate', bundle], selection+'-staple')
        else:
            exe = apps/'VirtualJaguar.AppImage'
            user_data = Path(env['XDG_DATA_HOME'])/'VirtualJaguar'
            assert (Path(env['XDG_DATA_HOME'])/'applications/io.github.libretro.VirtualJaguar.desktop').is_file()
        assert exe.is_file(), 'app missing'
        run([exe, '--headless', '--frames', '300', '--expect-hash', 'f4c30ea3', repo/'test/roms/yarc.j64'], selection+'-launch')
        sentinels = [user_data/'config.ini', user_data/'saves/user.sav', user_data/'roms/user-owned.j64']
        for p in sentinels:
            p.parent.mkdir(parents=True, exist_ok=True); p.write_text('qualification user data')
    before = {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']}
    run(cmd+[dry], selection+'-dry-after')
    assert {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']} == before, 'dry-run changed installed files'
    run(cmd, selection+'-update')
    run(cmd+[force], selection+'-force-update')
    if selection in ['app', 'both']:
        for p in sentinels:
            assert p.read_text() == 'qualification user data', f'update lost user data: {p}'
        run([exe, '--headless', '--frames', '300', '--expect-hash', 'f4c30ea3', repo/'test/roms/yarc.j64'], selection+'-updated-launch')
if windows:
    # Exercise the downloaded Inno installer too, not just the portable updater.
    asset_name = f'VirtualJaguar-{args.tag}-windows-x64-setup.exe'
    asset = next(a for a in release['assets'] if a['name'] == asset_name)
    setup = root/asset_name
    urllib.request.urlretrieve(asset['browser_download_url'], setup)
    with urllib.request.urlopen('https://github.com/libretro/virtualjaguar-libretro/releases/download/'+args.tag+'/SHA256SUMS.txt', timeout=60) as response:
        sums = {row.split()[1].lstrip('*'): row.split()[0] for row in response.read().decode().splitlines()}
    digest = hashlib.sha256(setup.read_bytes()).hexdigest()
    if digest != sums[asset_name]:
        raise RuntimeError('Published installer checksum mismatch')
    report['installer_sha256'] = digest
    env['QUALIFICATION_SETUP_FILE'] = str(setup)
    report['installer_signature'] = run([args.powershell, '-NoProfile', '-Command',
        '(Get-AuthenticodeSignature -LiteralPath $env:QUALIFICATION_SETUP_FILE).Status'], 'setup-signature').strip()
    installed = root/'setup-app'
    data = Path(env['APPDATA'])/'VirtualJaguar'/'saves'/'sentinel.sav'
    data.parent.mkdir(parents=True, exist_ok=True); data.write_text('preserve through setup lifecycle')
    setup_cmd = [setup, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/DIR='+str(installed)]
    run(setup_cmd, 'setup-install')
    run([installed/'virtualjaguar.exe', '--headless', '--frames', '300', '--expect-hash', 'f4c30ea3', repo/'test/roms/yarc.j64'], 'setup-launch')
    if (installed/'portable.txt').exists():
        raise RuntimeError('Installed app unexpectedly enables portable mode')
    run(setup_cmd, 'setup-update')
    if data.read_text() != 'preserve through setup lifecycle':
        raise RuntimeError('Setup update changed user data')
    run([installed/'unins000.exe', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'], 'setup-uninstall')
    # Inno uninstall may return before its final helper removes the exe.
    import time
    for _ in range(30):
        if not (installed/'virtualjaguar.exe').exists():
            break
        time.sleep(1)
    if (installed/'virtualjaguar.exe').exists() or data.read_text() != 'preserve through setup lifecycle':
        raise RuntimeError('Uninstall left the exe behind or removed user data')
report['result'] = 'PASS'
output = root/'qualification-result.json'
output.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2), flush=True)
