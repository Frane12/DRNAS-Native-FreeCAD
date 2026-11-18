# Deferred Measure menu composition

The Android runtime deliberately gates eager `Measure`/`MeasureGui` imports;
AUTO MEASURE remains opt-in after the first completed Coin frame. Upstream
`StdWorkbench` nevertheless inserts `Std_Measure` and `Std_MassProperties` into
the Tools menu and View toolbar unconditionally. Every workbench rebuild then
calls `CommandManager::addTo` for absent commands, repeatedly filling the GUI
notification table with warnings.

The Android-only Workbench patch inserts each optional command only after it
exists in the genuine command registry. It does not modify CommandManager's
warning handling, implement replacement commands, eagerly import Measure,
change the automatic-import setting or modify rendering/input code. Desktop
menu construction stays unchanged.

After a successful delayed MeasureGui import, the runtime verifies both real
commands and calls the upstream workbench `reloadActive` once. If registration
or menu rebuilding fails, the original error remains visible in diagnostics.
A new native export marker prevents packaging a stale FreeCADGui binary.

Validation: compiled generated C++ menu/toolbar composition for Android and
desktop, with absent/partial/full registration across repeated rebuilds;
runtime refresh tests cover success, missing commands, no workbench and error
propagation. Six new tests and nine existing Measure/assets/native diagnostic
tests pass. Actual pinned Workbench.cpp patch anchors and build-generator,
shell and YAML syntax were checked. Android CI and phone validation are
separate checks.
