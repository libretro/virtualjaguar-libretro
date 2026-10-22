#!/usr/bin/env python3
"""Exercise a published candidate in an isolated user profile on disposable CI hosts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
from qualification_support import verify_release_unchanged
import subprocess
import tempfile
import urllib.request
import urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--sha', required=True)
parser.add_argument('--powershell', default='pwsh')
parser.add_argument('--baseline-tag', default='v3.9.1')
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
    log_path = root/(label+'.log')
    with log_path.open('wb') as log:
        proc = subprocess.run([str(c) for c in cmd], env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=600)
    proc.stdout = log_path.read_bytes()
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
commit_request = urllib.request.Request(
    'https://api.github.com/repos/libretro/virtualjaguar-libretro/commits/'+urllib.parse.quote(args.tag, safe=''),
    headers=headers)
with urllib.request.urlopen(commit_request, timeout=60) as response:
    initial_tag_sha = json.load(response)['sha']
if initial_tag_sha != args.sha:
    raise RuntimeError('Published release does not identify the requested candidate SHA')
report['release_id'] = release['id']
windows = platform.system() == 'Windows'
frontends = []
if windows:
    # Invoke the GUI executable directly: Chocolatey shims may return before
    # their child finishes, losing process status and the qualification log.
    frontends += [r'C:\tools\RetroArch-Win64\retroarch.exe',
                  r'C:\RetroArch-Win64\retroarch.exe', r'C:\RetroArch\retroarch.exe',
                  r'C:\Program Files\RetroArch\retroarch.exe']
    frontends += [str(p) for p in Path(r'C:\ProgramData\chocolatey\lib\retroarch').rglob('retroarch.exe')]
else:
    frontends += [shutil.which('retroarch'), '/Applications/RetroArch.app/Contents/MacOS/RetroArch']
frontend = next((Path(p) for p in frontends if p and Path(p).is_file()), None)
if frontend is None:
    raise RuntimeError('Real RetroArch frontend is required for core-load qualification')
env['VJ_INSTALL_RETROARCH'] = str(frontend)
config = root/'retroarch-qualification.cfg'
linux = platform.system() == 'Linux'
video = 'gl' if platform.system() in ['Darwin', 'Linux'] else 'null'
# Old distro RetroArch null-video paths can crash before running content.
# Exercise the real GL frontend on a disposable virtual display instead.
frontend_cmd = ['xvfb-run', '-a', str(frontend)] if linux else [str(frontend)]
if linux:
    env['LIBGL_ALWAYS_SOFTWARE'] = '1'
config.write_text(f'video_driver = "{video}"\naudio_driver = "null"\ninput_driver = "null"\n'
                  'menu_driver = "rgui"\npause_nonactive = "false"\n'
                  'config_save_on_exit = "false"\nhistory_list_enable = "false"\n'
                  'microphone_enable = "false"\nmidi_driver = "null"\n'
                  f'playlist_directory = "{root.as_posix()}"\n'
                  f'core_options_path = "{(root/"core-options.cfg").as_posix()}"\n'
                  f'rgui_config_directory = "{root.as_posix()}"\n'
                  f'runtime_log_directory = "{root.as_posix()}"\n'
                  f'content_history_path = "{(root/"history.lpl").as_posix()}"\n'
                  f'savefile_directory = "{root.as_posix()}"\n'
                  f'savestate_directory = "{root.as_posix()}"\n')
report['retroarch'] = str(frontend)
report['baseline_tag'] = args.baseline_tag
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
    baseline_cmd = [args.baseline_tag if str(c) == args.tag else c for c in cmd]
    run(baseline_cmd, selection+'-baseline-install')
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
        run([exe, '--headless', '--frames', '300', repo/'test/roms/yarc.j64'], selection+'-baseline-launch')
        sentinels = [user_data/'config.ini', user_data/'saves/user.sav', user_data/'roms/user-owned.j64']
        for p in sentinels:
            p.parent.mkdir(parents=True, exist_ok=True); p.write_text('qualification user data')
    before = {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']}
    run(cmd+[dry], selection+'-dry-after')
    assert {str(d): snapshot(d) for d in [case, root/'home', root/'local', root/'roaming', root/'data', root/'config']} == before, 'dry-run changed installed files'
    payload = next(p for p in cores.iterdir() if p.suffix.lower() in ['.so', '.dylib', '.dll']) if selection == 'core' else exe
    baseline_hash = hashlib.sha256(payload.read_bytes()).hexdigest()
    update_output = run(cmd, selection+'-update')
    if 'already up to date' in update_output.lower() or hashlib.sha256(payload.read_bytes()).hexdigest() == baseline_hash:
        raise RuntimeError('Normal update did not replace the older published payload')
    if selection in ['core', 'both']:
        core = next(p for p in cores.iterdir() if p.suffix.lower() in ['.so', '.dylib', '.dll'])
        output = run(frontend_cmd+['--verbose', '--log-file='+str(root/(selection+'-retroarch.log')), '--config', config, '--max-frames=30',
                      '-L', core, repo/'test/roms/yarc.j64'], selection+'-retroarch-core-load')
        output += (root/(selection+'-retroarch.log')).read_text(errors='replace')
        if 'Failed to load' in output or 'Loading dynamic libretro core' not in output:
            raise RuntimeError('RetroArch did not confirm loading the installed core')
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
with urllib.request.urlopen(request, timeout=60) as response:
    final_release = json.load(response)
with urllib.request.urlopen(commit_request, timeout=60) as response:
    final_tag_sha = json.load(response)['sha']
verify_release_unchanged(release, final_release, args.sha, initial_tag_sha, final_tag_sha)
report['resolved_tag_sha'] = final_tag_sha
report['result'] = 'PASS'
output = root/'qualification-result.json'
output.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2), flush=True)
