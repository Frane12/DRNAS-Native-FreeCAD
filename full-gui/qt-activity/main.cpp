#include <QApplication>
#include <QMainWindow>
extern "C" void* drnas_create_qt_gui_shell_after_qt_android_bootstrap();

int main(int argc,char** argv) {
    QApplication app(argc, argv); // created by Qt's official Android entrypoint
    auto* window=static_cast<QMainWindow*>(
       drnas_create_qt_gui_shell_after_qt_android_bootstrap());
    window->show();
    return app.exec();
}
