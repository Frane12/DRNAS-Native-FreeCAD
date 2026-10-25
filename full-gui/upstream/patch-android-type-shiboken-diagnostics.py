#!/usr/bin/env python3
"""Read-only ARM64 FreeCAD C++ type and Shiboken pointer diagnostics.

Used solely by diagnostics/partdesign-shiboken-20261026.
Must run after pinned upstream checkout and before its CMake build.
This patch deliberately changes NO FreeCAD types, GL operations or QWidget owners.
"""
from pathlib import Path
import sys


def replace_exact(path: Path, needle: str, replacement: str, marker: str):
    text = path.read_text(encoding="utf-8")
    if marker in text:
        raise RuntimeError(f"Already instrumented: {path}")
    count = text.count(needle)
    if count != 1:
        raise RuntimeError(f"Pinned FreeCAD source changed: {path}: {needle!r} count={count}")
    path.write_text(text.replace(needle, replacement, 1), encoding="utf-8")


def patch_source(root: Path):
    part = root / "src/Mod/PartDesign/App/AppPartDesign.cpp"
    part_needle = "    PartDesign::Body                        ::init();"
    part_diagnostic = r"""
#ifdef __ANDROID__
    // DRNAS_TYPE: only inspect native class ancestry; never repair registrations here.
    for (const char* name : {"App::GeoFeature", "Part::Feature", "Part::BodyBase",
                             "PartDesign::Body", "PartDesign::Pad",
                             "PartDesign::Pocket", "PartDesign::Mirrored"}) {
        const Base::Type type = Base::Type::fromName(name);
        const Base::Type geo = Base::Type::fromName("App::GeoFeature");
        if (type.isBad()) {
            std::fprintf(stderr, "[DRNAS_TYPE] name=%s registered=0\n", name);
            continue;
        }
        const Base::Type parent = type.getParent();
        const std::string_view parentName =
            parent.isBad() ? std::string_view("(none)") : parent.getName();
        std::fprintf(stderr,
            "[DRNAS_TYPE] name=%s registered=1 key=%u parent=%.*s derivedGeo=%d\n",
            name, static_cast<unsigned>(type.getKey()),
            static_cast<int>(parentName.size()), parentName.data(),
            !geo.isBad() && type.isDerivedFrom(geo));
    }
    std::fflush(stderr);
#endif"""
    replace_exact(part, part_needle, part_needle + "\n" + part_diagnostic,
                  "[DRNAS_TYPE]")
    text = part.read_text(encoding="utf-8")
    text = "#include <cstdio>\n#include <string_view>\n#include <Base/Type.h>\n" + text
    part.write_text(text, encoding="utf-8")

    sketch = root / "src/Mod/Sketcher/App/AppSketcher.cpp"
    sketch_needle = "    Sketcher::SketchObject::init();"
    sketch_diagnostic = r"""
#ifdef __ANDROID__
    {
        const Base::Type sketchType = Base::Type::fromName("Sketcher::SketchObject");
        const Base::Type geo = Base::Type::fromName("App::GeoFeature");
        const Base::Type base = Base::Type::fromName("Part::Part2DObject");
        std::fprintf(stderr,
            "[DRNAS_TYPE] Sketcher::SketchObject registered=%d "
            "derivedPart2D=%d derivedGeo=%d\n",
            !sketchType.isBad(),
            !sketchType.isBad() && !base.isBad() && sketchType.isDerivedFrom(base),
            !sketchType.isBad() && !geo.isBad() && sketchType.isDerivedFrom(geo));
        std::fflush(stderr);
    }
#endif"""
    replace_exact(sketch, sketch_needle, sketch_needle + "\n" + sketch_diagnostic,
                  "[DRNAS_TYPE]")
    sketch.write_text("#include <cstdio>\n#include <Base/Type.h>\n" +
                      sketch.read_text(encoding="utf-8"), encoding="utf-8")

    geo = root / "src/App/GeoFeatureGroupExtension.cpp"
    geo_needle = "    if (!obj->isDerivedFrom<App::GeoFeature>()) {"
    geo_diagnostic = r"""
    if (!obj->isDerivedFrom<App::GeoFeature>()) {
#ifdef __ANDROID__
        const Base::Type actual = obj->getTypeId();
        const Base::Type required = App::GeoFeature::getClassTypeId();
        const std::string_view actualName = actual.getName();
        std::fprintf(stderr,
            "[DRNAS_GEO_REJECT] container=%p actual=%.*s actualKey=%u "
            "expectedGeoKey=%u derivedGeo=%d\n",
            static_cast<void*>(obj),
            static_cast<int>(actualName.size()), actualName.data(),
            static_cast<unsigned>(actual.getKey()),
            static_cast<unsigned>(required.getKey()),
            actual.isDerivedFrom(required));
        std::fflush(stderr);
#endif"""
    replace_exact(geo, geo_needle, geo_diagnostic, "[DRNAS_GEO_REJECT]")
    geo.write_text("#include <cstdio>\n#include <string_view>\n" +
                   geo.read_text(encoding="utf-8"), encoding="utf-8")

    gui = root / "src/Gui/PythonWrapper.cpp"
    gui_needle = (
        "Py::Object PythonWrapper::fromQWidget(QWidget* widget, const char* className)\n{"
    )
    gui_diagnostic = r"""
Py::Object PythonWrapper::fromQWidget(QWidget* widget, const char* className)
{
#ifdef __ANDROID__
    // DRNAS_SHIBOKEN_PTR: inspect pointer sign bit before Python/PySide wrapping.
    // On Android tagged pointers may occupy the upper half of the uint64 range.
    static unsigned reported = 0;
    if (reported++ < 48) {
        const auto raw = reinterpret_cast<std::uintptr_t>(widget);
        const unsigned long long unsignedAddress =
            static_cast<unsigned long long>(raw);
        std::fprintf(stderr,
            "[DRNAS_SHIBOKEN_PTR] QWidget ptr=%p unsigned=%llu signBit=%d class=%s\n",
            static_cast<void*>(widget), unsignedAddress,
            static_cast<int>((raw >> (sizeof(raw) * 8 - 1)) & 1U),
            className ? className : "(auto)");
        std::fflush(stderr);
    }
#endif"""
    replace_exact(gui, gui_needle, gui_diagnostic, "[DRNAS_SHIBOKEN_PTR]")
    gui.write_text("#include <cstdio>\n#include <cstdint>\n" +
                   gui.read_text(encoding="utf-8"), encoding="utf-8")

    print("PASS: read-only native type, GeoFeature and Shiboken pointer diagnostics",
          flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch-android-type-shiboken-diagnostics.py <FreeCAD source>")
    patch_source(Path(sys.argv[1]))
