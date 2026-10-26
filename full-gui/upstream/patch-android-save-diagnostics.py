#!/usr/bin/env python3
"""Trace the genuine native FCStd writer after a phone SIGABRT in saveCopy.

No CAD properties, archive format, exception behavior or save operations are
replaced. Every replacement is pinned and fails closed if upstream drifts.
Run after patch-document-diagnostics.py installs the durable native journal.
"""
from pathlib import Path
import runpy
import sys

HELPERS = runpy.run_path(str(Path(__file__).with_name('patch-document-diagnostics.py')))
annotate = HELPERS['annotate']
once = HELPERS['once']


def edit(root, relative, old, new):
    path = root / relative
    path.write_text(once(path.read_text(), old, new))


def patch_source(root):
    if 'SAVE_PY 00 parse filename' in (root / 'src/App/DocumentPyImp.cpp').read_text():
        raise RuntimeError('Already instrumented native Save')
    annotate(root, 'src/App/DocumentPyImp.cpp', 'PyObject* DocumentPy::saveCopy(PyObject* args)', [
        ('if (!PyArg_ParseTuple(args, "s", &fn)) {\n        return nullptr;\n    }',
         'SAVE_PY 00 parse filename'),
        ('getDocumentPtr()->saveCopy(fn);', 'SAVE_PY 01 native saveCopy'),
    ])
    path = root / 'src/App/DocumentPyImp.cpp'
    path.write_text('#ifdef __ANDROID__\n'
                    'extern "C" __attribute__((visibility("default"))) int drnas_android_save_trace_v1() { return 1; }\n'
                    '#endif\n' + path.read_text())
    edit(root, 'src/App/Document.cpp',
         '    return this->FileName.getStrValue() != checked ? saveToFile(checked.c_str()) : false;',
         '    ::drnasDocumentPhase("SAVE_COPY 02 saveToFile", "BEGIN");\n'
         '    const bool drnasSaved = this->FileName.getStrValue() != checked ? saveToFile(checked.c_str()) : false;\n'
         '    ::drnasDocumentPhase("SAVE_COPY 02 saveToFile", drnasSaved ? "RETURN_TRUE" : "RETURN_FALSE");\n'
         '    return drnasSaved;')
    annotate(root, 'src/App/Document.cpp', 'bool Document::saveCopy(const char* file) const', [
        ('const std::string checked = checkFileName(file);', 'SAVE_COPY 01 check filename'),
    ])
    annotate(root, 'src/App/Document.cpp', 'static std::string checkFileName(const char* file)', [
        ('std::string fn(file);', 'SAVE_NAME 01 construct filename'),
    ])
    edit(root, 'src/App/Document.cpp',
         '        GetApplication().signalSaveDocument(*this);\n    }\n\n    if (policy) {',
         '        ::drnasDocumentPhase("SAVE_FILE 19 app save signal", "BEGIN");\n'
         '        GetApplication().signalSaveDocument(*this);\n'
         '        ::drnasDocumentPhase("SAVE_FILE 19 app save signal", "END");\n'
         '        ::drnasDocumentPhase("SAVE_FILE 20 close ZIP scope", "BEGIN");\n'
         '    }\n'
         '    ::drnasDocumentPhase("SAVE_FILE 20 close ZIP scope", "END");\n\n'
         '    if (policy) {')
    annotate(root, 'src/App/Document.cpp', 'bool Document::saveToFile(const char* filename) const', [
        ('signalStartSave(*this, filename);', 'SAVE_FILE 10 start signal'),
        ('std::string nativePath = canonical_path(filename);', 'SAVE_FILE 11 canonical path'),
        ('Base::FileInfo originalFileInfo(nativePath);', 'SAVE_FILE 12 file info'),
        ('std::string uuid = Base::Uuid::createUuid();', 'SAVE_FILE 13 UUID'),
        ('Base::ofstream file(tmp, std::ios::out | std::ios::binary);', 'SAVE_FILE 14 open stream'),
        ('Base::ZipWriter writer(file);', 'SAVE_FILE 15 ZIP writer constructor'),
        ('writer.putNextEntry("Document.xml");', 'SAVE_FILE 16 Document.xml entry'),
        ('Document::Save(writer);', 'SAVE_FILE 17 document XML'),
        ('signalSaveDocument(writer);', 'SAVE_FILE 18 GUI save signal'),
        ('writer.writeFiles();', 'SAVE_FILE 18A archive payload'),
        ('backupPolicy.apply(fn, nativePath);', 'SAVE_FILE 21 backup rename'),
        ('signalFinishSave(*this, filename);', 'SAVE_FILE 22 finish signal'),
    ])
    annotate(root, 'src/App/Document.cpp', 'void Document::Save(Base::Writer& writer) const', [
        ('d->hashers.clear();', 'SAVE_XML 30 clear hashers'),
        ('addStringHasher(d->Hasher);', 'SAVE_XML 31 add hasher'),
        ('o->beforeSave();', 'SAVE_XML 32 object beforeSave'),
        ('    beforeSave();', 'SAVE_XML 33 document beforeSave'),
        ('d->Hasher->Save(writer);', 'SAVE_XML 34 hasher Save'),
        ('PropertyContainer::Save(writer);', 'SAVE_XML 35 document properties'),
        ('writeObjects(d->objectArray, writer);', 'SAVE_XML 36 objects'),
    ])
    annotate(root, 'src/App/Document.cpp', 'void Document::writeObjectData(', [
        ('it->Save(writer);', 'SAVE_OBJECT 37 native object Save'),
    ])
    edit(root, 'src/App/Document.cpp',
         '    for (auto it : objs) {\n        writer.Stream() << writer.ind() << "<Object name=',
         '    for (auto it : objs) {\n'
         '        ::drnasDocumentPhase("SAVE_OBJECT name", it->getNameInDocument());\n'
         '        writer.Stream() << writer.ind() << "<Object name=')
    for signature in ('ZipWriter::ZipWriter(const char* FileName)',
                      'ZipWriter::ZipWriter(std::ostream& os)'):
        annotate(root, 'src/Base/Writer.cpp', signature, [
            ('ZipStream.imbue(std::locale::classic());', 'SAVE_ZIP 40 classic locale'),
            ('ZipStream.precision(std::numeric_limits<double>::digits10 + 1);', 'SAVE_ZIP 41 precision'),
        ])
    annotate(root, 'src/Base/Writer.cpp', 'void ZipWriter::putNextEntry(', [
        ('Writer::putNextEntry(file, obj);', 'SAVE_ZIP 42 prepare entry'),
        ('ZipStream.putNextEntry(file);', 'SAVE_ZIP 43 put entry'),
        ('Writer::checkErrNo();', 'SAVE_ZIP 44 check errno'),
    ])
    annotate(root, 'src/Base/Writer.cpp', 'void ZipWriter::writeFiles()', [
        ('putNextEntry(entry.FileName.c_str());', 'SAVE_ZIP 45 file entry'),
        ('entry.Object->SaveDocFile(*this);', 'SAVE_ZIP 46 SaveDocFile'),
    ])
    edit(root, 'src/Base/Writer.cpp',
         '        FileEntry entry = FileList[index];\n'
         '        if (_drnasTraceEnabled) ::drnasDocumentPhase("SAVE_ZIP 45 file entry", "BEGIN");',
         '        FileEntry entry = FileList[index];\n'
         '        ::drnasDocumentPhase("SAVE_ZIP filename", entry.FileName.c_str());\n'
         '        if (_drnasTraceEnabled) ::drnasDocumentPhase("SAVE_ZIP 45 file entry", "BEGIN");')
    annotate(root, 'src/Base/Writer.cpp', 'ZipWriter::~ZipWriter()', [
        ('ZipStream.close();', 'SAVE_ZIP 47 close archive'),
    ])
    part = 'src/Mod/Part/App/PropertyTopoShape.cpp'
    annotate(root, part, 'void PropertyPartShape::beforeSave() const', [
        ('_Shape.beforeSave();', 'SAVE_BREP 50 shape beforeSave'),
    ])
    annotate(root, part, 'void PropertyPartShape::Save(Base::Writer& writer) const', [])
    annotate(root, part, 'void PropertyPartShape::SaveDocFile(Base::Writer& writer) const', [
        ('saveToFile(writer);', 'SAVE_BREP 51 file-backed save'),
        ('shape.exportBrep(writer.Stream());', 'SAVE_BREP 52 direct stream save'),
    ])
    edit(root, part,
         '    if (!BRepTools_Write(myShape, static_cast<Standard_CString>(fi.filePath().c_str()))) {',
         '    ::drnasDocumentPhase("SAVE_BREP 54 OCCT filename write", "BEGIN");\n'
         '    const bool drnasBrepWritten = BRepTools_Write(myShape, static_cast<Standard_CString>(fi.filePath().c_str()));\n'
         '    ::drnasDocumentPhase("SAVE_BREP 54 OCCT filename write", drnasBrepWritten ? "RETURN_TRUE" : "RETURN_FALSE");\n'
         '    if (!drnasBrepWritten) {')
    annotate(root, part, 'void PropertyPartShape::saveToFile(Base::Writer& writer) const', [
        ('static Base::FileInfo fi(App::Application::getTempFileName());', 'SAVE_BREP 53 temp path'),
        ('writer.Stream() << buf;', 'SAVE_BREP 55 temp BRep into ZIP'),
        ('file.close();', 'SAVE_BREP 56 close temporary file'),
        ('fi.deleteFile();', 'SAVE_BREP 57 delete temporary file'),
    ])
    annotate(root, 'src/Gui/Document.cpp', 'void Document::Save(Base::Writer& writer) const', [
        ('writer.addFile("GuiDocument.xml", this);', 'SAVE_GUI 60 register XML'),
        ('d->thumb.Save(writer);', 'SAVE_GUI 61 register thumbnail'),
    ])
    annotate(root, 'src/Gui/Document.cpp', 'void Document::SaveDocFile(Base::Writer& writer) const', [
        ('docItem->Save(writer);', 'SAVE_GUI 62 tree state'),
        ('obj->Save(writer);', 'SAVE_GUI 63 view provider'),
    ])
    print('PASS: native FCStd Save checkpoints installed; CAD operations and archive format retained')


if __name__ == '__main__':
    patch_source(Path(sys.argv[1]))
