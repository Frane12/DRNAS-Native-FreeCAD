cmake_minimum_required(VERSION 3.22)
project(DRNAS_Android_FreeCAD_QtGuiShell LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg SvgWidgets UiTools GuiTools LinguistTools)
add_library(drnas_freecad_qt_gui_shell SHARED QtCADShell.cpp)
target_link_libraries(drnas_freecad_qt_gui_shell PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::SvgWidgets Qt6::UiTools)
target_link_options(drnas_freecad_qt_gui_shell PRIVATE -Wl,--no-undefined)
