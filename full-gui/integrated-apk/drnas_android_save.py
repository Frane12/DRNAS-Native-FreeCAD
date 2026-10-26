"""Native FreeCAD serialization and Android Save acknowledgement.

The Android provider is handled by Java. No Qt file dialogs or substitute CAD
serialization are used here. Each request retains a complete private FCStd.
"""
import json
import os
from pathlib import Path
import uuid
import zipfile

import FreeCAD as App
import FreeCADGui as Gui
from drnas_android_runtime import _drnas_mark

_sessions = {}
_pending = {}
_watching = False


class _SaveObserver:
    def changed(self, item, *args):
        obj = getattr(item, 'Object', item)
        doc = getattr(obj, 'Document', obj)
        session = _sessions.get(getattr(doc, 'Name', None))
        if session and session['document'] is doc:
            session['revision'] += 1

    slotChangedObject = changed
    slotCreatedObject = changed
    slotDeletedObject = changed
    slotChangedDocument = changed
    slotRecomputedDocument = changed


_observer = _SaveObserver()


def _filename(document):
    name = Path(document.FileName).name if document.FileName else str(document.Label)
    for suffix in ('.FCBak', '.FCStd'):
        if name.lower().endswith(suffix.lower()):
            name = name[:-len(suffix)]
    name = ''.join('_' if c in '/\\' or ord(c) < 32 else c for c in name).strip(' .')
    name = name.encode('utf-8')[:120].decode('utf-8', errors='ignore')
    return (name or 'Dokument') + '.FCStd'


def prepare_save(directory):
    global _watching
    doc = App.ActiveDocument
    if doc is None:
        raise RuntimeError('Nema aktivnog dokumenta za spremanje')
    if not _watching:
        App.addDocumentObserver(_observer)
        Gui.addDocumentObserver(_observer)
        _watching = True
    session = _sessions.get(doc.Name)
    if session is None or session['document'] is not doc:
        session = {'document': doc, 'key': uuid.uuid4().hex, 'revision': 0}
        _sessions[doc.Name] = session
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    filename = _filename(doc)
    target = directory / filename
    token = uuid.uuid4().hex
    gui = Gui.getDocument(doc.Name)
    modified = bool(gui.Modified)
    revision = session['revision']
    parameters = App.ParamGet('User parameter:BaseApp/Preferences/Mod/Part/General')
    previous = parameters.GetBool('DirectAccess', True)
    document_parameters = App.ParamGet('User parameter:BaseApp/Preferences/Document')
    previous_thumbnail = document_parameters.GetBool('SaveThumbnail', True)
    _drnas_mark('ANDROID_SAVE SERIALIZE_BEGIN document=' + doc.Name
                + ' DirectAccess=False SaveThumbnail=False')
    try:
        parameters.SetBool('DirectAccess', False)
        # Gui::Document::Save normally queues Thumbnail::SaveDocFile, which
        # calls renderToImage with a separate offscreen GL path. It is optional
        # FCStd preview data; avoid that unvalidated GPU path on Android.
        document_parameters.SetBool('SaveThumbnail', False)
        doc.saveCopy(str(target))
        # saveCopy's Python wrapper returns None even if native saveCopy returns
        # false. Verify the actual payload before Android can truncate a target.
        with zipfile.ZipFile(target) as archive:
            if 'Document.xml' not in archive.namelist() or archive.testzip() is not None:
                raise ValueError('Spremljena FCStd arhiva nije ispravna')
        with target.open('rb') as stream:
            os.fsync(stream.fileno())
    finally:
        parameters.SetBool('DirectAccess', previous)
        document_parameters.SetBool('SaveThumbnail', previous_thumbnail)
        # Serializing a private copy must never announce an external save.
        gui.Modified = modified or session['revision'] != revision
    metadata = {'token': token, 'key': session['key'], 'document': doc.Name,
                'path': str(target), 'filename': filename, 'bytes': target.stat().st_size}
    temporary = directory / 'snapshot.json.tmp'
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(metadata, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, directory / 'snapshot.json')
    _pending[token] = {'document': doc, 'session': session, 'revision': revision}
    _drnas_mark('ANDROID_SAVE SERIALIZE_OK bytes=' + str(metadata['bytes']))
    return metadata


def finish_save(token, successful, mark_clean=True):
    request = _pending.pop(token, None)
    if request is None:
        return False
    if not successful:
        _drnas_mark('ANDROID_SAVE NOT_COMMITTED local_copy_retained=1')
        return False
    doc = request['document']
    try:
        same_document = App.getDocument(doc.Name) is doc
    except Exception:
        same_document = False
    # Never clear edits made while the provider was writing, or a newly opened
    # document that happens to reuse the original document's name.
    clean = (mark_clean and same_document and
             request['session']['revision'] == request['revision'])
    if clean:
        Gui.getDocument(doc.Name).Modified = False
    _drnas_mark('ANDROID_SAVE COMMITTED clean=' + str(bool(clean)))
    return bool(clean)
