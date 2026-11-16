#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QVariantMap>
#include <QFontMetrics>
#include <QToolButton>
#include <QElapsedTimer>
#include <array>

class DiagnosticPanel final : public QWidget {
public:
    QToolButton *assistButton, *regionButton, *resetButton, *pointerButton, *keymapButton;
    explicit DiagnosticPanel(QWidget *parent=nullptr) : QWidget(parent) {
        QFont f(QStringLiteral("Microsoft YaHei UI")); f.setPixelSize(12); setFont(f);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAccessibleName(QStringLiteral("顶部输入诊断"));
        setToolTip(QStringLiteral("本机排队：控制消息等待发送的时间。UI 延迟：100 ms 界面定时检查超出的时间。\n均为电脑端指标，不是手机响应时间。曲线保留最近 60 秒；时间曲线共用 ms 刻度，FPS 与丢帧独立缩放。\n丢帧：电脑视频缓冲区跳过的帧数/秒。最近按键合并按下和松开，连续重复以 ×次数显示；亮色表示仍按住。"));
        assistButton=new QToolButton(this);assistButton->setText(QStringLiteral("方向助手"));assistButton->setCheckable(true);
        assistButton->setToolTip(QStringLiteral("开启后识别框内方向序列并输入；失焦或普通鼠标模式暂停。F8 开关，F9 停止。"));
        keymapButton=new QToolButton(this);keymapButton->setText(QStringLiteral("按键配置"));
        keymapButton->setToolTip(QStringLiteral("打开现有游戏按键位置编辑页面。返回投屏后按 Ctrl 恢复映射。"));
        regionButton=new QToolButton(this);regionButton->setText(QStringLiteral("调整范围"));regionButton->setCheckable(true);
        regionButton->setToolTip(QStringLiteral("拖框移动；拖右下角缩放；框外拖动重新框选。调整时暂停输入。"));
        resetButton=new QToolButton(this);resetButton->setText(QStringLiteral("自动定位"));resetButton->setToolTip(QStringLiteral("恢复记忆范围，再根据四方向按钮连续确认并自动定位。手动调整后保持手动范围。"));
        pointerButton=new QToolButton(this);pointerButton->setText(QStringLiteral("手机光标"));pointerButton->setCheckable(true);pointerButton->setChecked(true);
        for(auto button:{assistButton,keymapButton,regionButton,resetButton,pointerButton}) {
            button->setFocusPolicy(Qt::NoFocus);button->setFont(f);
            button->setStyleSheet("QToolButton{color:#a9bbc9;background:#1b2938;border:1px solid #34485b;border-radius:4px;padding:2px 6px;} QToolButton:checked{color:#7de0b2;border-color:#7de0b2;background:#204539;}");
        }
        clock.start();
    }
    void setAssistStatus(const QString &value) {if(status!=value){status=value;update();}}
    void setSnapshot(const QVariantMap &data) {
        m_data=data;queuePeak=qMax(queuePeak,data.value("queueMs").toDouble());uiPeak=qMax(uiPeak,data.value("uiGapMs").toDouble());
        if(clock.elapsed()>=1000){sample();clock.restart();}
        setAccessibleDescription(QStringLiteral("FPS %1; %2; 最近 %3").arg(m_fps).arg(status,data.value("history").toStringList().join("  ")));update();
    }
    void addFps(int fps){m_fps=fps;update();}
    void sample(){samples[next]={{double(m_fps),m_data.value("skipped").toDouble(),queuePeak,uiPeak}};next=(next+1)%60;count=qMin(60,count+1);queuePeak=uiPeak=0;}
protected:
    void resizeEvent(QResizeEvent *) override {
        const int buttonWidth=qMin(82,qMax(1,(width()-56)/5));int x=12;
        for(auto button:{assistButton,keymapButton,regionButton,resetButton,pointerButton}) {
            button->setGeometry(x,108,buttonWidth,27);x+=buttonWidth+8;
        }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setFont(font());
        const QColor bg("#121b26"),line("#283747"),text("#d6e1eb"),muted("#8195a8"),cyan("#79d8f4"),green("#7de0b2"),amber("#f0c471"),red("#ff8892");
        p.fillRect(rect(),bg);p.setPen(line);p.drawLine(0,height()-1,width(),height()-1);const QFontMetrics fm(font());
        auto label=[&](int x,int y,const QString &s,QColor color,int w){p.setPen(color);p.drawText(QRect(x,y,qMax(0,w),20),Qt::AlignVCenter|Qt::AlignLeft,fm.elidedText(s,Qt::ElideRight,qMax(0,w)));};
        const bool compact=width()<900;const int rightWidth=compact?width():qRound(width()*.42);
        const int graphW=(rightWidth-42)/2;const int right=compact?width()-12:width()-rightWidth-16;int x=12;
        label(12,8,m_data.value("mode").toString(),muted,130);x=146;const auto held=m_data.value("heldKeys").toStringList();
        for(const auto &key:QStringList{"W","A","S","D"}){p.setPen(held.contains(key)?cyan:line);p.setBrush(held.contains(key)?QColor("#20465b"):QColor("#192635"));p.drawRoundedRect(QRectF(x,8,24,24),3,3);label(x+6,10,key,held.contains(key)?cyan:muted,18);x+=30;}
        label(x+10,10,QStringLiteral("触点 %1").arg(m_data.value("touches",QStringLiteral("无")).toString()),muted,right-x-7);
        label(12,43,QStringLiteral("最近"),muted,32);x=52;int hy=40;
        const auto history=m_data.value("historyEntries").toList();
        auto itemWidth=[&](QVariantMap item){const QString key=item.value("key").toString();const int n=item.value("count").toInt();return (key.startsWith("mouse-")?18:fm.horizontalAdvance(key))+(n>1?fm.horizontalAdvance(QStringLiteral(" ×%1").arg(n)):0)+20;};
        int first=history.size(),used=0,rows=1;
        while(first>0){int w=itemWidth(history[first-1].toMap())+7;if(used+w>right-52){if(rows==2)break;++rows;used=0;}used+=w;--first;}
        if(history.isEmpty())label(x,43,QStringLiteral("等待输入"),muted,right-x);
        for(int i=first;i<history.size();++i){
            const auto item=history[i].toMap();const QString key=item.value("key").toString();const int n=item.value("count").toInt(),w=itemWidth(item);const QColor color=item.value("held").toBool()?cyan:text;
            if(x+w>right){x=52;hy+=32;}if(hy>72)break;
            p.setPen(Qt::NoPen);p.setBrush(item.value("held").toBool()?QColor("#20465b"):QColor("#1b2938"));p.drawRoundedRect(QRectF(x,hy,w,26),4,4);int ix=x+10;
            if(key.startsWith("mouse-")){
                QRectF mouse(ix,hy+5,12,16);p.setPen(QPen(color,1));p.setBrush(Qt::NoBrush);p.drawRoundedRect(mouse,4,4);p.drawLine(QPointF(ix,hy+12),QPointF(ix+12,hy+12));p.drawLine(QPointF(ix+6,hy+5),QPointF(ix+6,hy+12));p.setPen(Qt::NoPen);p.setBrush(color);
                if(key=="mouse-left")p.drawRoundedRect(QRectF(ix+1,hy+6,4,5),1,1);else if(key=="mouse-right")p.drawRoundedRect(QRectF(ix+7,hy+6,4,5),1,1);else p.drawRect(QRectF(ix+5,hy+7,2,6));ix+=18;
            }else{label(ix,hy+3,key,color,w-20);ix+=fm.horizontalAdvance(key);}
            if(n>1)label(ix,hy+3,QStringLiteral(" ×%1").arg(n),color,w-(ix-x)-5);x+=w+7;
        }
        const int statusX=pointerButton->geometry().right()+12;
        label(statusX,112,status,assistButton->isChecked()?green:muted,right-statusX);
        const int gx=compact?12:width()-rightWidth+12;const int gy=compact?148:8;const int tx=gx+graphW+22;
        label(gx,gy,QStringLiteral("FPS  %1").arg(m_fps),green,graphW);
        label(gx,gy+23,QStringLiteral("丢帧  %1 /s").arg(m_data.value("skipped").toInt()),amber,graphW);
        label(tx,gy,QStringLiteral("本机排队  %1 ms").arg(m_data.value("queueMs").toDouble(),0,'f',1),cyan,graphW);
        label(tx,gy+23,QStringLiteral("UI 延迟  +%1 ms").arg(m_data.value("uiGapMs").toInt()),red,graphW);
        double fpsMax=60,dropMax=1,timeMax=10;for(int i=0;i<count;++i){fpsMax=qMax(fpsMax,samples[i][0]);dropMax=qMax(dropMax,samples[i][1]);timeMax=qMax(timeMax,qMax(samples[i][2],samples[i][3]));}
        auto curve=[&](int column,QRectF plot,double maximum,QColor color){QPainterPath path;for(int i=0;i<count;++i){int k=(next-count+i+60)%60;QPointF pt(plot.right()-(count-1-i)*plot.width()/59.0,plot.bottom()-samples[k][column]*plot.height()/maximum);if(!i)path.moveTo(pt);else path.lineTo(pt);}p.setPen(QPen(color,1.35));p.setBrush(Qt::NoBrush);p.drawPath(path);};
        curve(0,QRectF(gx,gy+53,graphW,64),fpsMax,green);curve(1,QRectF(gx,gy+53,graphW,64),dropMax,amber);curve(2,QRectF(tx,gy+53,graphW,64),timeMax,cyan);curve(3,QRectF(tx,gy+53,graphW,64),timeMax,red);
    }
private:
    QVariantMap m_data;QString status=QStringLiteral("已关闭");std::array<std::array<double,4>,60> samples{};
    int next=0,count=0,m_fps=0;double queuePeak=0,uiPeak=0;QElapsedTimer clock;
};
