"""PySide1-style QtGui namespace: Qt 6 moved widgets to QtWidgets."""
from PySide6.QtGui import *  # noqa: F401,F403
from PySide6.QtWidgets import *  # noqa: F401,F403
from PySide6 import QtGui as _QtGui, QtWidgets as _QtWidgets

def __getattr__(name):
    if hasattr(_QtWidgets, name):
        return getattr(_QtWidgets, name)
    return getattr(_QtGui, name)
