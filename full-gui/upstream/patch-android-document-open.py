#!/usr/bin/env python3
"""Patch pinned upstream FreeCAD file selection for Android SAF content URIs.

Run after checkout of the original FreeCAD sources and before compiling GUI.
Only changes FileDialog.cpp, without modifying Coin3D, render or touch logic.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("Usage: patch-android-document-open.py <freecad-source-dir>")

source_file = Path(sys.argv[1]) / "src/Gui/FileDialog.cpp"
source = source_file.read_text(encoding="utf-8")

header_anchor = "#include <QUrl>\n"
native_anchor = "#ifndef FC_OS_WIN32\nQStringList FileDialogInternal::nativeFileDialog("
return_anchor = "    return selected;\n}\n#endif\n"
if source.count("DRNAS_SAF_OPEN_BRIDGE") != 0:
    print("DRNAS Android SAF file-open bridge already installed")
    sys.exit(0)
for anchor in (header_anchor, native_anchor, return_anchor):
    if source.count(anchor) != 1:
        raise SystemExit(f"Unexpected pinned FreeCAD FileDialog.cpp layout: {anchor!r}")

android_headers = """#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJniObject>
#include <QMessageBox>
#endif
"""
source = source.replace(header_anchor, header_anchor + android_headers, 1)

native_helper = r"""#ifdef Q_OS_ANDROID
namespace {
// DRNAS_SAF_OPEN_BRIDGE: preserve the provider's real filename/extension.
// A content:// URI is not a local filesystem filename. The selected Qt
// file dialog returns such a URI, while FreeCAD's import handler expects
// a regular file path and reads its suffix via QFileInfo.
QString drnasPrepareSelectedAndroidDocument(const QString& path, QWidget* parent)
{
    if (!path.startsWith(QStringLiteral("content://"), Qt::CaseInsensitive)) {
        return path;
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        QMessageBox::warning(parent, QObject::tr("Cannot Open File"),
                             QObject::tr("Android file access context is unavailable."));
        return {};
    }

    const QJniObject uri = QJniObject::fromString(path);
    const QJniObject converted = QJniObject::callStaticObjectMethod(
        "org/drnas/freecad/bootstrap/AndroidDocumentBridge",
        "cacheForOpen",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object<jobject>(),
        uri.object<jstring>()
    );

    const QString localPath = converted.isValid() ? converted.toString() : QString();
    if (localPath.isEmpty() || !QFileInfo(localPath).isFile()) {
        qWarning().noquote() << "[DRNAS_SAF] Android document staging failed:" << localPath;
        const QString cause = localPath.startsWith(QStringLiteral("ERROR:"))
            ? localPath.mid(6).trimmed()
            : QObject::tr("Android file bridge unavailable or no local file returned.");
        QMessageBox::warning(parent, QObject::tr("Cannot Open File"),
                             QObject::tr("Failed to read the selected Android document:\n%1")
                                 .arg(cause));
        return {};
    }
    qInfo().noquote() << "[DRNAS_SAF] Document staged for FreeCAD:"
                      << QFileInfo(localPath).fileName();
    return localPath;
}
}  // namespace
#endif

"""
source = source.replace(native_anchor, native_helper + native_anchor, 1)
replacement = """#ifdef Q_OS_ANDROID
    // Native Android picker returned content://, not an ordinary file path.
    // Stage every selected document before FreeCAD detects the file format.
    // Do not alter Save mode: Android SAF output needs a separate write flow.
    if (mode != NativeFileDialogMode::Save) {
        QStringList staged;
        for (const QString& file : selected) {
            if (file.isEmpty()) {
                continue;
            }
            const QString localPath = drnasPrepareSelectedAndroidDocument(file, parent);
            if (localPath.isEmpty()) {
                return {};
            }
            staged << localPath;
        }
        selected = staged;
    }
#endif
    return selected;
}
#endif
"""
source = source.replace(return_anchor, replacement, 1)
source_file.write_text(source, encoding="utf-8")
print("PASS: Android content:// documents stage under app cache with original extension")
