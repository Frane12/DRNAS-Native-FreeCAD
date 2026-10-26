#!/usr/bin/env python3
"""Keep Android FCStd geometry serialization inside OCCT's filename writer.

Phone APK 323 reached SAVE_BREP 54 and aborted in FreeCAD's copied
BRepTools_Write implementation. TopoShape::exportBrep(filename) uses OCCT's
own writer, with identical no-triangulation/version-1 options. Avoid passing
FreeCAD's std::ofstream through OCCT on Android; retain desktop behavior.
Run after the native document/save checkpoint patches.
"""
from pathlib import Path
import sys

SIGNATURE = 'static Standard_Boolean BRepTools_Write(const TopoDS_Shape& Sh, const Standard_CString File)'
SYMBOL = 'drnas_android_brep_filename_save_v1'


def patch_source(root):
    path = root / 'src/Mod/Part/App/PropertyTopoShape.cpp'
    source = path.read_text()
    if SYMBOL in source:
        raise RuntimeError('Android BRep filename Save already installed')
    if 'SAVE_BREP 54 OCCT filename write' not in source:
        raise RuntimeError('Native Save checkpoints must be installed first')
    anchor = SIGNATURE + '\n{\n'
    ending = '    return isGood;\n}\n\nvoid PropertyPartShape::saveToFile('
    if source.count(anchor) != 1 or source.count(ending) != 1:
        raise RuntimeError('Pinned BRep writer changed')
    source = source.replace(anchor, '''#ifdef __ANDROID__
extern "C" __attribute__((visibility("default"))) int drnas_android_brep_filename_save_v1() { return 1; }
#endif
''' + anchor + '''#ifdef __ANDROID__
    ::drnasDocumentPhase("SAVE_BREP 58 OCCT-owned filename writer v1", "BEGIN");
    // Match TopoShape::exportBrep(filename): OCCT owns the file stream,
    // writes format 1, and omits triangulation and its normals.
    const Standard_Boolean written = BRepTools::Write(
        Sh, File, Standard_False, Standard_False, TopTools_FormatVersion_VERSION_1);
    ::drnasDocumentPhase("SAVE_BREP 58 OCCT-owned filename writer v1",
                         written ? "RETURN_TRUE" : "RETURN_FALSE");
    return written;
#else
''', 1)
    source = source.replace(ending, '    return isGood;\n#endif\n}\n\nvoid PropertyPartShape::saveToFile(', 1)
    path.write_text(source)
    print('PASS: Android FCStd BRep uses the genuine OCCT filename writer (format 1, no triangulation)')


if __name__ == '__main__':
    patch_source(Path(sys.argv[1]))
