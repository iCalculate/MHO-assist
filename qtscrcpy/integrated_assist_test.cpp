#include <QApplication>
#include <QTest>
#include <QFile>
#include <QImage>
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
        bool allowed=true;QList<QPair<int,bool>> events;QString lastStatus;
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
        assist.canSend=[&]{return allowed;};assist.sendKey=[&](int key,bool down){events.append(qMakePair(key,down));};assist.showStatus=[&](QString s){lastStatus=s;};
        QFile file(app.arguments().value(1));require(file.open(QIODevice::ReadOnly),"reference YUV fixture");QByteArray raw=file.readAll();
        require(raw.size()==1080*484*3/2,"reference YUV size");
        const uint8_t *y=reinterpret_cast<const uint8_t*>(raw.constData());const uint8_t *u=y+1080*484,*v=u+540*242;
        auto feed=[&](int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){assist.frame(1080,484,y,u,v,1080,540,540);QTest::qWait(5);}};
        assist.enable(true);QElapsedTimer startup;startup.start();while(events.size()<6 && startup.elapsed()<6500)feed(20);
        if(events.size()!=6)std::cerr<<"events="<<events.size()<<" status="<<lastStatus.toStdString()<<"\n";
        require(events.size()==6,"decoded crop must produce exactly three down/up pairs");
        require(events[0].first==Qt::Key_Down && events[2].first==Qt::Key_Left && events[4].first==Qt::Key_Up,"actual decoded crop sequence must match reference");
        for(int i=0;i<6;++i)require(events[i].second==(i%2==0),"every direction press must have a release");
        feed(500);require(events.size()==6,"same on-screen sequence must not replay");
        assist.resetRegion();
        QElapsedTimer wait;wait.start();while(events.size()==6 && wait.elapsed()<3500)feed(5);
        require(events.size()==7,"next recognition starts a key");assist.enable(false);
        require(events.size()==8 && !events.last().second,"disable during hold releases immediately");feed(300);require(events.size()==8,"disabled assistant cannot continue pending sequence");
        assist.enable(true);assist.resetRegion();wait.restart();while(events.size()==8 && wait.elapsed()<3500)feed(5);
        require(events.size()==9,"second start before focus cancellation");allowed=false;feed(120);require(events.size()==10 && !events.last().second,"focus loss releases and stops remaining keys");
        allowed=true;assist.edit(true);int before=events.size();feed(300);require(events.size()==before,"editing suppresses input");
        QRectF old=assist.overlay->region;QPoint center=assist.overlay->pixels().center().toPoint();QTest::mousePress(assist.overlay,Qt::LeftButton,Qt::NoModifier,center);
        QMouseEvent move(QEvent::MouseMove,center+QPoint(30,-10),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(assist.overlay,&move);QTest::mouseRelease(assist.overlay,Qt::LeftButton,Qt::NoModifier,center+QPoint(30,-10));
        require(assist.overlay->region!=old,"dragging changes normalized region");QRectF normalized=assist.overlay->region;video.resize(720,320);app.processEvents();require(assist.overlay->size()==video.size() && assist.overlay->region==normalized,"overlay scales with video without changing region");
        assist.stop();std::cout<<"PASS decoded YUV -> packaged recognizer -> paced input; no duplicate; cancel/release; region drag/scale\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
