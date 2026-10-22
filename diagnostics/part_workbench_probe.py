"""Crash-resilient, opt-in FreeCAD Android Part workbench probe.

Invoke from the existing Qt GUI process with the Python GIL held:
  import sys
  sys.path.insert(0, '<directory containing this file>')
  import part_workbench_probe as p
  p.run("core", "/path/in/app/private/files/part-probe.log")
  p.run("gui_import", "/path/in/app/private/files/part-probe.log")
  p.run("activate", "/path/in/app/private/files/part-probe.log")

Run ONE stage per button press, in order. Each checkpoint is fsync'ed before
calling native code, so the last BEGIN without END identifies the dangerous call.
No new windows, no OpenGL setup and no automatic workbench activation.
"""

import datetime
import os
import traceback

VALID_STAGES = ("core", "gui_import", "activate")


def _checkpoint(path, stage, state, info=""):
    timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
    line = "{} | PART_PROBE | {} | {} | {}\n".format(timestamp, stage, state, info)
    with open(path, "a", encoding="utf-8") as stream:
        stream.write(line)
        stream.flush()
        os.fsync(stream.fileno())


def run(stage, log_path):
    """Return True/False for Python errors; native abort may terminate process."""
    if stage not in VALID_STAGES:
        raise ValueError("Unknown probe stage: " + repr(stage))
    if not os.path.isabs(log_path):
        raise ValueError("Use an absolute app-private diagnostic log path")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    _checkpoint(log_path, stage, "BEGIN")
    try:
        if stage == "core":
            import Part
            _checkpoint(log_path, stage, "IMPORT_OK", str(getattr(Part, "__file__", "")))
        elif stage == "gui_import":
            import FreeCADGui as Gui
            _checkpoint(log_path, stage, "FREECADGUI_OK")
            # Do not activate here: isolate module import from activation.
            import PartGui
            _checkpoint(log_path, stage, "PARTGUI_OK", str(getattr(PartGui, "__file__", "")))
        elif stage == "activate":
            import FreeCADGui as Gui
            _checkpoint(log_path, stage, "PRE_ACTIVATE")
            Gui.activateWorkbench("PartWorkbench")
            _checkpoint(log_path, stage, "ACTIVATED")
        _checkpoint(log_path, stage, "END_OK")
        return True
    except BaseException:
        _checkpoint(log_path, stage, "PYTHON_ERROR", traceback.format_exc())
        return False
