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

CUBE = "bool NaviCubeImplementation::mousePressed(short x, short y)\n{\n    PickId pick = pickFace(x, y);\n    setHilite(pick);\n    if (pick == PickId::None) {\n        mouseDown = false;\n        dragStarted = false;\n        dragMode = MouseDragMode::None;\n        return false;\n    }\n\n    mouseDown = true;\n    dragStarted = false;\n    pressPos = SbVec2s(x, y);\n    lastDragPos = pressPos;\n\n    const FaceType faceType = getFaceType(pick);\n    const bool dragAllowed = faceType == FaceType::Main || faceType == FaceType::Edge\n        || faceType == FaceType::Corner;\n    dragMode = dragAllowed ? (draggable ? MouseDragMode::MoveNaviCube : MouseDragMode::RotateCamera)\n                           : MouseDragMode::None;\n    return true;\n}\n\nvoid NaviCubeImplementation::handleMenu()\n{\n    menu->exec(QCursor::pos());\n}\n\nbool NaviCubeImplementation::mouseReleased(short x, short y)\n{\n    static const float pi = boost::math::constants::pi<float>();\n\n    setHilite(PickId::None);\n    if (!mouseDown) {\n        return false;\n    }\n\n    const bool wasDragging = dragStarted;\n    const MouseDragMode releasedDragMode = dragMode;\n    mouseDown = false;\n    dragStarted = false;\n    dragMode = MouseDragMode::None;\n\n    if (wasDragging) {\n        if (releasedDragMode == MouseDragMode::RotateCamera) {\n            if (auto* navigation = viewer->navigationStyle()) {\n                navigation->endOrbitDrag();\n            }\n        }\n        resetClickState();\n    }\n    else {\n        PickId pickId = pickFace(x, y);\n        long step = std::clamp(long(naviStepByTurn), 4L, 36L);\n        float rotStepAngle = (2 * std::numbers::pi) / step;\n\n        FaceType faceType = getFaceType(pickId);\n        if (faceType == FaceType::Main || faceType == FaceType::Edge || faceType == FaceType::Corner) {\n            // FIXME: Quarter currently flattens Qt double-clicks into ordinary Coin button\n            // presses, so NaviCube has to detect the recenter gesture locally.\n            bool moveToCenter = clickTimer.isValid() && lastClickPickId == pickId\n                && clickTimer.elapsed() < QApplication::doubleClickInterval();\n\n            // Handle the cube faces\n            SbRotation orientation;\n            if (rotateToNearest) {\n                orientation = getNearestOrientation(pickId);\n            }\n            else {\n                orientation = getFaceRotation(pickId);\n            }\n            viewer->setCameraOrientation(orientation, moveToCenter);\n\n            if (moveToCenter) {\n                resetClickState();\n            }\n            else {\n                lastClickPickId = pickId;\n                clickTimer.start();\n            }\n            setHilite(PickId::None);\n        }\n        else if (faceType == FaceType::Button) {\n            // Handle the menu\n            if (pickId == PickId::ViewMenu) {\n                resetClickState();\n                setHilite(PickId::None);\n                handleMenu();\n                return true;\n            }\n            else if (pickId == PickId::Home) {\n                resetClickState();\n                viewer->viewHome();\n                setHilite(pickId);\n                return true;\n            }\n\n            // Handle the flat buttons\n            resetClickState();\n            SbRotation rotation = getFaceRotation(pickId);\n            if (pickId == PickId::Backside) {\n                rotation.scaleAngle(pi);\n            }\n            else {\n                rotation.scaleAngle(rotStepAngle);\n            }\n\n            // If the previous flat button animation is still active then apply the rotation to the\n            // previous target orientation, otherwise apply the rotation to the current camera\n            // orientation\n            if (flatButtonAnimation != nullptr\n                && flatButtonAnimation->state() == QAbstractAnimation::Running) {\n                flatButtonTargetOrientation = rotation * flatButtonTargetOrientation;\n            }\n            else {\n                flatButtonTargetOrientation = rotation * viewer->getCameraOrientation();\n            }\n\n            flatButtonAnimation = viewer->setCameraOrientation(flatButtonTargetOrientation);\n            setHilite(pickId);\n        }\n        else {\n            setHilite(PickId::None);\n            resetClickState();\n            return true;\n        }\n    }\n    return true;\n}\n\nvoid NaviCubeImplementation::setHilite(PickId hilite)\n{\n    if (hilite != hiliteId) {\n        hiliteId = hilite;\n        if (soNaviCube) {\n            soNaviCube->hiliteId = static_cast<int>(hiliteId);\n        }\n        viewer->getSoRenderManager()->scheduleRedraw();\n    }\n}\n\nbool NaviCubeImplementation::hasDraggedPastThreshold(short x, short y) const\n{\n    const long dx = x - pressPos[0];\n    const long dy = y - pressPos[1];\n    constexpr long minimumDragThreshold = 3;\n    const long threshold\n        = std::max(minimumDragThreshold, static_cast<long>(std::lround(3.0 * devicePixelRatio)));\n    return dx * dx + dy * dy >= threshold * threshold;\n}\n\nvoid NaviCubeImplementation::startCameraRotationDrag()\n{\n    constexpr float minCameraDistanceFactor = 1.05F;\n    constexpr float clippingRadiusFactor = 1.0F;\n    constexpr float dragSensitivity = 0.45F;\n\n    dragStarted = true;\n    setHilite(PickId::None);\n\n    if (auto* navigation = viewer->navigationStyle()) {\n        NavigationStyle::OrbitDragOptions options;\n        options.centerMode = NavigationStyle::OrbitDragOptions::CenterMode::SceneBoundingSphere;\n        options.minDistanceFactor = minCameraDistanceFactor;\n        options.clippingRadiusFactor = clippingRadiusFactor;\n        options.sensitivity = dragSensitivity;\n        navigation->beginOrbitDrag(options);\n    }\n}\n\nvoid NaviCubeImplementation::updateCameraRotationDrag(short x, short y)\n{\n    const int dx = x - lastDragPos[0];\n    const int dy = y - lastDragPos[1];\n    if (dx == 0 && dy == 0) {\n        return;\n    }\n\n    auto* navigation = viewer->navigationStyle();\n    const qreal physicalCubeWidgetSize = getPhysicalCubeWidgetSize();\n    if (!navigation || physicalCubeWidgetSize <= 0) {\n        lastDragPos = SbVec2s(x, y);\n        return;\n    }\n\n    const auto toProjectorPos = [physicalCubeWidgetSize](short value) {\n        return 0.5F + static_cast<float>(value) / static_cast<float>(physicalCubeWidgetSize);\n    };\n    const SbVec2f curpos(toProjectorPos(x), toProjectorPos(y));\n    const SbVec2f prevpos(toProjectorPos(lastDragPos[0]), toProjectorPos(lastDragPos[1]));\n    navigation->updateOrbitDrag(curpos, prevpos);\n\n    lastDragPos = SbVec2s(x, y);\n    requestRedraw(false);\n}\n\nbool NaviCubeImplementation::mouseMoved(short x, short y) { return false; }\n"

class CameraPatchTest(unittest.TestCase):
    def test_patch_is_android_only_and_rejects_drift_atomically(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            nav = root / 'src/Gui/Navigation/NavigationStyle.cpp'
            view = root / 'src/Gui/View3DInventorViewer.cpp'
            nav.parent.mkdir(parents=True)
            cube = root / 'src/Gui/NaviCube.cpp'
            cube.write_text(CUBE)
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
            patched_cube = cube.read_text()
            for label in ('NAVI_CUBE 01', 'NAVI_CUBE 02', 'NAVI_CUBE 03', 'NAVI_CUBE 04', 'NAVI_CUBE 05', 'NAVI_CUBE 06', 'NAVI_CUBE 07'):
                self.assertEqual(patched_cube.count(label), 2)
            self.assertIn('viewer->setCameraOrientation(orientation, moveToCenter);', patched_cube)

if __name__ == '__main__':
    unittest.main()
