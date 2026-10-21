cmake_minimum_required(VERSION 3.22)
project(DRNAS_FreeCAD_Android_QtActivity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg SvgWidgets UiTools)
# Use Qt's official Android entrypoint and deployment settings instead of
# calling QApplication from the unrelated OCCT Java Activity.
qt_add_executable(drnas_freecad_qtgui
    main.cpp
    IntegratedGui.cpp
    NativeOcctViewport.cpp
)
target_link_libraries(drnas_freecad_qtgui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::SvgWidgets Qt6::UiTools)

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
