// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../DrnasReadablePalette.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) {qWarning().noquote()<<s;} }
static void require(bool good,const char* why) {if (!good) qFatal("%s",why);}
static double luminance(QColor c) {
    auto linear=[](double x){return x<=0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4);};
    return 0.2126*linear(c.redF())+0.7152*linear(c.greenF())+0.0722*linear(c.blueF());
}
static double contrast(QColor a,QColor b) {
    const double x=luminance(a), y=luminance(b);
    return (std::max(x,y)+0.05)/(std::min(x,y)+0.05);
}
static void readable(const QPalette& p,QPalette::ColorRole text,QPalette::ColorRole background) {
    for (auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled})
        require(contrast(p.color(group,text),p.color(group,background))>=4.5,
                "text/background contrast must remain readable in active, inactive and disabled states");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    // Reproduce the supplied screenshot: dark-system text on light controls.
    QPalette mixed; mixed.setColor(QPalette::All,QPalette::Window,QColor("#303030"));
    for (auto role:{QPalette::Text,QPalette::WindowText,QPalette::ButtonText,
                    QPalette::Base,QPalette::Button}) mixed.setColor(QPalette::All,role,Qt::white);
    QApplication::setPalette(mixed);
    require(contrast(QApplication::palette().text().color(),QApplication::palette().base().color())<1.1,
            "fixture must reproduce white-on-white text before the repair");
    new DrnasReadablePalette(&app);
    QWidget surface;surface.resize(860,600);
    auto* layout=new QVBoxLayout(&surface);
    auto* title=new QLabel("FreeCAD — Model / Svojstva / Datoteke",&surface);layout->addWidget(title);
    auto* tree=new QTreeWidget(&surface);tree->setHeaderLabels({"Model","Vrijednost"});
    new QTreeWidgetItem(tree,{"Neimenovano","Kocka"});
    new QTreeWidgetItem(tree,{"Pad","Dostupna naredba"});layout->addWidget(tree);
    auto* table=new QTableWidget(2,2,&surface);table->setHorizontalHeaderLabels({"Svojstvo","Vrijednost"});
    table->setItem(0,0,new QTableWidgetItem("Label"));table->setItem(0,1,new QTableWidgetItem("Neimenovano"));
    table->setItem(1,0,new QTableWidgetItem("Duljina"));table->setItem(1,1,new QTableWidgetItem("10 mm"));
    layout->addWidget(table);
    auto* name=new QLineEdit("kocka.FCStd",&surface);layout->addWidget(name);
    auto* choice=new QComboBox(&surface);choice->addItems({"Alati","Part Design","Sketcher"});layout->addWidget(choice);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&surface);
    buttons->button(QDialogButtonBox::Open)->setEnabled(false);layout->addWidget(buttons);
    auto* dark=new QWidget(&surface);
    dark->setStyleSheet("QWidget{background:#182d42;color:#e9f2fa;}");
    auto* darkLayout=new QVBoxLayout(dark);auto* diagnostic=new QLabel("Log — izvorni tamni panel",dark);
    darkLayout->addWidget(diagnostic);layout->addWidget(dark);
    surface.show();QTest::qWait(30);
    for (auto* widget:{static_cast<QWidget*>(tree),static_cast<QWidget*>(table),
                       static_cast<QWidget*>(name),static_cast<QWidget*>(choice)})
        readable(widget->palette(),QPalette::Text,QPalette::Base);
    readable(tree->header()->palette(),QPalette::ButtonText,QPalette::Button);
    readable(buttons->button(QDialogButtonBox::Open)->palette(),QPalette::ButtonText,QPalette::Button);
    readable(buttons->button(QDialogButtonBox::Cancel)->palette(),QPalette::ButtonText,QPalette::Button);
    readable(title->palette(),QPalette::WindowText,QPalette::Window);
    readable(tree->palette(),QPalette::HighlightedText,QPalette::Highlight);
    require(luminance(diagnostic->palette().window().color())<0.1 &&
            contrast(diagnostic->palette().windowText().color(),diagnostic->palette().window().color())>7,
            "an explicitly paired dark diagnostic panel must retain its readable local stylesheet");
    // A later native or FreeCAD style reset must not bring the mixed palette back.
    QApplication::setPalette(mixed);QTest::qWait(30);
    readable(tree->palette(),QPalette::Text,QPalette::Base);
    readable(table->palette(),QPalette::Text,QPalette::Base);
    QTemporaryDir directory;require(directory.isValid(),"temporary file-dialog directory required");
    require(QDir(directory.path()).mkdir("diagnostics"),"visible folder fixture required");
    QFile model(directory.filePath("kocka.FCStd"));
    require(model.open(QIODevice::WriteOnly),"visible filename fixture required");model.close();
    QFileDialog files(&surface,"Otvori datoteku",directory.path());
    files.setOption(QFileDialog::DontUseNativeDialog);files.setAcceptMode(QFileDialog::AcceptOpen);
    files.selectFile("kocka.FCStd");
    files.show();QTest::qWait(60);
    for (auto* view:files.findChildren<QAbstractItemView*>())
        readable(view->palette(),QPalette::Text,QPalette::Base);
    for (auto* edit:files.findChildren<QLineEdit*>())
        readable(edit->palette(),QPalette::Text,QPalette::Base);
    for (auto* button:files.findChildren<QPushButton*>())
        readable(button->palette(),QPalette::ButtonText,QPalette::Button);
    const QString preview=qEnvironmentVariable("DRNAS_CONTRAST_PREVIEW");
    if (!preview.isEmpty()) {
        require(files.grab().save(preview+"-files.png"),"file-dialog preview must save");
        files.hide();require(surface.grab().save(preview+"-controls.png"),"control preview must save");
    }
    std::cout<<"Mixed Android palette, property views, late file dialog and all text states passed\n";
}
