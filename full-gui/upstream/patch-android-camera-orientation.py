#!/usr/bin/env python3
"""Restore upstream camera animation on Android and trace its stages."""
from pathlib import Path
import sys

def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Pinned camera source changed: ' + repr(old[:120]))
    return source.replace(old, new, 1)

def patch(root):
    path = root / 'src/Gui/Navigation/NavigationStyle.cpp'
    source = path.read_text()
    source = once(source, '#include "Navigation/NavigationAnimator.h"',
                  '#include "Navigation/NavigationAnimator.h"\n#ifdef __ANDROID__\n#include <Base/AndroidDocumentDiagnostics.h>\nextern "C" __attribute__((visibility("default"))) int drnas_android_camera_direct_v1() { return 1; }\nextern "C" __attribute__((visibility("default"))) int drnas_android_camera_animation_v1() { return 1; }\n#endif')
    signature = 'std::shared_ptr<NavigationAnimation> NavigationStyle::setCameraOrientation('
    start = source.index(signature)
    end = source.index('\nstd::shared_ptr<NavigationAnimation> NavigationStyle::translateCamera(', start)
    body = source[start:end]
    def step(old, label):
        nonlocal body
        body = once(body, old, '#ifdef __ANDROID__\n    drnasDocumentPhase("CAMERA ' + label + '", "BEGIN");\n#endif\n    ' + old + '\n#ifdef __ANDROID__\n    drnasDocumentPhase("CAMERA ' + label + '", "END");\n#endif')
    step('animator->stop();', '01 stop animation')
    step('const SbVec3f focalPoint = viewer->getFocalPoint();', '02 focal point')
    body = once(body, '    if (isAnimationEnabled()) {\n        return viewer->startAnimation(orientation, focalPoint, translation);\n    }',
                '    if (isAnimationEnabled()) {\n#ifdef __ANDROID__\n        drnasDocumentPhase("CAMERA 03 animation", "BEGIN");\n#endif\n        return viewer->startAnimation(orientation, focalPoint, translation);\n    }\n#ifdef __ANDROID__\n    drnasDocumentPhase("CAMERA 03 direct orientation", "BEGIN");\n#endif')
    step('setCameraOrientationValue(camera, orientation, OrientationChangeSource::Programmatic);', '04 set orientation')
    step('camera->position = focalPoint + newRotationCenterDistance + translation;', '05 set position')
    source = source[:start] + body + source[end:]

    viewer_path = root / 'src/Gui/View3DInventorViewer.cpp'
    viewer = viewer_path.read_text()
    signature = 'std::shared_ptr<NavigationAnimation> View3DInventorViewer::setCameraOrientation('
    start = viewer.index(signature)
    end = viewer.index('\nvoid View3DInventorViewer::setCameraType(', start)
    section = viewer[start:end]
    section = once(section, '    SoCamera* camera = getCamera();',
                   '#ifdef __ANDROID__\n    drnasDocumentPhase("CAMERA 00 viewer", "BEGIN");\n#endif\n    SoCamera* camera = getCamera();')
    section = once(section, '    return navigation->setCameraOrientation(orientation, moveToCenter);',
                   '#ifdef __ANDROID__\n    drnasDocumentPhase("CAMERA 00 navigation", "BEGIN");\n#endif\n    return navigation->setCameraOrientation(orientation, moveToCenter);')
    viewer = viewer[:start] + section + viewer[end:]
    include = '#include <Base/AndroidDocumentDiagnostics.h>\n'
    if include not in viewer:
        viewer = '#ifdef __ANDROID__\n' + include + '#endif\n' + viewer

    # Trace the real NaviCube event path only; preserve selection and camera behavior.
    cube_path = root / 'src/Gui/NaviCube.cpp'
    cube = cube_path.read_text()
    include = '#include <Base/AndroidDocumentDiagnostics.h>\n'
    if include not in cube:
        cube = '#ifdef __ANDROID__\n' + include + '#endif\n' + cube

    def trace_section(signature, end_signature, replacements):
        nonlocal cube
        start = cube.index(signature)
        end = cube.index(end_signature, start)
        section = cube[start:end]
        for statement, label in replacements:
            before = '#ifdef __ANDROID__\n    drnasDocumentPhase("NAVI_CUBE ' + label + '", "BEGIN");\n#endif\n'
            after = '\n#ifdef __ANDROID__\n    drnasDocumentPhase("NAVI_CUBE ' + label + '", "END");\n#endif'
            section = once(section, statement, before + statement + after)
        cube = cube[:start] + section + cube[end:]

    trace_section('bool NaviCubeImplementation::mousePressed(short x, short y)',
                  '\nvoid NaviCubeImplementation::handleMenu()', [
        ('    PickId pick = pickFace(x, y);', '01 press pick'),
        ('    setHilite(pick);', '02 press highlight'),
    ])
    trace_section('bool NaviCubeImplementation::mouseReleased(short x, short y)',
                  '\nbool NaviCubeImplementation::mouseMoved(', [
        ('    static const float pi = boost::math::constants::pi<float>();\n\n    setHilite(PickId::None);', '03 release highlight'),
        ('        PickId pickId = pickFace(x, y);', '04 release pick'),
        ('                orientation = getNearestOrientation(pickId);', '05 nearest orientation'),
        ('                orientation = getFaceRotation(pickId);', '06 face orientation'),
        ('            viewer->setCameraOrientation(orientation, moveToCenter);', '07 camera orientation'),
    ])
    path.write_text(source)
    viewer_path.write_text(viewer)
    cube_path.write_text(cube)

if __name__ == '__main__':
    patch(Path(sys.argv[1]))
    print('CAMERA_ANIMATION_V1: upstream animation restored with direct fallback')
