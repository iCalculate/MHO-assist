#include <QApplication>
#include <QImage>
#include <QFontDatabase>
#include "QtScrcpy/uibase/diagnosticpanel.h"
#include "QtScrcpy/uibase/inputhistory.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include "QtScrcpy/uibase/keepratiowidget.h"
#include "QtScrcpy/util/diagnosticlog.h"
static void require(bool b,const char *m) { if(!b) throw std::runtime_error(m); }
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    try {
        KeepRatioWidget view;
        QWidget *phone=new QWidget;
        DiagnosticPanel *panel=new DiagnosticPanel;
        view.setWidget(phone);view.setDiagnosticsWidget(panel);
        view.setWidthHeightRatio(640.0f/286);view.showDiagnostics(true);view.show();
        for(QSize size:{QSize(1200,400),QSize(640,400),QSize(400,700),QSize(1600,500)}) {
            view.resize(size);app.processEvents();
            require(view.rect().contains(phone->geometry()),"phone must fit the viewport");
            require(view.rect().contains(panel->geometry()),"diagnostic panel must fit the viewport");
            require(!phone->geometry().intersects(panel->geometry()),"diagnostic panel must never overlay phone content");
            require(qAbs(phone->width()-phone->height()*(640.0/286))<3,"phone aspect ratio must be preserved");
        }
        require(panel->y()==0 && panel->width()==view.width(),"diagnostics must span the top");
        QVariantMap sample{{"mode",QStringLiteral("映射 ON")},{"held",QStringLiteral("W + D + 左键")},
            {"heldKeys",QStringList{"W","D"}},{"touches",QStringLiteral("#0 摇杆, #1 mouse-view")},
            {"queueMs",0.3},{"uiGapMs",6},{"repairs",0},{"skipped",0},
            {"history",QStringList{QStringLiteral("W ↓"),QStringLiteral("D ↓"),QStringLiteral("D ↑"),QStringLiteral("左键 ↓"),QStringLiteral("左键 ↑"),QStringLiteral("D ↓")}}};
        InputHistory history;
        for (int i=0;i<8;++i) {history.record(QStringLiteral("mouse-left ↓"));history.record(QStringLiteral("mouse-left ↑"));}
        history.record(QStringLiteral("Ctrl ↓"));history.record(QStringLiteral("Ctrl ↑"));
        history.record(QStringLiteral("mouse-right ↓"));history.record(QStringLiteral("mouse-right ↑"));
        history.record(QStringLiteral("D ↓"));
        sample.insert("historyEntries",history.entries());
        panel->setSnapshot(sample);
        panel->setAssistStatus(QStringLiteral("识别中 · 等待稳定序列 · 2.4 ms"));
        panel->assistButton->setChecked(true);

        for(int i=0;i<60;++i) {
            sample.insert("skipped",i==34?12:i%7==0?2:0);sample.insert("queueMs",i==35?8.4:0.3);sample.insert("uiGapMs",i%9==0?12:2);
            panel->setSnapshot(sample);panel->addFps(i==34?32:i%5==0?57:60);panel->sample();
        }
        for(QSize size:{QSize(1172,440),QSize(640,400),QSize(400,700)}) {
            view.resize(size);app.processEvents();
            require(panel->y()==0 && panel->width()==view.width(),"all sizes must use a top header");
            require(phone->y()>=panel->height(),"video stays below the header");
        }
        view.resize(1172,440);app.processEvents();
        QImage canvas(panel->size(),QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer paintTimer;paintTimer.start();
        for(int i=0;i<1000;++i) panel->render(&canvas);
        std::cout<<"1000 panel paints="<<paintTimer.elapsed()<<"ms\n";
        require(paintTimer.elapsed()<5000,"header painting must stay lightweight");
        view.showDiagnostics(false);app.processEvents();
        require(!panel->isVisible(),"hide FPS must hide diagnostics");
        QTemporaryDir folder;
        DiagnosticLog log;log.start(folder.path());
        QElapsedTimer timer;timer.start();
        for(int i=0;i<10000;++i) log.append(QString("input sample %1").arg(i));
        const auto ms=timer.elapsed();log.stop();
        require(ms<500,"diagnostic enqueue must not block input on disk writes");
        std::cout<<"PASS: four viewport sizes, no overlap, preserved aspect, top header; 10000 log enqueues="<<ms<<"ms\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1; }
}
