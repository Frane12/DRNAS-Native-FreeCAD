"""Legacy FreeCAD PySide compatibility facade backed by genuine PySide6.

This is Python-only glue, NOT a replacement for compiled Android PySide6,
Shiboken6 or Pivy. Fail visibly if those bindings are unavailable.
"""
try:
    import PySide6
except ImportError as exc:
    raise ImportError(
        "DRNAS: PySide compatibility requires real Android ARM64 PySide6 "
        "and shiboken6 modules built for the bundled Python/Qt ABI"
    ) from exc

from . import QtCore, QtGui, QtSvg  # noqa: F401
__all__ = ["QtCore", "QtGui", "QtSvg"]
