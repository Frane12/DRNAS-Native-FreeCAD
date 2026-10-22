# Android PartWorkbench crash isolation

The captured log from 2026-10-22 ends at `Gui.activateWorkbench('PartWorkbench')` inside Python execution, with no return marker. It does **not** yet prove that OpenGL caused the crash.

This branch adds an isolated Python diagnostic helper; it deliberately **does not alter application startup or hook up UI buttons**, because the Android GUI host code is not identified in the current default-branch tree.

## Integrate with the existing DRNAS debug panel

1. Ship `diagnostics/part_workbench_probe.py` with the app's Python files.
2. Use the same existing Python execution/GIL bridge that successfully executes `import FreeCADGui as Gui`. **Do not** run Python code from an arbitrary Qt/background thread.
3. Add three labeled buttons, `1 Core Part`, `2 Import PartGui`, `3 Activate Part`. Each button dispatches one independent command: `import part_workbench_probe; part_workbench_probe.run("core", "<app-private-absolute-log-path>")` (change the stage for the other two).
4. Copy `part-probe.log` with the normal DRNAS diagnostic report after a crash. Last `BEGIN`/`PRE_ACTIVATE` lacking an `END_OK` shows which step did not return.
5. Only if native failure persists, collect Android tombstone / logcat `Fatal signal` + backtrace to identify the actual .so and crashing frame. Python `except` cannot catch SIGSEGV or SIGABRT.
6. Keep automatic Part activation disabled while debugging; a successful `core` or `gui_import` test must not transition into `activate` automatically.

Note: `import PartGui` availability differs by FreeCAD build. A Python ImportError in stage 2 is diagnostic evidence, not proof of a native crash. Use isolated stages and restart after any crash.
