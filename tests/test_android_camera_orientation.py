import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('camera_patch', ROOT / 'full-gui/upstream/patch-android-camera-orientation.py')
camera_patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(camera_patch)

NAV = '''#include "Navigation/NavigationAnimator.h"
std::shared_ptr<NavigationAnimation> NavigationStyle::setCameraOrientation(
    const SbRotation& orientation, const SbBool moveToCenter) const
{
    SoCamera* camera = getCamera();
    if (!camera) return {};
    if (!canChangeCameraOrientation(camera->orientation.getValue(), orientation, OrientationChangeSource::Programmatic)) return {};
    animator->stop();
    const SbVec3f focalPoint = viewer->getFocalPoint();
    SbVec3f translation(0, 0, 0);
    if (moveToCenter) {
        SoGetBoundingBoxAction action(viewer->getSoRenderManager()->getViewportRegion());
        action.apply(viewer->getSceneGraph());
        SbBox3f box = action.getBoundingBox();
        if (!box.isEmpty()) translation = box.getCenter() - focalPoint;
    }
    if (isAnimationEnabled()) {
        return viewer->startAnimation(orientation, focalPoint, translation);
    }
    const SbVec3f rotationCenterDistanceCam = camera->focalDistance.getValue() * SbVec3f(0, 0, 1);
    setCameraOrientationValue(camera, orientation, OrientationChangeSource::Programmatic);
    SbVec3f newRotationCenterDistance;
    camera->orientation.getValue().multVec(rotationCenterDistanceCam, newRotationCenterDistance);
    camera->position = focalPoint + newRotationCenterDistance + translation;
    return {};
}
std::shared_ptr<NavigationAnimation> NavigationStyle::translateCamera() const { return {}; }
'''
VIEW = '''std::shared_ptr<NavigationAnimation> View3DInventorViewer::setCameraOrientation(
    const SbRotation& orientation, const bool moveToCenter) const
{
    SoCamera* camera = getCamera();
    if (!camera) return {};
    return navigation->setCameraOrientation(orientation, moveToCenter);
}
void View3DInventorViewer::setCameraType(int) {}
'''

class CameraPatchTest(unittest.TestCase):
    def test_patch_is_android_only_and_rejects_drift_atomically(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            nav = root / 'src/Gui/Navigation/NavigationStyle.cpp'
            view = root / 'src/Gui/View3DInventorViewer.cpp'
            nav.parent.mkdir(parents=True)
            nav.write_text(NAV)
            view.write_text(VIEW.replace('    SoCamera* camera = getCamera();', ''))
            with self.assertRaises(RuntimeError):
                camera_patch.patch(root)
            self.assertEqual(nav.read_text(), NAV)
            view.write_text(VIEW)
            camera_patch.patch(root)
            patched = nav.read_text()
            self.assertIn('#ifndef __ANDROID__\n    if (isAnimationEnabled())', patched)
            self.assertIn('camera->position = focalPoint + newRotationCenterDistance + translation;', patched)
            for label in ('CAMERA 01', 'CAMERA 02', 'CAMERA 04', 'CAMERA 05'):
                self.assertIn(label, patched)
            self.assertIn('CAMERA 00 viewer', view.read_text())

if __name__ == '__main__':
    unittest.main()
