#!/usr/bin/env python3
"""Verify and merge a successful binding bundle before Android APK staging."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
bundle = root/'out/bindings/runtime'
artifact = root/'out/upstream-freecad-gui'
native = artifact/'native'
qt = root/'out/full-deps/qt/6.8.3/android_arm64_v8a/lib'
report = root/'out/integrated-freecad-qtapk'
report.mkdir(parents=True, exist_ok=True)
metadata = json.loads((bundle/'BUILD-INFO.json').read_text())
assert metadata['qt'] == '6.8.3' and metadata['python'] == '3.14.8'
assert metadata['abi'] == 'arm64-v8a'
assert metadata['coin'] == '50e37f04e7027661ea2318d0425b7836ac819568', 'Coin must match the pinned FreeCAD submodule ABI'
subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=native,check=True)
for package, modules in {'PySide6':['QtCore','QtGui','QtWidgets'], 'shiboken6':['Shiboken'], 'pivy':['_coin']}.items():
    folder = bundle/'python-bindings'/package
    assert (folder/'__init__.py').is_file(), package
    for module in modules:
        assert list(folder.glob(module+'*.so')), (package,module)

# The updated CAD and Pivy payloads must use the same native SWIG registry.
coin_extensions = list((bundle/'python-bindings/pivy').glob('_coin*.so'))
assert len(coin_extensions) == 1, 'Ambiguous Pivy native extension'
base_runtime = set(re.findall(rb'swig_runtime_data([0-9]+)', (native/'libFreeCADBase.so').read_bytes()))
pivy_runtime = set(re.findall(rb'swig_runtime_data([0-9]+)', coin_extensions[0].read_bytes()))
assert len(base_runtime) == 1 and base_runtime == pivy_runtime, (
    'FreeCADBase/Pivy SWIG runtime mismatch', base_runtime, pivy_runtime)
print('PASS: updated FreeCADBase/Pivy share SWIG runtime ' + next(iter(base_runtime)).decode())

# Match the native Coin ABI symbols actually referenced by the prebuilt CAD.
def symbols(path, defined):
    flag = '--defined-only' if defined else '--undefined-only'
    output = subprocess.check_output(['nm','-D',flag,str(path)], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.split()}
old_coin = symbols(native/'libCoin.so', True)
new_coin_path = bundle/'native/libCoin.so'
new_coin = symbols(new_coin_path, True)
used = set()
for path in native.glob('*.so'):
    if path.name != 'libCoin.so':
        used.update(symbols(path, False) & old_coin)
missing = used-new_coin
assert not missing, 'New Coin is missing FreeCAD ABI symbols: '+str(sorted(missing)[:20])

# Normalize versioned SONAMEs in copies, without relocating ELF segments.
spec = importlib.util.spec_from_file_location('relink',root/'full-gui/integrated-apk/relocate-android-sonames.py')
relink = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relink)
for path in (bundle/'native').glob('*.so*'):
    if '.so.' in path.name:
        relink.RENAME[path.name] = path.name.split('.so.')[0]+'.so'
for path in bundle.rglob('*.so*'):
    if path.is_file():
        relink.rewrite(path)
for path in list((bundle/'native').glob('*.so.*')):
    dest = path.with_name(path.name.split('.so.')[0]+'.so')
    if dest.exists():
        assert path.read_bytes() == dest.read_bytes(), f'Native alias collision: {dest}'
    else:
        shutil.copyfile(path,dest)
    path.unlink()

# Resolve every extension against the entire target SDK before modifying CAD.
providers = {p.name for p in qt.glob('*.so')}
for directory in (native, bundle/'native', root/'out/full-deps/python/lib', root/'out/full-deps/icu/lib', root/'out/full-deps/boost/lib', root/'out/full-deps/xerces-c/lib', root/'out/full-deps/yaml-cpp/lib', root/'out/occt-android/lib'):
    providers.update(p.name for p in directory.glob('*.so*'))
system = {'libc.so','libm.so','libdl.so','liblog.so','libz.so','libandroid.so','libEGL.so','libGLESv2.so','libGLESv3.so','libOpenSLES.so','libjnigraphics.so','libvulkan.so','libmediandk.so','libaaudio.so','libnativewindow.so'}
failures = []
qt_needed = set()
pending = list(bundle.rglob('*.so'))
inspected = set()
while pending:
    path = pending.pop()
    if path in inspected:
        continue
    inspected.add(path)
    header = subprocess.check_output(['readelf','-h',str(path)],text=True)
    assert re.search(r'Machine:\s*AArch64',header), path
    dynamic = subprocess.check_output(['readelf','-d',str(path)],text=True)
    assert not re.search(r'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6',dynamic), path
    for dep in re.findall(r'Shared library: \[([^]]+)\]',dynamic):
        if (qt/dep).is_file() and dep not in qt_needed:
            qt_needed.add(dep)
            pending.append(qt/dep)
        if dep not in providers|system:
            failures.append(f'{path.name} -> {dep}')
assert not failures, 'Binding dependencies missing: '+str(failures)

for package in ('PySide6','shiboken6','pivy'):
    dest = artifact/'python-bindings'/package
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(bundle/'python-bindings'/package,dest)
for name in qt_needed:
    shutil.copyfile(qt/name,native/name)
for path in (bundle/'native').glob('*.so'):
    # GL/GLU are also consumed from opengl-compat by stage-runtime.sh.
    dest = artifact/'opengl-compat/lib'/path.name if path.name in ('libGL.so','libGLU.so') else native/path.name
    shutil.copyfile(path,dest)
(native/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(native.glob('*.so'))))
(report/'BINDINGS-INFO.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(f'PASS: merged genuine Android binding packages; {len(used)} Coin ABI symbol references checked.')
print('Class layouts and device imports still require on-device testing.')
