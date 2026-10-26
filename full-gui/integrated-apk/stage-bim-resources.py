#!/usr/bin/env python3
"""Package pinned upstream BIM/Draft scripts and compile their real Qt resources."""
from pathlib import Path
import shutil
import subprocess
import sys

SUFFIXES = {'.py', '.ui', '.svg', '.png', '.jpg', '.jpeg', '.gif', '.xpm',
            '.ico', '.qss', '.yaml', '.yml', '.json', '.qm', '.xml', '.csv',
            '.ttf', '.otf', '.txt', '.qrc', '.dat', '.FCMat', '.html'}
SKIP = {'__pycache__', 'tests', 'test', 'Tests', 'Test'}
REQUIRED = {'Mod/BIM/ArchMaterial.py', 'Mod/BIM/ArchWall.py',
            'Mod/BIM/ArchWindow.py', 'Mod/BIM/ArchComponent.py',
            'Mod/BIM/Arch_rc.py', 'Mod/Draft/Draft.py',
            'Mod/Draft/Draft_rc.py', 'Mod/Draft/WorkingPlane.py',
            'Mod/Draft/draftutils/params.py', 'lib/lazy_loader/lazy_loader.py'}


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


def stage(source, destination, rcc):
    count = 0
    for module, resource in (('BIM', 'Arch'), ('Draft', 'Draft')):
        original = source / 'src/Mod' / module
        target = destination / 'Mod' / module
        count += copy_scripts(original, target)
        count += copy_scripts(original / 'Resources',
                              destination / 'share/Mod' / module / 'Resources')
        # Same operation as upstream PYSIDE_WRAP_RC. Real Qt resources, no
        # placeholder Arch_rc/Draft_rc modules or runtime PySide compilation.
        output = target / (resource + '_rc.py')
        subprocess.run([str(rcc), '-g', 'python', '-o', str(output),
                        str(original / 'Resources' / (resource + '.qrc'))], check=True)
        compile(output.read_text(encoding='utf-8'), str(output), 'exec')
    count += copy_scripts(source / 'src/3rdParty/lazy_loader',
                          destination / 'lib/lazy_loader')
    count += copy_scripts(Path(__file__).with_name('python-compat'), destination / 'Mod')
    missing = [name for name in REQUIRED if not (destination / name).is_file()]
    if missing:
        raise SystemExit(f'Incomplete BIM/Draft restore resources: {missing}')
    print(f'PASS: staged {count} pinned BIM/Draft assets and compiled Arch/Draft Qt resources')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('Usage: stage-bim-resources.py pinned-FreeCAD-source resource-dir host-rcc')
    stage(*map(Path, sys.argv[1:]))
