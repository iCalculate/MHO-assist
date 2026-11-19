#include <QApplication>
#include <QImage>
#include <QFontDatabase>
#include "QtScrcpy/uibase/diagnosticpanel.h"
#include "QtScrcpy/uibase/inputhistory.h"
#include <QElapsedTimer>
#include <QScrollBar>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include "QtScrcpy/uibase/keepratiowidget.h"
#include "QtScrcpy/util/diagnosticlog.h"
static void require(bool b,const char *m) { if(!b) throw std::runtime_error(m); }

static QImage renderPanel(DiagnosticPanel &panel) {
    QImage canvas(panel.size(), QImage::Format_ARGB32_Premultiplied);
    panel.render(&canvas);
    return canvas;
}

static QRect activeKeyRect(DiagnosticPanel &panel, QVariantMap &snapshot, const QString &key) {
    snapshot.insert("heldKeys", QStringList{key});
    panel.setSnapshot(snapshot);
    const auto canvas = renderPanel(panel);
    const auto activeFill = QColor("#20465b").rgb();
    QRect bounds;
    for (int y = 0; y < canvas.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < canvas.width(); ++x) {
            if (row[x] == activeFill) bounds = bounds.united(QRect(x, y, 1, 1));
        }
    }
    require(!bounds.isEmpty(), "a held movement key must be highlighted");
    return bounds;
}

static void checkKeyboardLayout(DiagnosticPanel &panel, QVariantMap snapshot) {
    snapshot.remove("historyEntries");
    const auto w = activeKeyRect(panel, snapshot, QStringLiteral("W"));
    const auto a = activeKeyRect(panel, snapshot, QStringLiteral("A"));
    const auto s = activeKeyRect(panel, snapshot, QStringLiteral("S"));
    const auto d = activeKeyRect(panel, snapshot, QStringLiteral("D"));
    require(w.center().x() == s.center().x() && w.bottom() < s.top(), "W must sit directly above S");
    require(a.top() == s.top() && s.top() == d.top() && a.right() < s.left() && s.right() < d.left(),
            "A, S and D must appear left-to-right on one row");
}

static void checkHistoryStrip(DiagnosticPanel &panel, QVariantMap snapshot, InputHistory history) {
    auto *scrollBar = panel.findChild<QScrollBar *>(QStringLiteral("inputHistoryScrollBar"));
    require(scrollBar && scrollBar->orientation() == Qt::Horizontal, "input history must scroll horizontally");
    panel.setSnapshot(snapshot);
    require(!scrollBar->isVisible(), "the scrollbar must hide when input history fits");

    for (const auto &key : QStringList{"CapsLock", "Shift", "mouse-mid", "Escape", "Backspace", "Enter"}) {
        history.record(key + QStringLiteral(" ↓"));
        history.record(key + QStringLiteral(" ↑"));
    }
    snapshot.insert("historyEntries", history.entries());
    DiagnosticPanel waitingForLayout;
    waitingForLayout.setSnapshot(snapshot);
    waitingForLayout.resize(panel.size());
    waitingForLayout.show();
    QApplication::processEvents();
    auto *initialScroll = waitingForLayout.findChild<QScrollBar *>(QStringLiteral("inputHistoryScrollBar"));
    require(initialScroll && initialScroll->maximum() > 0 && initialScroll->value() == initialScroll->maximum(),
            "history must show the latest keys if populated before the first resize");
    panel.setSnapshot(snapshot);
    require(scrollBar->isVisible() && scrollBar->maximum() > 0, "overflowing history must expose a scrollbar");
    require(scrollBar->value() == scrollBar->maximum(), "input history must initially show the latest keys");
    const QRect scrollRect = scrollBar->geometry();
    require(scrollRect.bottom() < panel.assistButton->geometry().top(), "input history must fit above the controls");
    const QRect chips(scrollRect.left(), scrollRect.top() - 25, scrollRect.width(), 25);
    const auto latest = renderPanel(panel).copy(chips);
    scrollBar->setValue(0);
    require(renderPanel(panel).copy(chips) != latest, "scrolling back must reveal earlier keys");

    history.record(QStringLiteral("Tab ↓"));
    history.record(QStringLiteral("Tab ↑"));
    snapshot.insert("historyEntries", history.entries());
    panel.setSnapshot(snapshot);
    require(scrollBar->value() == 0, "new input must preserve the position when browsing older keys");

    scrollBar->setValue(scrollBar->maximum());
    const int previousMaximum = scrollBar->maximum();
    history.record(QStringLiteral("Alt ↓"));
    snapshot.insert("historyEntries", history.entries());
    panel.setSnapshot(snapshot);
    require(scrollBar->maximum() > previousMaximum && scrollBar->value() == scrollBar->maximum(),
            "new input must remain visible when following the latest keys");

    for (int i = history.items.size(); i < 32; ++i) {
        const QString key = QStringLiteral("F%1").arg(i);
        history.record(key + QStringLiteral(" ↓"));
        history.record(key + QStringLiteral(" ↑"));
    }
    snapshot.insert("historyEntries", history.entries());
    panel.setSnapshot(snapshot);
    scrollBar->setValue(scrollBar->maximum() / 2);
    const int browsingPosition = scrollBar->value();
    const auto beforeEviction = renderPanel(panel).copy(chips);
    history.record(QStringLiteral("Space ↓"));
    snapshot.insert("historyEntries", history.entries());
    panel.setSnapshot(snapshot);
    require(scrollBar->value() < browsingPosition && renderPanel(panel).copy(chips) == beforeEviction,
            "evicting an older key must retain the keys currently being browsed");
}

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
        for (const auto &size : {QSize(640, 400), QSize(1172, 440)}) {
            view.resize(size);
            app.processEvents();
            checkKeyboardLayout(*panel, sample);
            checkHistoryStrip(*panel, sample, history);
        }
        panel->setSnapshot(sample);
        panel->setAssistStatus(QStringLiteral("识别中 · 等待稳定序列 · 2.4 ms"));
        panel->assistButton->setChecked(true);

        for(int i=0;i<240;++i) {
            sample.insert("skipped",i==134?12:i%7==0?2:0);sample.insert("queueMs",i==135?8.4:0.3);sample.insert("uiGapMs",i%9==0?12:2);
            panel->setSnapshot(sample);panel->addFps(i==134?32:i%5==0?57:60);panel->sample((i+1)*250);
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
        std::cout<<"PASS: viewport layout, WASD, scrollable history; 10000 log enqueues="<<ms<<"ms\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1; }
}
