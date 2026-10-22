"""FreeCAD legacy QtCore import facade."""
from PySide6.QtCore import *  # noqa: F401,F403
from PySide6 import QtCore as _QtCore

def __getattr__(name):
    return getattr(_QtCore, name)
