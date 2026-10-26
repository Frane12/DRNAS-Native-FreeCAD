#!/usr/bin/env python3
"""Android-only Sketcher constraint icon capture, source -> composed -> Coin bytes.

This diagnostic patches one pinned upstream file. It does not alter actual
constraint geometry, image pixels, alpha compositing or GL state.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])
path = root / "src/Mod/Sketcher/Gui/EditModeConstraintCoinManager.cpp"
code = path.read_text()

def once(old, new):
    global code
    if code.count(old) != 1:
        raise SystemExit(f"DRNAS ICON CAPTURE: changed upstream anchor: {old[:110]!r}, matches={code.count(old)}")
    code = code.replace(old, new, 1)

once(
    "#include <QPainter>\n",
    "#include <QPainter>\n"
    "#include <QDir>\n"
    "#include <QDebug>\n"
    "#include <QSet>\n"
    "#include <QStandardPaths>\n",
)

helper = r"""
// Diagnostic: capture true upstream Sketcher images at three checkpoints.
// Bounded once per horizontal/vertical icon to avoid render-loop I/O.
static void drnasCaptureSketcherIcon(const QString& key, const QImage& src)
{
    const QString directory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/diagnostics/sketcher-icons");
    if (src.isNull() || src.width() <= 0 || src.height() <= 0
        || src.width() > 1024 || src.height() > 1024) {
        qWarning().noquote() << "SKETCHER_NATIVE_ICON INVALID" << key
                             << "size=" << src.size();
        return;
    }
    const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
    int transparent = 0, partial = 0, opaque = 0;
    for (int y=0; y<img.height(); ++y) {
        for (int x=0; x<img.width(); ++x) {
            const int a = img.pixelColor(x,y).alpha();
            if (a == 0) ++transparent;
            else if (a == 255) ++opaque;
            else ++partial;
        }
    }
    const bool folderOK = QDir().mkpath(directory);
    const QString output = directory + QStringLiteral("/") + key + QStringLiteral(".png");
    const bool pngOK = folderOK && img.save(output, "PNG");
    qWarning().noquote() << "SKETCHER_NATIVE_ICON"
                         << key << "size=" << img.width() << "x" << img.height()
                         << "zero=" << transparent << "partial=" << partial
                         << "opaque=" << opaque
                         << "png=" << pngOK << "path=" << output;
}
"""
once("using namespace Sketcher;\n",
     "using namespace Sketcher;\n" + helper)

anchor_source = """    QImage icon = pxMap.toImage();
    // The pixmap was already scaled so we don't need to scale the image"""
replacement_source = """    QImage icon = pxMap.toImage();
    // Capture original bitmap returned by FreeCAD BitmapFactory, NOT a
    // separately rendered SVG. Only H/V are sampled, once per session.
    static QSet<QString> drnasInitialCaptured;
    const bool drnasTrack = (type == QStringLiteral("Constraint_Horizontal")
                          || type == QStringLiteral("Constraint_Vertical"))
                          && !drnasInitialCaptured.contains(type);
    if (drnasTrack) {
        drnasInitialCaptured.insert(type);
        drnasCaptureSketcherIcon(type + QStringLiteral("-source"), icon);
    }
    // The pixmap was already scaled so we don't need to scale the image"""
once(anchor_source, replacement_source)

# This exact return belongs to renderConstrIcon only.
once(
    """    return image;
}

void EditModeConstraintCoinManager::drawTypicalConstraintIcon""",
    """    if (drnasTrack) {
        drnasCaptureSketcherIcon(type + QStringLiteral("-composed"), image);
    }
    return image;
}

void EditModeConstraintCoinManager::drawTypicalConstraintIcon""",
)

once(
    """    sendConstraintIconToCoin(image, i.destination);
}

QString EditModeConstraintCoinManager::iconTypeFromConstraint""",
    """    sendConstraintIconToCoin(image, i.destination);

    // Read back the *CPU-side Coin SoSFImage bytes*, not GPU pixels.
    // If composed PNG is correct and Coin PNG differs, conversion is at fault.
    static QSet<QString> drnasCoinCaptured;
    if ((i.type == QStringLiteral("Constraint_Horizontal")
         || i.type == QStringLiteral("Constraint_Vertical"))
        && !drnasCoinCaptured.contains(i.type)) {
        drnasCoinCaptured.insert(i.type);
        SbVec2s dims;
        int components = 0;
        const unsigned char* raw = i.destination->image.getValue(dims, components);
        qWarning().noquote() << "SKETCHER_NATIVE_ICON Coin bytes"
                             << i.type << "components=" << components
                             << "w=" << dims[0] << "h=" << dims[1]
                             << "data=" << (raw != nullptr);
        if (raw && components == 4 && dims[0] > 0 && dims[1] > 0
            && dims[0] <= 1024 && dims[1] <= 1024) {
            const QImage rgba(raw, dims[0], dims[1], dims[0]*4,
                              QImage::Format_RGBA8888);
            drnasCaptureSketcherIcon(i.type + QStringLiteral("-coin"), rgba);
        }
    }
}

QString EditModeConstraintCoinManager::iconTypeFromConstraint""",
)
path.write_text(code)
print("SKETCHER_NATIVE_ICON: captured BitmapFactory, composition and Coin SoSFImage for H/V")
