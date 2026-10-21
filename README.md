# DRNAS Native FreeCAD — Android ARM64

An **independent Android porting effort** based on the upstream [FreeCAD](https://github.com/FreeCAD/FreeCAD) and [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT) source code.

**Phase 1:** Cross-compile OCCT V7_9_3 geometric modelling libraries on a Linux x86_64 GitHub runner **for Android arm64-v8a/Bionic** using the Android NDK. This does **not** yet build the FreeCAD executable, GUI, or APK.

## Build
- Push to `main` or open **Actions → DRNAS FreeCAD – Android ARM64 OCCT → Run workflow**.
- The workflow produces verifiable Android AArch64 `.so` libraries plus headers, an ELF audit, SHA-256 manifest, logs, and the source SHA.
- The native compiler test verifies Android PIE (`ET_DYN`) with `/system/bin/linker64` before the costly build.
- Release binaries are not claimed until the job actually succeeds.

## Persistent build cache
- **ccache 2 GB** using NDK Clang as a compiler launcher; restores previous cache from the same branch and saves even on compile failure.
- **Verified installed OCCT libraries** cached separately and **only after passing ELF validation**. A build/verification-script change invalidates this cache.
- Outputs are also uploaded as GitHub Actions artifacts; caches may expire and are not releases.

## Roadmap
1. OCCT FoundationClasses/ModelingData/ModelingAlgorithms on Android arm64-v8a.
2. Native CAD geometry test (box/boolean/STEP); turn on DataExchange as needed.
3. Cross-compile FreeCAD core, Part workbench, Python integration and their dependencies.
4. Qt/3D viewport integration, touch interface and APK.

Android is **not** generic Linux ARM64: the target links against Bionic and uses the Android dynamic linker, not Linux glibc. The port build system and Android-specific changes are independent work; upstream ownership and licenses remain with the respective authors.