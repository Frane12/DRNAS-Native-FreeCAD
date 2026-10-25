#!/usr/bin/env python3
"""Add only genuine Measure extensions to the known-good Android payload."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbols(path, defined):
    flag = '--defined-only' if defined else '--undefined-only'
    output = subprocess.check_output(['nm', '-D', flag, str(path)], text=True)
    return {p[-1].split('@')[0] for line in output.splitlines()
            if (p := line.split()) and (defined or p[-2] == 'U')}


def integrate(donor, baseline, report):
    donor, baseline, report = map(Path, (donor, baseline, report))
    src, dst = donor/'native', baseline/'native'
    modules = ('Measure.so', 'MeasureGui.so')
    expected = {}
    for line in (src/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[name.lstrip('*')] = digest
    before = {p.name: sha(p) for p in dst.glob('*.so')}
    assert before, 'Baseline native payload is empty'
    assert all(name not in before for name in modules), 'Baseline unexpectedly already includes Measure'
    donor_symbols, baseline_symbols = set(), set()
    # Verify the new extensions' references into FreeCAD/Coin/Part against
    # the original binaries, rather than silently replacing their providers.
    for name in before:
        candidate = src/name
        if candidate.is_file():
            donor_symbols.update(symbols(candidate, True))
        baseline_symbols.update(symbols(dst/name, True))
    imported = set()
    for name in modules:
        path = src/name
        assert path.is_file() and sha(path) == expected[name], f'Unverified donor module: {name}'
        header = subprocess.check_output(['readelf', '-h', str(path)], text=True)
        dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
        assert re.search(r'Machine:\s*AArch64', header), name
        assert not re.search(r'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6', dynamic), name
        imported.update(symbols(path, False))
        baseline_symbols.update(symbols(path, True))
    missing = (imported & donor_symbols) - baseline_symbols
    assert not missing, 'Measure requires unavailable baseline ABI symbols: '+str(sorted(missing)[:30])
    for name in modules:
        shutil.copyfile(src/name, dst/name)
    assert all(sha(dst/name) == digest for name, digest in before.items()), 'An original library changed'
    (dst/'SHA256SUMS').write_text(''.join(
        sha(p)+'  '+p.name+'\n' for p in sorted(dst.glob('*.so'))))
    report.mkdir(parents=True, exist_ok=True)
    (report/'MEASURE-DEPENDENCY-INFO.json').write_text(json.dumps({
        'baseline_native_run': 40093671283,
        'measure_native_run': 40218602929,
        'freecad_ref': 'd4ffc7f725973e073907746e88d320d3e438fd34',
        'added_modules': {name: sha(dst/name) for name in modules},
        'preserved_native_libraries': before,
        'checked_provider_symbols': len(imported & donor_symbols),
    }, indent=2)+'\n')
    print(f'PASS: only Measure/MeasureGui added; {len(before)} original libraries preserved; provider ABI checked')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('Usage: integrate-measure.py donor-artifact baseline-artifact report-dir')
    integrate(*sys.argv[1:])
