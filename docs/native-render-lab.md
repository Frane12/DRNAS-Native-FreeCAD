# DRNAS FreeCAD Native Render LAB
This isolated debug APK (package org.drnas.freecad.coreprobe, version 0.0.3) extracts actual OpenCASCADE B-Rep tessellation from box-minus-bore geometry and renders the triangles directly using Android OpenGL ES 2.0. C++ JNI returns packed XYZ + flat face normals. No fabricated cube is rendered. Touch: one finger orbit, pinch zoom, two fingers pan; use reset camera to restore initial view. Renderer is GLES2 and avoids Qt GUI dependency at this stage.

The FreeCAD source Base64/Vector3D JNI self-test is preserved, and output line breaks are fixed. Successful upstream FreeCADBase module subset and OCCT library caches are reused: this is a native Android geometry viewer, NOT yet FreeCADApp / Part workbench / parametric document engine.
APK build success proves compilation and packaging only; physical Android GPU functionality still requires testing.
