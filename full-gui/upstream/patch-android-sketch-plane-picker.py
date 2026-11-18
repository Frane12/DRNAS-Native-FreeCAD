#!/usr/bin/env python3
"""Route Android's unselected Part Design sketch to the native plane picker."""
from pathlib import Path
import sys

ORIGINAL = '''    void tryFindSupport()
    {
        createBodyOrThrow();

        createSketchAndShowAttachment();
    }'''
REPLACEMENT = '''    void tryFindSupport()
    {
#ifdef __ANDROID__
        std::fprintf(stderr, "[NATIVE] SKETCH_PLANE_PICKER BODY_BEGIN\\n");
#endif
        createBodyOrThrow();

#ifdef __ANDROID__
        std::fprintf(stderr, "[NATIVE] SKETCH_PLANE_PICKER BODY_READY\\n");
        // The native feature picker already implements plane selection,
        // transaction cancellation and sketch attachment. Use it for a new
        // sketch with no support selected (including a selected Body itself).
        // Keep face, datum, sketch and multi-selection on the original path.
        const auto selection = Gui::Selection().getSelectionEx();
        bool bodyOnly = selection.size() == 1
            && selection.front().getObject() == activeBody;
        if (bodyOnly) {
            for (const auto& sub : selection.front().getSubNames()) {
                if (!sub.empty()) { bodyOnly = false; break; }
            }
        }
        if (selection.empty() || bodyOnly) {
            std::fprintf(stderr, "[NATIVE] SKETCH_PLANE_PICKER BEGIN selection=%zu bodyOnly=%d\\n",
                         selection.size(), bodyOnly);
            findAndSelectPlane();
            std::fprintf(stderr, "[NATIVE] SKETCH_PLANE_PICKER RETURN\\n");
            return;
        }
#endif
        createSketchAndShowAttachment();
    }'''
MARKER = '''
#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
int drnas_android_sketch_plane_picker_v1() { return 1; }
#endif
'''


def patch(root):
    path = root / 'src/Mod/PartDesign/Gui/SketchWorkflow.cpp'
    source = path.read_text()
    changes = [(ORIGINAL, REPLACEMENT),
               ('#include <map>\n', '#include <map>\n#include <cstdio>\n'),
               ('using namespace PartDesignGui;\n', 'using namespace PartDesignGui;\n' + MARKER)]
    for before, _ in changes:
        if source.count(before) != 1:
            raise RuntimeError('Pinned SketchWorkflow anchor changed: ' + repr(before[:100]))
    for before, after in changes:
        source = source.replace(before, after, 1)
    path.write_text(source)


if __name__ == '__main__':
    patch(Path(sys.argv[1]))
    print('SKETCH_PLANE_PICKER_V1: Android unselected sketches use real native feature picker')
