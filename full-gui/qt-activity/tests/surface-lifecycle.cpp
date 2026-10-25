// SPDX-License-Identifier: LGPL-2.1-or-later
// Guard against reintroducing the v2 startup freeze: the native surface
// observer must never suppress Qt's QWidget painting/composition lifecycle.
#include "../DrnasRenderSurfaceGuard.h"
#include <QApplication>
#include <QMainWindow>
#include <QPaintEvent>
#include <QTest>
#include <QDebug>
#include <iostream>

namespace DrnasDiagnostics {
void note(const QString& message) { qInfo().noquote() << message; }
}

class PaintProbe : public QWidget {
public:
    int frames=0;
protected:
    void paintEvent(QPaintEvent* event) override {
        ++frames;
        QWidget::paintEvent(event);
    }
};

void check(bool condition, const char* message) {
    if (!condition) qFatal("%s", message);
}

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    QMainWindow host;
    auto* canvas=new PaintProbe;
    host.setCentralWidget(canvas);
    host.resize(400,869);
    new DrnasRenderSurfaceGuard(&host);
    check(host.updatesEnabled(), "observer must not disable host updates before show");
    check(canvas->updatesEnabled(), "observer must not disable child updates before show");

    host.show();
    QTest::qWait(150);
    check(host.updatesEnabled() && canvas->updatesEnabled(),
          "observer must not suspend Qt composition during first expose");
    const int first=canvas->frames;
    canvas->update();
    QTest::qWait(80);
    check(canvas->frames>first, "paint requests must pass to the visible canvas");

    host.hide();
    QTest::qWait(40);
    check(host.updatesEnabled() && canvas->updatesEnabled(),
          "observer must not disable updates when the host is hidden");

    host.show();
    QTest::qWait(150);
    const int second=canvas->frames;
    canvas->update();
    QTest::qWait(80);
    check(canvas->frames>second, "canvas must repaint after restoring the host");
    check(host.size()==QSize(400,869), "surface observation must not resize the host");
    std::cout << "Passive Qt surface lifecycle and repaint checks passed\n";
}
