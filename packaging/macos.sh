#!/bin/bash
set -euo pipefail
package=${1:?Specify the macOS architecture}
build_dir=${2:-build}
case "$package" in
  macOS-arm64) architecture=arm64 ;;
  macOS-x64) architecture=x86_64 ;;
  *) echo "Unsupported package architecture: $package" >&2; exit 1 ;;
esac
version=$(sed -n 's/^project(Aurex VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
stage="dist/Aurex-$version-$package"
rm -rf "$stage"
mkdir -p "$stage"
ditto "$build_dir/Aurex.app" "$stage/Aurex.app"
cp LICENSE "$stage/Aurex.app/Contents/Resources/"
cp -R "${RUNNER_TEMP:?}/opencv-install/share/licenses/opencv4" "$stage/Aurex.app/Contents/Resources/licenses/OpenCV-third-party"
macdeployqt "$stage/Aurex.app" -always-overwrite
python3 - "$stage/Aurex.app" "$architecture" <<'PY'
import os, pathlib, shutil, subprocess, sys, tempfile
app = pathlib.Path(sys.argv[1]).resolve()
architecture = sys.argv[2]
for path in app.rglob('*'):
    if path.is_symlink() or not path.is_file():
        continue
    if 'Mach-O' not in subprocess.run(['file', '-b', str(path)], capture_output=True, text=True, check=True).stdout:
        continue
    architectures = subprocess.run(['lipo', '-archs', str(path)], capture_output=True, text=True, check=True).stdout.split()
    if architecture not in architectures:
        raise SystemExit(f'Wrong architecture: {path}')
    if len(architectures) > 1:
        subprocess.run(['lipo', str(path), '-thin', architecture, '-output', str(path)], check=True)
    subprocess.run(['strip', '-S', '-x', str(path)], check=True)
    libraries = subprocess.run(['otool', '-L', str(path)], capture_output=True, text=True, check=True).stdout.splitlines()[1:]
    identifiers = {line.strip() for line in subprocess.run(['otool', '-D', str(path)], capture_output=True, text=True, check=True).stdout.splitlines()[1:]}
    for line in libraries:
        if ' (compatibility version ' not in line:
            continue
        dependency = line.strip().split(' (')[0]
        if dependency in identifiers and dependency.startswith('/') and not dependency.startswith(str(app / 'Contents/Frameworks') + '/'):
            replacement = '@loader_path/' + path.name
            subprocess.run(['install_name_tool', '-id', replacement, str(path)], check=True)
            dependency = replacement
        if dependency.startswith(str(app / 'Contents/Frameworks') + '/'):
            replacement = '@rpath/' + str(pathlib.Path(dependency).relative_to(app / 'Contents/Frameworks'))
            argument = ['-id', replacement] if dependency in identifiers else ['-change', dependency, replacement]
            subprocess.run(['install_name_tool', *argument, str(path)], check=True)
            dependency = replacement
        if dependency.startswith('/') and not dependency.startswith(('/System/', '/usr/lib/')):
            raise SystemExit(f'Unbundled library: {dependency}')
        if dependency.startswith('@rpath/') and not (app / 'Contents/Frameworks' / dependency.removeprefix('@rpath/')).is_file():
            raise SystemExit(f'Missing bundled library: {dependency}')
    commands = subprocess.run(['otool', '-l', str(path)], capture_output=True, text=True, check=True).stdout.splitlines()
    rpaths = {commands[i + 2].strip().split('path ', 1)[1].split(' (offset')[0]
              for i, line in enumerate(commands) if line.strip() == 'cmd LC_RPATH'}
    for rpath in rpaths:
        if rpath.startswith('/') and not rpath.startswith(('/System/', '/usr/lib/')):
            subprocess.run(['install_name_tool', '-delete_rpath', rpath, str(path)], check=True)
    if path == app / 'Contents/MacOS/Aurex' and '@executable_path/../Frameworks' not in rpaths:
        subprocess.run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', str(path)], check=True)
subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
with tempfile.TemporaryDirectory() as temp:
    moved = pathlib.Path(temp) / 'Aurex.app'
    shutil.copytree(app, moved, symlinks=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith(('DYLD_', 'QT_'))}
    env['QT_QPA_PLATFORM'] = 'cocoa'
    subprocess.run([str(moved / 'Contents/MacOS/Aurex'), '--screenshot', temp + '/startup.png'], env=env, timeout=30, check=True)
    if not pathlib.Path(temp + '/startup.png').is_file():
        raise SystemExit('Packaged application startup failed')
PY
ditto -c -k --sequesterRsrc --keepParent "$stage/Aurex.app" "$stage.zip"
