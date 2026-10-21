#!/usr/bin/env python3
"""Safe in-place Android AArch64 ELF DT_NEEDED and SONAME relinker.

NEVER use patchelf on the prebuilt upstream libraries: patchelf introduced
PT_LOAD segments whose offset and virtual address differed modulo 4096;
Android's linker then interpreted libCoin verneed[0] as "TIPx".
Do not move or enlarge any ELF sections: new names always fit in dynstr.
"""
from pathlib import Path
import struct
import sys

PRIVATE_ICU = {
    "libicuuc.so": "libd75uc.so",
    "libicui18n.so": "libd75i18.so",
    "libicudata.so": "libd75dat.so",
    "libicuio.so": "libd75io.so",
    "libicutest.so": "libd75tst.so",
    "libicutu.so": "libd75tu.so",
}
RENAME = {"libGL.so.1": "libGL.so"}
for old, new in PRIVATE_ICU.items():
    assert len(new) <= len(old)
    RENAME[old] = new
    RENAME[old + ".75"] = new


def rewrite(path):
    data = bytearray(path.read_bytes())
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
        raise ValueError(f"{path.name}: not Android ARM64 little-endian ELF")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    ent, num = struct.unpack_from("<HH", data, 54)
    if ent != 56 or phoff + ent * num > len(data):
        raise ValueError(f"{path.name}: invalid ELF program headers")
    loads = []
    dynamic = None
    for i in range(num):
        typ, flags, off, vaddr, _, size, memsize, align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * ent
        )
        if typ == 1:
            if off % 4096 != vaddr % 4096:
                raise ValueError(
                    f"{path.name}: malformed non-congruent PT_LOAD: "
                    f"offset={off:#x} vaddr={vaddr:#x}"
                )
            loads.append((vaddr, off, size))
        if typ == 2:
            dynamic = (off, size)
    if dynamic is None:
        raise ValueError(f"{path.name}: missing dynamic table")

    def locate(addr):
        for vaddr, off, size in loads:
            if vaddr <= addr < vaddr + size:
                return off + addr - vaddr
        raise ValueError(f"{path.name}: unmapped ELF address {addr:#x}")

    off, size = dynamic
    entries = []
    tags = {}
    for index in range(off, off + size, 16):
        tag, value = struct.unpack_from("<QQ", data, index)
        if tag == 0:
            break
        entries.append((tag, value))
        tags[tag] = value
    base = locate(tags[5])
    limit = base + tags[10]
    if limit > len(data):
        raise ValueError(f"{path.name}: truncated dynstr")

    def read_string(index):
        pos = base + index
        if pos < base or pos >= limit:
            raise ValueError(f"{path.name}: bad dynstr index {index}")
        end = data.find(0, pos, limit)
        if end < 0:
            raise ValueError(f"{path.name}: unterminated dynstr")
        return data[pos:end].decode("ascii")

    names = [value for tag, value in entries if tag in (1, 14)]
    verneeds = []
    if 0x6ffffffe in tags:
        vpos = locate(tags[0x6ffffffe])
        for i in range(tags.get(0x6fffffff, 0)):
            version, count, file_idx, aux, following = struct.unpack_from(
                "<HHIII", data, vpos
            )
            if version != 1:
                raise ValueError(f"{path.name}: unexpected version-need format")
            verneeds.append(file_idx)
            if i + 1 < tags[0x6fffffff]:
                if following == 0:
                    raise ValueError(f"{path.name}: broken version-need chain")
                vpos += following
    count = 0
    for index in set(names + verneeds):
        old = read_string(index)
        new = RENAME.get(old)
        if new is None:
            continue
        if len(new) > len(old):
            raise ValueError(f"{path.name}: replacement too long: {old}")
        pos = base + index
        data[pos:pos + len(old) + 1] = new.encode("ascii") + (
            b"\0" * (len(old) + 1 - len(new))
        )
        count += 1
    needed = [read_string(value) for tag, value in entries if tag == 1]
    if any(name in RENAME for name in needed):
        raise ValueError(f"{path.name}: missed a legacy DT_NEEDED alias")
    if any(read_string(i) not in needed for i in verneeds):
        raise ValueError(f"{path.name}: GNU version-need must match DT_NEEDED")
    if count:
        path.write_bytes(data)
    return count, needed


def main():
    stage = Path(sys.argv[1])
    if not stage.is_dir():
        raise ValueError(f"Native staging folder missing: {stage}")
    so_files = sorted(stage.glob("lib*.so"))
    if not so_files:
        raise ValueError("No staged Android ELF libraries")
    needed_map = {}
    changed = 0
    for so in so_files:
        count, needed = rewrite(so)
        changed += count
        needed_map[so.name] = needed
    for old, new in PRIVATE_ICU.items():
        original = stage / old
        if original.is_file():
            if (stage / new).exists():
                raise ValueError(f"Private ICU name collision: {new}")
            original.rename(stage / new)
            print(f"Privately namespaced Android ICU: {old} => {new}")
    available = {p.name for p in stage.glob("lib*.so")}
    for source, needed in needed_map.items():
        for name in needed:
            if name in RENAME or name.startswith("libicu"):
                raise ValueError(f"Unrelocated dependency: {source} -> {name}")
            if name.startswith("libd75") and name not in available:
                raise ValueError(f"Missing private ICU dependency: {name}")
    for required in (
        "libFreeCADGui.so", "libFreeCADApp.so", "libFreeCADBase.so",
        "libCoin.so", "libGL.so", "libGLU.so",
        "libd75uc.so", "libd75i18.so", "libd75dat.so",
    ):
        if required not in available:
            raise ValueError(f"Missing required Android runtime: {required}")
    print(f"PASS: {len(so_files)} Android arm64 ELF files retained their PT_LOAD layout; "
          f"{changed} in-place dynamic string updates.")


if __name__ == "__main__":
    main()
