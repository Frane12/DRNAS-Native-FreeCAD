#pragma once

// DRNAS: single-window Android/Qt diagnostic panel.
// This panel is always a child of the QtActivity host: it MUST NOT create a
// QDialog, QWindow, QOpenGLWidget, new EGLSurface or a second QApplication.
#include "StartupDiagnostics.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

class DrnasDebugCenter final : public QObject {
    QPointer<QMainWindow> host_;
    QPushButton* launcher_ = nullptr;
    QScrollArea* panel_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* feedback_ = nullptr;
    QCheckBox* fullscreen_ = nullptr;
    QCheckBox* immersive_ = nullptr;
    QCheckBox* verbose_ = nullptr;
    QComboBox* windowSize_ = nullptr;
    QComboBox* uiScale_ = nullptr;
    QFont baseFont_;
    bool syncing_ = false;
    QString lastState_;
    static inline DrnasDebugCenter* active_ = nullptr;

    static QString geometry(const QRect& r) {
        return QStringLiteral("(%1,%2) %3x%4")
            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    static QString format(const QSurfaceFormat& f) {
        return QStringLiteral("GL version=%1.%2 ES=%3 depth=%4 stencil=%5 "
                              "samples=%6 swap=%7")
            .arg(f.majorVersion()).arg(f.minorVersion())
            .arg(f.renderableType() == QSurfaceFormat::OpenGLES)
            .arg(f.depthBufferSize()).arg(f.stencilBufferSize())
            .arg(f.samples()).arg(int(f.swapBehavior()));
    }
    static QString nativeProcessState() {
        // Read-only /proc/self introspection: no dlopen, glGetString,
        // eglInitialize or GL context creation while diagnosing a live viewport.
        QString report;
        QFile stat(QStringLiteral("/proc/self/status"));
        if (stat.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> wanted{"VmRSS:", "VmPeak:", "VmHWM:", "Threads:"};
            while (!stat.atEnd()) {
                const QByteArray line = stat.readLine().trimmed();
                for (const QByteArray& key : wanted) {
                    if (line.startsWith(key)) {
                        report += QStringLiteral("\\nProcess ") + QString::fromLatin1(line);
                        break;
                    }
                }
            }
        }
        QFile maps(QStringLiteral("/proc/self/maps"));
        QSet<QString> found;
        if (maps.open(QIODevice::ReadOnly)) {
            while (!maps.atEnd()) {
                const QByteArray line = maps.readLine();
                if (line.contains("libEGL") || line.contains("libGLES") ||
                    line.contains("libGL.so") || line.contains("libGLU") ||
                    line.contains("libCoin") || line.contains("libFreeCAD") ||
                    line.contains("vulkan")) {
                    const QByteArray path = line.mid(line.lastIndexOf(' ') + 1).trimmed();
                    if (!path.isEmpty())
                        found.insert(QFileInfo(QString::fromLocal8Bit(path)).fileName());
                }
            }
        }
        QStringList mapped = found.values();
        mapped.sort();
        report += QStringLiteral("\\nMapped GPU/FreeCAD modules: ")
                + (mapped.isEmpty() ? QStringLiteral("(none accessible)") : mapped.join(QStringLiteral(", ")));
        return report;
    }
    static QString widget(const QWidget* w) {
        if (!w) return QStringLiteral("(none)");
        return QStringLiteral("%1/%2 geom=%3 shown=%4 focus=%5 topLevel=%6 "
                              "nativeHandle=%7")
            .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName(),
                 geometry(w->geometry()))
            .arg(w->isVisible()).arg(w->hasFocus()).arg(w->isWindow())
            .arg(w->windowHandle() != nullptr);
    }
    void arrange() {
        if (!host_ || !panel_ || !launcher_) return;
        const QRect r = host_->contentsRect();
        const int usableWidth = std::max(100, r.width() - 10);
        const int width = std::min(460, usableWidth);
        const int height = std::min(540, std::max(100, r.height() - 74));
        launcher_->setGeometry(r.left() + 5, r.bottom() - 47, 67, 42);
        panel_->setGeometry(r.left() + 5,
                            std::max(r.top() + 2, r.bottom() - height - 53),
                            width, height);
        if (panel_->isVisible()) panel_->raise();
        launcher_->raise();
    }
    void notify(const QString& value) {
        if (feedback_) feedback_->setText(value);
        DrnasDiagnostics::note(QStringLiteral("DBG ") + value);
    }
    static void setImmersiveAndroid(bool enabled) {
#ifdef Q_OS_ANDROID
        // Only the Android UI thread may change Activity system bars. No new
        // native window, EGL context, or Qt application is created here.
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([enabled] {
            if (!QNativeInterface::QAndroidApplication::isActivityContext()) return;
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            if (!activity.isValid()) return;
            QJniObject window = activity.callObjectMethod(
                "getWindow", "()Landroid/view/Window;");
            if (!window.isValid()) return;
            if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
                QJniObject controller = window.callObjectMethod(
                    "getInsetsController", "()Landroid/view/WindowInsetsController;");
                if (controller.isValid()) {
                    // WindowInsets.Type.statusBars() | navigationBars().
                    controller.callMethod<void>(enabled ? "hide" : "show",
                                                "(I)V", 3);
                }
            } else {
                QJniObject decor = window.callObjectMethod(
                    "getDecorView", "()Landroid/view/View;");
                if (decor.isValid()) {
                    // IMMERSIVE_STICKY | HIDE_NAVIGATION | FULLSCREEN |
                    // LAYOUT_STABLE | LAYOUT_HIDE_NAVIGATION | LAYOUT_FULLSCREEN
                    decor.callMethod<void>("setSystemUiVisibility", "(I)V",
                                           enabled ? 0x1706 : 0);
                }
            }
        });
#else
        Q_UNUSED(enabled);
#endif
    }
    void setFullScreen(bool enabled) {
        if (!host_) return;
        DrnasDiagnostics::note(QStringLiteral("DBG fullscreen requested=%1 before=%2")
            .arg(enabled).arg(geometry(host_->geometry())));
        if (enabled) host_->showFullScreen();
        else host_->showNormal();
        QSettings s;
        s.setValue(QStringLiteral("drnasDebug/fullscreen"), enabled);
        s.sync();
        QTimer::singleShot(200, this, [this, enabled] {
            if (!host_) return;
            notify(QStringLiteral("Fullscreen requested=%1 actual=%2 geometry=%3")
                .arg(enabled).arg(host_->isFullScreen()).arg(geometry(host_->geometry())));
            arrange();
            refresh();
        });
    }
    void applyWindowSize(int index) {
        if (!host_ || syncing_) return;
        const QList<QSize> sizes = {
            QSize(), QSize(640, 480), QSize(800, 600),
            QSize(960, 540), QSize(1280, 720)
        };
        if (index < 0 || index >= sizes.size()) return;
        if (host_->isFullScreen()) {
            notify(QStringLiteral("Window size works in windowed mode only; "
                                  "fullscreen keeps the device's native display resolution."));
            return;
        }
        QScreen* screen = host_->screen() ? host_->screen() : QGuiApplication::primaryScreen();
        const QRect bounds = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
        const QSize request = sizes[index].isEmpty() ? bounds.size() : sizes[index];
        const QSize clamped(std::min(request.width(), bounds.width()),
                            std::min(request.height(), bounds.height()));
        DrnasDiagnostics::note(QStringLiteral("DBG Qt logical window size requested=%1x%2 "
                                "clamped=%3x%4; PHYSICAL DISPLAY MODE UNCHANGED")
            .arg(request.width()).arg(request.height())
            .arg(clamped.width()).arg(clamped.height()));
        host_->resize(clamped);
        QTimer::singleShot(200, this, [this] {
            if (!host_) return;
            notify(QStringLiteral("Actual Qt window: %1; physical screen unchanged")
                .arg(geometry(host_->geometry())));
            refresh();
        });
    }
    void applyScale(int index) {
        if (syncing_) return;
        const double factors[] = { 0.70, 0.85, 1.0, 1.15 };
        if (index < 0 || index >= 4) return;
        QFont font = baseFont_;
        if (font.pointSizeF() > 0)
            font.setPointSizeF(std::max(6.0, baseFont_.pointSizeF() * factors[index]));
        else if (font.pixelSize() > 0)
            font.setPixelSize(std::max(8, int(baseFont_.pixelSize() * factors[index])));
        qApp->setFont(font);
        notify(QStringLiteral("Qt app font scale=%1% (does not alter display mode)")
            .arg(int(factors[index] * 100)));
        refresh();
    }
    QString snapshot() const {
        QString s = QStringLiteral("DRNAS DISPLAY / QT / OPENGL SNAPSHOT ")
                    + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        s += QStringLiteral("\nDevice: ") + QSysInfo::prettyProductName()
            + QStringLiteral(" / ") + QSysInfo::currentCpuArchitecture()
            + QStringLiteral(" / Qt ") + QString::fromLatin1(qVersion());
        s += QStringLiteral("\nQt backend=%1 QT_LOGGING_RULES=%2")
            .arg(QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM")),
                 QString::fromLocal8Bit(qgetenv("QT_LOGGING_RULES")));
        if (host_) {
            QScreen* screen = host_->screen() ? host_->screen() : QGuiApplication::primaryScreen();
            if (screen) {
                s += QStringLiteral("\nScreen=%1 geometry=%2 usable=%3 "
                                    "dpr=%4 dpi=%5 refresh=%6Hz physical=%7x%8mm")
                    .arg(screen->name(), geometry(screen->geometry()),
                         geometry(screen->availableGeometry()))
                    .arg(screen->devicePixelRatio()).arg(screen->logicalDotsPerInch())
                    .arg(screen->refreshRate()).arg(screen->physicalSize().width())
                    .arg(screen->physicalSize().height());
            }
            s += QStringLiteral("\nHost: ") + widget(host_);
            s += QStringLiteral("\nHost screen-state fullscreen=%1 maximized=%2 "
                                "minimized=%3 devicePixelRatio=%4")
                .arg(host_->isFullScreen()).arg(host_->isMaximized())
                .arg(host_->isMinimized()).arg(host_->devicePixelRatioF());
            s += QStringLiteral("\nCenter: ") + widget(host_->centralWidget());
            if (QWindow* handle = host_->windowHandle()) {
                s += QStringLiteral("\nHost QWindow: geom=%1 visible=%2 "
                                    "exposed=%3 surfaceType=%4 %5")
                    .arg(geometry(handle->geometry()))
                    .arg(handle->isVisible()).arg(handle->isExposed())
                    .arg(int(handle->surfaceType())).arg(format(handle->format()));
            }
            const auto glWidgets = host_->findChildren<QOpenGLWidget*>();
            s += QStringLiteral("\nQt QOpenGLWidget descendants=%1").arg(glWidgets.size());
            for (QOpenGLWidget* gl : glWidgets) {
                const QOpenGLContext* context = gl->context();
                s += QStringLiteral("\n GL widget: ") + widget(gl);
                s += QStringLiteral(" valid=%1 context=%2")
                    .arg(gl->isValid()).arg(context && context->isValid());
                if (context) s += QStringLiteral(" ") + format(context->format());
            }
        }
        QOpenGLContext* current = QOpenGLContext::currentContext();
        s += QStringLiteral("\nCurrent-thread GL context=%1")
            .arg(current && current->isValid());
        if (current) s += QStringLiteral(" ") + format(current->format());
        s += nativeProcessState();
        const auto windows = QGuiApplication::topLevelWindows();
        s += QStringLiteral("\nNative Qt top-level QWindows=%1").arg(windows.size());
        for (QWindow* w : windows) {
            s += QStringLiteral("\n QWindow: %1/%2 geom=%3 shown=%4 exposed=%5 "
                                "surface=%6")
                .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName(),
                     geometry(w->geometry()))
                .arg(w->isVisible()).arg(w->isExposed())
                .arg(int(w->surfaceType()));
        }
        const auto widgets = QApplication::topLevelWidgets();
        s += QStringLiteral("\nQt top-level widgets=%1").arg(widgets.size());
        for (QWidget* w : widgets) s += QStringLiteral("\n ") + widget(w);
        s += QStringLiteral("\nActive: ") + widget(QApplication::activeWindow());
        s += QStringLiteral("\nFocus: ") + widget(QApplication::focusWidget());
        QSettings settings;
        s += QStringLiteral("\nStartup marker=%1 pending=%2 result=%3")
            .arg(settings.value(QStringLiteral("nativeProbe/current")).toString())
            .arg(settings.value(QStringLiteral("nativeProbe/pending")).toBool())
            .arg(settings.value(QStringLiteral("nativeProbe/result")).toString());
        s += QStringLiteral("\nNOTE: A Qt snapshot cannot obtain GL_VENDOR outside "
                            "a current OpenGL context. EGL/GL4ES and driver errors "
                            "are preserved in Qt/Android logs.");
        return s;
    }
    void refresh() {
        if (!host_ || !info_ || !panel_->isVisible()) return;
        info_->setText(snapshot());
    }

protected:
    bool eventFilter(QObject* target, QEvent* event) override {
        if (!event || !host_) return false;
        if (target == host_ && (event->type() == QEvent::Resize
            || event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange))
            QTimer::singleShot(0, this, [this] { arrange(); });
        if (!verbose_->isChecked()) return false;
        const QEvent::Type t = event->type();
        if (t != QEvent::Show && t != QEvent::Hide && t != QEvent::Resize &&
            t != QEvent::Move && t != QEvent::WindowStateChange &&
            t != QEvent::WindowActivate && t != QEvent::WindowDeactivate &&
            t != QEvent::PlatformSurface) return false;
        auto* w = qobject_cast<QWidget*>(target);
        auto* nw = qobject_cast<QWindow*>(target);
        if (w && (w == host_ || w->isWindow() ||
                  qobject_cast<QOpenGLWidget*>(w))) {
            DrnasDiagnostics::note(QStringLiteral("DBG Qt event=%1 ") .arg(int(t)) + widget(w));
        } else if (nw) {
            DrnasDiagnostics::note(QStringLiteral("DBG QWindow event=%1 geom=%2 "
                                                  "visible=%3 exposed=%4")
                .arg(int(t)).arg(geometry(nw->geometry()))
                .arg(nw->isVisible()).arg(nw->isExposed()));
        }
        return false; // STRICTLY observation only: never steal FreeCAD input.
    }

public:
    static void recordSnapshot(const QString& reason) {
        if (!active_) return;
        DrnasDiagnostics::note(QStringLiteral("DBG checkpoint: ") + reason);
        DrnasDiagnostics::note(active_->snapshot());
    }

    explicit DrnasDebugCenter(QMainWindow* host) : QObject(host), host_(host),
                                                     baseFont_(qApp->font()) {
        active_ = this;
        launcher_ = new QPushButton(QStringLiteral("DBG"), host);
        launcher_->setObjectName(QStringLiteral("drnasDebugLauncher"));
        launcher_->setStyleSheet(QStringLiteral(
            "QPushButton{background:#245878;color:white;border:1px solid #8ab5ce;"
            "border-radius:6px;font-weight:bold;font-size:13px;}"));
        panel_ = new QScrollArea(host);
        panel_->setObjectName(QStringLiteral("drnasDebugPanel"));
        panel_->setWidgetResizable(true);
        panel_->setStyleSheet(QStringLiteral(
            "QScrollArea{background:#182a3b;border:1px solid #8ab5ce;}"
            "QLabel{color:#e3eef7;font-size:11px;}"
            "QPushButton,QComboBox{min-height:34px;}"
            "QCheckBox{color:#e3eef7;min-height:30px;}"));
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(8, 8, 8, 8);
        auto* title = new QLabel(QStringLiteral("DRNAS DEBUG — Android / Qt / OpenGL"));
        title->setWordWrap(true);
        layout->addWidget(title);
        fullscreen_ = new QCheckBox(QStringLiteral("Fullscreen (native device resolution)"));
        fullscreen_->setChecked(host_->isFullScreen());
        layout->addWidget(fullscreen_);
        immersive_ = new QCheckBox(QStringLiteral(
            "Hide Android status/navigation bars (immersive)"));
        immersive_->setChecked(false);
        layout->addWidget(immersive_);

        layout->addWidget(new QLabel(QStringLiteral(
            "Qt window size (windowed only; this does NOT change phone display pixels):")));
        windowSize_ = new QComboBox;
        windowSize_->addItems({QStringLiteral("Auto / available"),
            QStringLiteral("640 x 480"), QStringLiteral("800 x 600"),
            QStringLiteral("960 x 540"), QStringLiteral("1280 x 720")});
        layout->addWidget(windowSize_);

        layout->addWidget(new QLabel(QStringLiteral("Qt application font scale:")));
        uiScale_ = new QComboBox;
        uiScale_->addItems({QStringLiteral("70%"), QStringLiteral("85%"),
                            QStringLiteral("100%"), QStringLiteral("115%")});
        uiScale_->setCurrentIndex(2);
        layout->addWidget(uiScale_);
        verbose_ = new QCheckBox(QStringLiteral(
            "Detailed QWindow / widget geometry events (larger logs)"));
        layout->addWidget(verbose_);
        auto* row1 = new QHBoxLayout;
        auto* snapshotButton = new QPushButton(QStringLiteral("SNAPSHOT"));
        auto* copyButton = new QPushButton(QStringLiteral("COPY FULL LOG"));
        row1->addWidget(snapshotButton); row1->addWidget(copyButton);
        layout->addLayout(row1);
        auto* row2 = new QHBoxLayout;
        auto* resetButton = new QPushButton(QStringLiteral("RESET VIEW"));
        auto* closeButton = new QPushButton(QStringLiteral("CLOSE"));
        row2->addWidget(resetButton); row2->addWidget(closeButton);
        layout->addLayout(row2);
        feedback_ = new QLabel(QStringLiteral("Ready. Open a debug snapshot before starting FreeCAD."));
        feedback_->setWordWrap(true);
        layout->addWidget(feedback_);
        info_ = new QLabel;
        info_->setWordWrap(true);
        info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(info_);
        panel_->setWidget(body);
        panel_->hide();

        connect(launcher_, &QPushButton::clicked, this, [this] {
            panel_->setVisible(!panel_->isVisible());
            if (panel_->isVisible()) { refresh(); arrange(); }
            else launcher_->raise();
        });
        connect(closeButton, &QPushButton::clicked, panel_, &QWidget::hide);
        connect(fullscreen_, &QCheckBox::toggled, this,
                [this](bool checked) { if (!syncing_) setFullScreen(checked); });
        connect(immersive_, &QCheckBox::toggled, this, [this](bool checked) {
            QSettings s;
            s.setValue(QStringLiteral("drnasDebug/immersive"), checked);
            s.sync();
            setImmersiveAndroid(checked);
            notify(QStringLiteral("Android immersive system bars=%1").arg(checked));
        });
        connect(windowSize_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) { applyWindowSize(index); });
        connect(uiScale_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) { applyScale(index); });
        connect(verbose_, &QCheckBox::toggled, this, [this](bool checked) {
            notify(QStringLiteral("Detailed Qt window event logging=%1").arg(checked));
        });
        connect(snapshotButton, &QPushButton::clicked, this, [this] {
            const QString s = snapshot();
            DrnasDiagnostics::note(QStringLiteral("DBG MANUAL SNAPSHOT:\n") + s);
            info_->setText(s);
            notify(QStringLiteral("Snapshot saved in persistent log"));
        });
        connect(copyButton, &QPushButton::clicked, this, [this] {
            DrnasDiagnostics::note(QStringLiteral("DBG Copy full report"));
            QApplication::clipboard()->setText(snapshot() + QStringLiteral("\n\n")
                                             + DrnasDiagnostics::report());
            notify(QStringLiteral("Diagnostic snapshot + previous/current logs copied"));
        });
        connect(resetButton, &QPushButton::clicked, this, [this] {
            syncing_ = true;
            uiScale_->setCurrentIndex(2);
            windowSize_->setCurrentIndex(0);
            fullscreen_->setChecked(false);
            immersive_->setChecked(false);
            syncing_ = false;
            applyScale(2);
            setFullScreen(false);
            QTimer::singleShot(350, this, [this] { applyWindowSize(0); });
            notify(QStringLiteral("Restoring Qt windowed mode and default font"));
        });
        qApp->installEventFilter(this);
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            if (!host_) return;
            arrange();
            if (panel_->isVisible()) {
                const bool current = host_->isFullScreen();
                if (fullscreen_->isChecked() != current) {
                    syncing_ = true;
                    fullscreen_->setChecked(current);
                    syncing_ = false;
                }
            }
            const QString state = QStringLiteral("%1/%2/%3")
                .arg(geometry(host_->geometry()))
                .arg(host_->isFullScreen())
                .arg(QGuiApplication::topLevelWindows().size());
            if (state != lastState_) {
                lastState_ = state;
                DrnasDiagnostics::note(QStringLiteral("DBG window state changed: ") + state);
            }
        });
        timer->start(900);
        launcher_->show();
        arrange();
        QSettings s;
        if (s.value(QStringLiteral("drnasDebug/fullscreen"), false).toBool()) {
            QTimer::singleShot(0, this, [this] {
                syncing_ = true; fullscreen_->setChecked(true); syncing_ = false;
                setFullScreen(true);
            });
        }
        if (s.value(QStringLiteral("drnasDebug/immersive"), false).toBool()) {
            QTimer::singleShot(100, this, [this] { immersive_->setChecked(true); });
        }
        DrnasDiagnostics::note(QStringLiteral("DBG panel available at bottom-left"));
    }

    ~DrnasDebugCenter() override {
        if (active_ == this) active_ = nullptr;
    }
};
