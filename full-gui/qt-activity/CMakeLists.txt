cmake_minimum_required(VERSION 3.22)
project(DRNAS_FreeCAD_Android_QtActivity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg SvgWidgets UiTools)
# Use Qt's official Android entrypoint and deployment settings instead of
# calling QApplication from the unrelated OCCT Java Activity.
qt_add_executable(drnas_freecad_qtgui
    main.cpp
    ../qt-widgets-shell/QtCADShell.cpp
)
target_link_libraries(drnas_freecad_qtgui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::SvgWidgets Qt6::UiTools)
set_target_properties(drnas_freecad_qtgui PROPERTIES
    QT_ANDROID_PACKAGE_NAME "org.drnas.freecad.qtgui"
    QT_ANDROID_APP_NAME "DRNAS FreeCAD Qt GUI LAB"
    QT_ANDROID_MIN_SDK_VERSION 26
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_COMPILE_SDK_VERSION 35
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1.0"
)
