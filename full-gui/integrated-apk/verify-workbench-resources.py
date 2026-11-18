"""Read every packaged workbench resource through Qt and compare it to its source."""
from pathlib import Path
import runpy
import sys
import xml.etree.ElementTree as ET

from PySide6.QtCore import QFile, QIODevice, QResource


def verify(root):
    total = 0
    for module, name in (('BIM', 'Arch'), ('Draft', 'Draft'), ('Help', 'Help'), ('Tux', 'Tux')):
        directory = root / 'Mod' / module
        resources = directory / 'Resources'
        manifest = ET.parse(resources / (name + '.qrc'))
        bundle = runpy.run_path(str(directory / (name + '_rc.py')))
        count = 0
        try:
            for group in manifest.findall('qresource'):
                prefix = group.get('prefix', '').strip('/')
                for item in group.findall('file'):
                    alias = item.get('alias', item.text)
                    path = ':/' + '/'.join(filter(None, (prefix, alias)))
                    entry = QResource(path)
                    if not entry.isValid() or entry.compressionAlgorithm() not in (
                            QResource.NoCompression, QResource.ZlibCompression):
                        raise ValueError(f'{module}: missing or unsupported Android resource {path}')
                    file = QFile(path)
                    if not file.open(QIODevice.ReadOnly):
                        raise OSError(f'{module}: cannot open {path}: {file.errorString()}')
                    try:
                        if bytes(file.readAll()) != (resources / item.text).read_bytes():
                            raise ValueError(f'{module}: resource content mismatch at {path}')
                    finally:
                        file.close()
                    count += 1
        finally:
            bundle['qCleanupResources']()
        if not count:
            raise ValueError(f'{module}: empty resource manifest')
        print(f'{module}: {count} Qt resources verified')
        total += count
    return total


if __name__ == '__main__':
    print(f'PASS: {verify(Path(sys.argv[1]))} Android-compatible resources')
