"""Legacy PySide namespace backed by the genuine Android PySide6 QtWidgets."""
from PySide6.QtWidgets import *  # noqa: F401,F403
from PySide6 import QtWidgets as _QtWidgets


def __getattr__(name):
    return getattr(_QtWidgets, name)
