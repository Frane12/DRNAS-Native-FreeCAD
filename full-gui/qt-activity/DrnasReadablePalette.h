// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QStyle>
#include <QTimer>
#include "StartupDiagnostics.h"

// Pair every text role with its background, including inactive and disabled
// widgets. Android's dark system palette must not mix with light Qt controls.
class DrnasReadablePalette final : public QObject {
    QTimer settle;
    bool applying=false;
    static constexpr QPalette::ColorRole roles[]={
        QPalette::Window,QPalette::WindowText,QPalette::Base,QPalette::AlternateBase,
        QPalette::Text,QPalette::Button,QPalette::ButtonText,QPalette::Highlight,
        QPalette::HighlightedText,QPalette::ToolTipBase,QPalette::ToolTipText,
        QPalette::PlaceholderText,QPalette::Link,QPalette::LinkVisited};
    bool matches() const {
        const QPalette current=QApplication::palette(), wanted=light();
        for (auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled})
            for (auto role:roles) if (current.color(group,role)!=wanted.color(group,role)) return false;
        return true;
    }
    void apply() {
        if (applying || matches()) return;
        applying=true;
        QApplication::setPalette(light());
        applying=false;
        DrnasDiagnostics::note(QStringLiteral("UI CONTRAST: consistent light palette; dark active/inactive/disabled text"));
    }
protected:
    bool eventFilter(QObject*,QEvent* event) override {
        // FreeCAD's style setup and Android configuration changes can replace
        // the application palette after main(). Restore it outside dispatch.
        // Individual widget stylesheets (e.g. dark diagnostics) remain intact.
        if (!applying && event->type()==QEvent::ApplicationPaletteChange && !matches())
            settle.start();
        return false;
    }
public:
    static QPalette light() {
        QPalette palette(QColor(QStringLiteral("#edf1f5")));
        auto color=[&](QPalette::ColorRole role,const char* value) {
            palette.setColor(QPalette::All,role,QColor(QString::fromLatin1(value)));
        };
        color(QPalette::Window,"#edf1f5");color(QPalette::WindowText,"#15212e");
        color(QPalette::Base,"#ffffff");color(QPalette::AlternateBase,"#edf3f8");
        color(QPalette::Text,"#15212e");color(QPalette::Button,"#e2e8ee");
        color(QPalette::ButtonText,"#15212e");color(QPalette::BrightText,"#ffffff");
        color(QPalette::Light,"#ffffff");color(QPalette::Midlight,"#f3f6f9");
        color(QPalette::Mid,"#9aa8b8");color(QPalette::Dark,"#63758a");
        color(QPalette::Shadow,"#34475b");color(QPalette::Highlight,"#155f9d");
        color(QPalette::HighlightedText,"#ffffff");color(QPalette::ToolTipBase,"#fff8da");
        color(QPalette::ToolTipText,"#15212e");color(QPalette::PlaceholderText,"#526273");
        color(QPalette::Link,"#075894");color(QPalette::LinkVisited,"#653b85");
        for (auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText,QPalette::PlaceholderText})
            palette.setColor(QPalette::Disabled,role,QColor(QStringLiteral("#596777")));
        palette.setColor(QPalette::Disabled,QPalette::Base,QColor(QStringLiteral("#f1f3f5")));
        palette.setColor(QPalette::Disabled,QPalette::Button,QColor(QStringLiteral("#e5e9ed")));
        return palette;
    }
    explicit DrnasReadablePalette(QObject* owner) : QObject(owner) {
        // Use one palette-aware widget style on Android as well as desktop.
        if (QApplication::style()->objectName().compare(QStringLiteral("fusion"),Qt::CaseInsensitive)!=0)
            QApplication::setStyle(QStringLiteral("Fusion"));
        settle.setSingleShot(true);settle.setInterval(0);
        connect(&settle,&QTimer::timeout,this,[this]{apply();});
        apply();qApp->installEventFilter(this);
    }
};
