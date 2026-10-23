cmake_minimum_required(VERSION 3.22)
project(DRNAS_FreeCAD_Android_QtActivity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg SvgWidgets UiTools Network PrintSupport Xml Concurrent)
# Use Qt's official Android entrypoint and deployment settings instead of
# calling QApplication from the unrelated OCCT Java Activity.
# FULL upstream FreeCAD uses its own Coin3D/OpenGL viewport.
# Do not even compile the separate Qt/OCCT OpenGL startup viewer into that
# APK; it can initialize an incompatible Android EGL/FBO before FreeCADGui.
# Preserve the old viewport for the manually dispatched standalone OCCT LAB.
set(DRNAS_QT_ACTIVITY_SOURCES main.cpp IntegratedGui.cpp StartupDiagnostics.cpp)
if(NOT DRNAS_FREECAD_RUNTIME_DIR)
    list(APPEND DRNAS_QT_ACTIVITY_SOURCES NativeOcctViewport.cpp)
endif()
qt_add_executable(drnas_freecad_qtgui ${DRNAS_QT_ACTIVITY_SOURCES})
if(DRNAS_FREECAD_RUNTIME_DIR)
    target_compile_definitions(drnas_freecad_qtgui PRIVATE
        DRNAS_FREECAD_FULL_INTEGRATION=1)
    message(STATUS "FULL FREECAD NO-PREVIEW: bootstrap is Qt Widgets only; FreeCAD owns OpenGL")
endif()
target_link_libraries(drnas_freecad_qtgui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::SvgWidgets Qt6::UiTools
    # Real FreeCADGui and FreeCADApp directly DT_NEEDED these modules.
    # Merely putting FreeCADGui.so in QT_ANDROID_EXTRA_LIBS does NOT cause
    # androiddeployqt to include their transitive Qt6 libraries.
    Qt6::Network Qt6::PrintSupport Qt6::Xml Qt6::Concurrent)
if(ANDROID)
    target_link_libraries(drnas_freecad_qtgui PRIVATE log)
endif()

# Real upstream OCCT 7.9.3 native libraries, reused from the stable
# Android ARM64 FreeCAD viewport pipeline (no OCCT recompilation).
if(NOT OCCT_ANDROID_ROOT)
    message(FATAL_ERROR "OCCT_ANDROID_ROOT must point at the verified Android OCCT SDK")
endif()
set(OCCT_INCLUDE "${OCCT_ANDROID_ROOT}/include/opencascade")
set(OCCT_LIB "${OCCT_ANDROID_ROOT}/lib")
if(NOT EXISTS "${OCCT_INCLUDE}/TopoDS_Shape.hxx")
    message(FATAL_ERROR "Native Android OpenCASCADE SDK headers missing")
endif()
target_include_directories(drnas_freecad_qtgui PRIVATE "${OCCT_INCLUDE}")
foreach(lib TKBO TKBool TKPrim TKMesh TKTopAlgo TKGeomAlgo
            TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel)
    set(path "${OCCT_LIB}/lib${lib}.so")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Verified Android OCCT module missing: ${path}")
    endif()
    target_link_libraries(drnas_freecad_qtgui PRIVATE "${path}")
endforeach()
# Official Qt CMake deployment feature: copy all transitive Android OCCT
# shared libraries into the installable APK, alongside Qt6 runtime libraries.
file(GLOB ANDROID_OCCT_LIBS "${OCCT_LIB}/libTK*.so")
list(SORT ANDROID_OCCT_LIBS)
# FreeCAD shared objects are present in the *final signed APK*, but must
# NOT be added to QtLoader's eager native load list: Android loads its
# QT_ANDROID_EXTRA_LIBS before main(), making a premature SIGABRT invisible.
# androiddeployqt automatically packages Qt6 and the working OCCT viewer.
if(DRNAS_FREECAD_RUNTIME_DIR)
    foreach(required libFreeCADBase.so libFreeCADApp.so libFreeCADGui.so libCoin.so libGL.so libGLU.so)
        if(NOT EXISTS "${DRNAS_FREECAD_RUNTIME_DIR}/${required}")
            message(FATAL_ERROR "Native upstream FreeCAD/GL4ES runtime missing: ${required}")
        endif()
    endforeach()
    # CRITICAL: QT_ANDROID_EXTRA_LIBS are loaded by Android's QtLoader
    # BEFORE our C++ main() and BEFORE any GUI can report a crash. Do not
    # register upstream FreeCADGui/Coin3D here. Package them separately by
    # signed APK injection, leaving QtLoader's previously working OCCT
    # startup list untouched. The user tests them by opt-in dlopen.
    message(STATUS "SAFE MODE: upstream FreeCADGui runtime is bundled after Qt packaging, not eagerly loaded by QtLoader")
endif()
set_target_properties(drnas_freecad_qtgui PROPERTIES
    QT_ANDROID_EXTRA_LIBS "${ANDROID_OCCT_LIBS}")
set_target_properties(drnas_freecad_qtgui PROPERTIES
    QT_ANDROID_PACKAGE_NAME "org.drnas.freecad.qtgui"
    QT_ANDROID_APP_NAME "DRNAS FreeCAD Qt GUI LAB"
    QT_ANDROID_MIN_SDK_VERSION 26
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_COMPILE_SDK_VERSION 35
    QT_ANDROID_VERSION_CODE 2
    QT_ANDROID_VERSION_NAME "0.2.0-occt"
)

if(DRNAS_FREECAD_RUNTIME_DIR)
    set_target_properties(drnas_freecad_qtgui PROPERTIES
        QT_ANDROID_PACKAGE_NAME "org.drnas.freecad.integrated"
        QT_ANDROID_APP_NAME "DRNAS FreeCAD Native GUI Integration"
        QT_ANDROID_VERSION_CODE 3
        QT_ANDROID_VERSION_NAME "0.3.0-alpha"
    )
endif()

# Android-only startup screen runs before Qt's native libraries/QApplication.
# Change UI scale before context creation, preserving Qt's input-coordinate mapping.
if(ANDROID AND DRNAS_FREECAD_RUNTIME_DIR)
    set_target_properties(drnas_freecad_qtgui PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
endif()
