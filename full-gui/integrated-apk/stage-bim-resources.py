#!/usr/bin/env python3
"""Stage upstream FreeCAD Python modules and their Android-compatible Qt resources."""
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

SUFFIXES = {'.py', '.ui', '.svg', '.png', '.jpg', '.jpeg', '.gif', '.xpm',
            '.ico', '.qss', '.yaml', '.yml', '.json', '.qm', '.xml', '.csv',
            '.ttf', '.otf', '.txt', '.qrc', '.dat', '.FCMat', '.html', '.css'}
SKIP = {'__pycache__', 'tests', 'test', 'Tests', 'Test'}
REQUIRED = {'Mod/BIM/ArchMaterial.py', 'Mod/BIM/ArchWall.py',
            'Mod/BIM/ArchWindow.py', 'Mod/BIM/ArchComponent.py',
            'Mod/BIM/Arch_rc.py', 'Mod/Draft/Draft.py',
            'Mod/Draft/Draft_rc.py', 'Mod/Draft/WorkingPlane.py',
            'Mod/Draft/draftutils/params.py', 'lib/lazy_loader/lazy_loader.py',
            'Mod/Show/__init__.py', 'Mod/Show/mTempoVis.py', 'Mod/Show/SceneDetails/Camera.py',
            'Mod/Help/InitGui.py', 'Mod/Help/Help_rc.py', 'Mod/Help/dlgPreferencesHelp.ui',
            'Mod/Help/default.css', 'Mod/Tux/InitGui.py', 'Mod/Tux/Tux_rc.py',
            'Mod/Tux/NavigationIndicatorGui.py', 'Mod/Tux/PersistentToolbarsGui.py'}


def copy_scripts(source, destination):
    if not source.is_dir():
        raise SystemExit(f'Missing pinned upstream resource directory: {source}')
    count = 0
    for file in sorted(source.rglob('*')):
        relative = file.relative_to(source)
        if (not file.is_file() or file.is_symlink() or file.suffix not in SUFFIXES
                or any(p.startswith('.') or p in SKIP for p in relative.parts)):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        count += 1
    return count


def compile_resources(rcc, directory, name):
    manifest = directory / 'Resources' / (name + '.qrc')
    tree = ET.parse(manifest)
    for group in tree.findall('qresource'):
        aliases = {}
        for entry in group.findall('file'):
            alias = entry.get('alias', entry.text)
            if alias in aliases:
                previous = aliases[alias]
                if entry.text != previous.text or entry.attrib != previous.attrib:
                    raise ValueError(f'Conflicting resource alias {alias} in {manifest}')
                group.remove(entry)
            else:
                aliases[alias] = entry
    # Upstream lists its Swedish translation twice. Normalize only the staged copy.
    tree.write(manifest, encoding='utf-8')
    output = directory / (name + '_rc.py')
    # Host rcc defaults to zstd; Android QtCore supports zlib.
    subprocess.run([str(rcc), '--no-zstd', '--compress-algo', 'zlib',
                    '-g', 'python', '-o', str(output), str(manifest)], check=True)
    compile(output.read_bytes(), str(output), 'exec')


def stage(source, destination, rcc):
    count = 0
    for module, resource in (('BIM', 'Arch'), ('Draft', 'Draft'), ('Help', 'Help'), ('Tux', 'Tux')):
        original = source / 'src/Mod' / module
        target = destination / 'Mod' / module
        count += copy_scripts(original, target)
        if module in ('Draft', 'BIM'):
            count += copy_scripts(original / 'Resources',
                                  destination / 'share/Mod' / module / 'Resources')
        compile_resources(rcc, target, resource)
    count += copy_scripts(source / 'src/Mod/Show', destination / 'Mod/Show')
    count += copy_scripts(source / 'src/3rdParty/lazy_loader',
                          destination / 'lib/lazy_loader')
    count += copy_scripts(Path(__file__).with_name('python-compat'), destination / 'Mod')
    missing = [name for name in REQUIRED if not (destination / name).is_file()]
    if missing:
        raise SystemExit(f'Incomplete BIM/Draft restore resources: {missing}')
    print(f'PASS: staged {count} pinned FreeCAD module assets and compiled four Qt resource bundles')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('Usage: stage-bim-resources.py pinned-FreeCAD-source resource-dir host-rcc')
    stage(*map(Path, sys.argv[1:]))
