#include <QApplication>
#include <QTest>
#include <QFile>
#include <QImage>
#include <QCursor>
#include <QDir>
#include <QFontDatabase>
#include <iostream>
#include <stdexcept>
#include "QtScrcpy/uibase/directionassistant.h"
#include "QtScrcpy/uibase/inputhistory.h"
static void require(bool b,const char *s){if(!b)throw std::runtime_error(s);}
int main(int argc,char **argv){
    QApplication app(argc,argv);QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    try{
        InputHistory history;
        require(readableKey(Qt::Key_Control)=="Ctrl" && readableKey(Qt::Key_Shift)=="Shift","modifier labels must be readable");
        for(int i=0;i<12;++i){history.record(QStringLiteral("mouse-left ↓"));history.record(QStringLiteral("mouse-left ↑"));}
        require(history.items.size()==1 && history.items[0].count==12 && !history.items[0].held,"repeated mouse clicks must be one complete item");
        history.record(QStringLiteral("W ↓"));history.record(QStringLiteral("D ↓"));history.record(QStringLiteral("W ↑"));
        require(!history.items[1].held && history.items[2].held,"overlapping press/release must pair by key");
        history.record(QStringLiteral("D ↓"));require(history.items[2].count==1,"duplicate down is not another click");
        history.record(QStringLiteral("重置"));require(!history.items.last().held,"reset clears all held displays");
        for(int i=0;i<100;++i){history.record(QString::number(i)+QStringLiteral(" ↓"));history.record(QString::number(i)+QStringLiteral(" ↑"));}
        require(history.items.size()==32,"history must be bounded");
        std::cout<<"PASS history pairs, repeat folding, modifier labels, bounded storage\n";
        DirectionRoundFlow retryFlow;
        retryFlow.clicked(DirectionMenu::Match, 0);
        require(!retryFlow.shouldTap(DirectionMenu::Match, 2399) && retryFlow.shouldTap(DirectionMenu::Match, 2400),
                "an unchanged page should allow one quick retry");
        retryFlow.clicked(DirectionMenu::Match, 2400);
        require(!retryFlow.shouldTap(DirectionMenu::Match, 17399) && retryFlow.shouldTap(DirectionMenu::Match, 17400),
                "subsequent retries should back off without stalling indefinitely");
        require(retryFlow.shouldTap(DirectionMenu::Confirm, 2500), "the next recognized stage should advance promptly");
        bool allowed=true;QList<QPair<QPointF,bool>> events;QString lastStatus;
        QWidget video;video.resize(1080,484);video.show();DirectionAssistant assist(&video,&video);
        video.setStyleSheet("background:#345678;");
        assist.overlay->show();app.processEvents();
        require(!assist.overlay->mask().contains(QPoint(540,200)),"overlay mask must not cover game interior");
        require(!assist.overlay->mask().contains(assist.overlay->pixels().center().toPoint()),"recognition box interior must be transparent");
        require(video.grab().toImage().pixelColor(540,200)==QColor("#345678"),"enabling overlay must preserve background pixels");
        assist.overlay->setEditing(true);app.processEvents();
        require(video.grab().toImage().pixelColor(540,200)==QColor("#345678"),"editing must preserve background pixels");
        assist.overlay->setEditing(false);
        std::cout<<"PASS region overlay and editing preserve game pixels\n";
        const auto directionPosition = [](int key) { return QPointF(0.6 + (key - Qt::Key_Left) * 0.065, 0.7); };
        assist.canSend = [&] { return allowed; };
        assist.directionPosition = directionPosition;
        assist.sendTouch = [&](const QPointF &position, const QSize &, bool down) {
            events.append(qMakePair(position, down));
            return true;
        };
        assist.showStatus = [&](QString status) { lastStatus = status; };
        QFile file(app.arguments().value(1));require(file.open(QIODevice::ReadOnly),"reference YUV fixture");QByteArray raw=file.readAll();
        require(raw.size()==1080*484*3/2,"reference YUV size");
        const uint8_t *y=reinterpret_cast<const uint8_t*>(raw.constData());const uint8_t *u=y+1080*484,*v=u+540*242;
        auto feed=[&](int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){assist.frame(1080,484,y,u,v,1080,540,540);QTest::qWait(5);}};
        video.showMinimized();app.processEvents();
        const QPoint originalCursor = QCursor::pos();
        assist.enable(true);QElapsedTimer startup;startup.start();while(events.size()<6 && startup.elapsed()<6500)feed(20);
        if(events.size()!=6)std::cerr<<"events="<<events.size()<<" status="<<lastStatus.toStdString()<<"\n";
        require(events.size()==6,"decoded crop must produce exactly three down/up pairs");
        const auto expected = app.arguments().value(2) == "left-left-down"
            ? Qt::Key_Left : Qt::Key_Down;
        const auto last = expected == Qt::Key_Left ? Qt::Key_Down : Qt::Key_Up;
        require(events[0].first == directionPosition(expected) && events[2].first == directionPosition(Qt::Key_Left)
                && events[4].first == directionPosition(last), "decoded arrows must use their mapped touch positions");
        require(QCursor::pos() == originalCursor, "direction touches must leave the desktop cursor alone");
        for(int i=0;i<6;++i)require(events[i].second==(i%2==0),"every direction press must have a release");
        feed(500);require(events.size()==6,"same on-screen sequence must not replay");
        assist.resetRegion();
        QElapsedTimer wait;wait.start();while(events.size()==6 && wait.elapsed()<3500)feed(5);
        require(events.size()==7,"next recognition starts a key");assist.enable(false);
        require(events.size()==8 && !events.last().second,"disable during hold releases immediately");feed(300);require(events.size()==8,"disabled assistant cannot continue pending sequence");
        assist.enable(true);assist.resetRegion();wait.restart();while(events.size()==8 && wait.elapsed()<3500)feed(5);
        require(events.size()==9,"second start before losing device eligibility");allowed=false;feed(120);require(events.size()==10 && !events.last().second,"losing the device releases a held touch and stops remaining steps");
        allowed=true;video.showNormal();app.processEvents();assist.edit(true);int before=events.size();feed(300);require(events.size()==before,"editing suppresses input");
        QRectF old=assist.overlay->region;QPoint center=assist.overlay->pixels().center().toPoint();QTest::mousePress(assist.overlay,Qt::LeftButton,Qt::NoModifier,center);
        QMouseEvent move(QEvent::MouseMove,center+QPoint(30,-10),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(assist.overlay,&move);QTest::mouseRelease(assist.overlay,Qt::LeftButton,Qt::NoModifier,center+QPoint(30,-10));
        require(assist.overlay->region!=old,"dragging changes normalized region");QRectF normalized=assist.overlay->region;video.resize(720,320);app.processEvents();require(assist.overlay->size()==video.size() && assist.overlay->region==normalized,"overlay scales with video without changing region");
        assist.stop();std::cout<<"PASS decoded YUV -> packaged recognizer -> paced input; no duplicate; cancel/release; region drag/scale\n";
        if (app.arguments().size() > 3) {
            const QDir frames(app.arguments().at(3));
            const auto inspect = [&](const QString &name, DirectionMenu expectedMenu, QPoint expectedPoint) {
                const QImage luma = QImage(frames.filePath(name + ".png")).convertToFormat(QImage::Format_Grayscale8);
                require(!luma.isNull(), "menu replay sample must load");
                const auto result = DirectionMenuRecognizer::recognize(luma.width(), luma.height(), luma.constBits(), luma.bytesPerLine());
                if (result.menu != expectedMenu) std::cerr << name.toStdString() << " got menu " << int(result.menu) << "\n";
                require(result.menu == expectedMenu, "replay sample must classify its actual game page");
                if (expectedMenu != DirectionMenu::None) {
                    const QPoint pixel(qRound(result.position.x() * 960), qRound(result.position.y() * 431));
                    require((pixel - expectedPoint).manhattanLength() < 12, "touch must follow the visible button position");
                }
                return luma;
            };
            const auto match = inspect("match", DirectionMenu::Match, {195, 194});
            const auto confirm = inspect("confirm", DirectionMenu::Confirm, {571, 289});
            const auto enter = inspect("enter", DirectionMenu::Enter, {479, 289});
            const auto exit = inspect("exit", DirectionMenu::Exit, {795, 396});
            inspect("shift-match", DirectionMenu::Match, {232, 213});
            inspect("shift-confirm", DirectionMenu::Confirm, {608, 308});
            inspect("shift-enter", DirectionMenu::Enter, {516, 308});
            inspect("shift-exit", DirectionMenu::Exit, {832, 415});
            inspect("scale-match", DirectionMenu::Match, {195, 194});
            inspect("scale-confirm", DirectionMenu::Confirm, {571, 289});
            inspect("scale-enter", DirectionMenu::Enter, {479, 289});
            inspect("scale-exit", DirectionMenu::Exit, {795, 396});
            for (const auto &file : frames.entryList({"negative-*.png"}, QDir::Files))
                inspect(file.chopped(4), DirectionMenu::None, {});
            const auto checkUnrelatedDialog = [](const QImage &screen, const QRect &prompt) {
                QImage dialog = screen.scaled(960, 431).convertToFormat(QImage::Format_RGB32);
                QPainter painter(&dialog);
                painter.fillRect(prompt, QColor(55, 52, 43));
                painter.end();
                dialog = dialog.convertToFormat(QImage::Format_Grayscale8);
                const auto match = DirectionMenuRecognizer::recognize(
                    dialog.width(), dialog.height(), dialog.constBits(), dialog.bytesPerLine());
                require(match.menu == DirectionMenu::None, "the dance prompt must be present before clicking a generic dialog");
            };
            checkUnrelatedDialog(confirm, QRect(325, 204, 204, 25));
            checkUnrelatedDialog(enter, QRect(450, 198, 166, 25));
            const QByteArray chroma(480 * 216, char(128));
            const auto *neutral = reinterpret_cast<const uint8_t *>(chroma.constData());
            const auto replay = [&](const QImage &frame, int duration = 440) {
                const QImage stream = frame.scaled(960, 432);
                QElapsedTimer timer;
                timer.start();
                while (timer.elapsed() < duration) {
                    assist.frame(960, 432, stream.constBits(), neutral, neutral, stream.bytesPerLine(), 480, 480);
                    QTest::qWait(12);
                }
            };
            bool completed = false;
            assist.singleRoundFinished = [&] { completed = true; };
            assist.edit(false);
            assist.enable(true);
            events.clear();
            replay(exit);
            require(completed && !assist.enabled && events.isEmpty(), "single-round replay must stop at results without dismissing it");

            assist.setContinuous(true);
            assist.enable(true);
            for (int round = 0; round < 2; ++round) {
                for (const auto &screen : {exit, match, confirm, enter}) replay(screen);
                require(events.size() == (round + 1) * 8, "each round transition must issue four paired taps");
            }
            replay(enter);
            require(events.size() == 16, "the same page must not produce immediate repeat taps");
            assist.stop();
            std::cout << "PASS two rounds of menu replay, offset/scale checks, duplicate suppression, single-round stop\n";
        }
        return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
