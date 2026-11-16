#pragma once
#include <QWidget>
#include <QPainter>
#include <QRegion>
#include <QPointer>
#include <QFile>
#include <QMouseEvent>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRandomGenerator>
#include <functional>

class DirectionRegion final : public QWidget {
public:
    QRectF region{283.0/1080,377.0/484,493.0/1080,54.0/484};
    bool editing=false;
    std::function<void()> changed;
    explicit DirectionRegion(QWidget *video):QWidget(video->parentWidget()?video->parentWidget():video),surface(video){
        setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);
        setStyleSheet("background:transparent;border:none;");setMouseTracking(true);video->installEventFilter(this);syncGeometry();hide();
    }
    static QRectF defaultRegion(){return QRectF(283.0/1080,377.0/484,493.0/1080,54.0/484);}
    void setEditing(bool value){editing=value;setAttribute(Qt::WA_TransparentForMouseEvents,!value);setCursor(value?Qt::SizeAllCursor:Qt::ArrowCursor);refresh();}
    QRectF pixels() const{return QRectF(region.x()*width(),region.y()*height(),region.width()*width(),region.height()*height());}
    void refresh(){
        const QRect outer=pixels().adjusted(-3,-3,4,4).toAlignedRect();
        QRegion shape=QRegion(outer)-QRegion(pixels().adjusted(3,3,-3,-3).toAlignedRect());
        if(editing){shape+=QRegion(QRect(10,10,qMin(width()-20,410),25));shape+=QRegion(QRect(pixels().bottomRight().toPoint()-QPoint(8,8),QSize(14,14)));}
        setMask(shape);update();
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Resize || event->type()==QEvent::Move){syncGeometry();refresh();raise();}
        if(editing){
            if(event->type()==QEvent::MouseButtonPress){mousePressEvent(static_cast<QMouseEvent*>(event));return true;}
            if(event->type()==QEvent::MouseMove){mouseMoveEvent(static_cast<QMouseEvent*>(event));return true;}
            if(event->type()==QEvent::MouseButtonRelease){mouseReleaseEvent(static_cast<QMouseEvent*>(event));return true;}
        }
        return false;
    }
    void showEvent(QShowEvent *) override {syncGeometry();refresh();}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QRectF box=pixels();
        p.setPen(QPen(QColor("#7de0b2"),1.5,editing?Qt::DashLine:Qt::SolidLine));p.setBrush(Qt::NoBrush);p.drawRect(box);
        if(editing){p.fillRect(QRectF(box.bottomRight()-QPointF(7,7),QSizeF(10,10)),QColor("#7de0b2"));
            QFont f(QStringLiteral("Microsoft YaHei UI"));f.setPixelSize(12);p.setFont(f);
            const QString hint=QStringLiteral("拖动移动 · 右下角缩放 · 框外重新框选 · F7 完成");QRectF label(10,10,qMin(width()-20,410),25);p.fillRect(label,QColor(18,27,38,220));p.drawText(label,Qt::AlignCenter,hint);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton)return;drag=true;start=e->pos();original=pixels();
        mode=QRectF(original.bottomRight()-QPointF(14,14),QSizeF(28,28)).contains(start)?2:original.contains(start)?1:0;e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(!drag)return;QPointF point(qBound(0,e->x(),width()),qBound(0,e->y(),height()));QRectF box;
        if(mode==1){box=original.translated(point-start);box.moveLeft(qBound(0.0,box.left(),width()-box.width()));box.moveTop(qBound(0.0,box.top(),height()-box.height()));}
        else if(mode==2)box=QRectF(original.topLeft(),QPointF(qMax(original.left()+40,point.x()),qMax(original.top()+12,point.y())));
        else box=QRectF(start,point).normalized();
        box=box.intersected(rect());if(box.width()>=40 && box.height()>=12){region=QRectF(box.x()/width(),box.y()/height(),box.width()/width(),box.height()/height());refresh();}e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {if(e->button()==Qt::LeftButton && drag){drag=false;if(changed)changed();e->accept();}}
private:
    QPointer<QWidget> surface;
    void syncGeometry(){if(surface)setGeometry(parentWidget()==surface?surface->rect():surface->geometry());}
    bool drag=false;int mode=0;QPointF start;QRectF original;
};

// Recognition runs off the UI thread, with a single outstanding bounded crop.
// Only the owning window can issue mapped input; no desktop keyboard injection.
class DirectionAssistant final : public QObject {
public:
    DirectionRegion *overlay;
    std::function<bool()> canSend;
    std::function<void(int,bool)> sendKey;
    std::function<void(bool)> setMapping;
    std::function<void(QString)> showStatus;
    bool enabled=false,editing=false;
    explicit DirectionAssistant(QWidget *video,QObject *owner):QObject(owner),overlay(new DirectionRegion(video)){
        elapsed.start();process.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&process,&QProcess::readyReadStandardOutput,this,[this]{readReply();});
        QObject::connect(&process,&QProcess::readyReadStandardError,this,[this]{process.readAllStandardError();});
        QObject::connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){ready=false;busy=false;abortInput();status(QStringLiteral("识别模块启动失败"));});
        QObject::connect(&process,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){ready=false;busy=false;abortInput();if(enabled)status(QStringLiteral("识别模块已停止，请重新开启"));});
        inputTimer.setSingleShot(true);QObject::connect(&inputTimer,&QTimer::timeout,this,[this]{inputStep();});
        monitor.setInterval(50);QObject::connect(&monitor,&QTimer::timeout,this,[this]{
            if(!eligible()){abortInput();if(enabled)status(editing?QStringLiteral("调整范围中，输入暂停"):QStringLiteral("等待前台映射模式 · Ctrl"));}
            else if(ready && sequence.isEmpty() && elapsed.elapsed()-lastFrame>500){status(QStringLiteral("等待投屏画面"));}
            if(busy && elapsed.elapsed()-requestAt>5000){busy=false;ready=false;process.kill();abortInput();status(QStringLiteral("识别超时，请重新开启"));}
        });monitor.start();
        overlay->changed=[this]{manualRegion=true;resetRegionState();save();};
    }
    ~DirectionAssistant() override {abortInput();process.kill();process.waitForFinished(500);}
    void setSerial(const QString &value){
        serial=value;QSettings settings(QCoreApplication::applicationDirPath()+"/direction-assist.ini",QSettings::IniFormat);
        QFile legacy(QCoreApplication::applicationDirPath()+"/direction-region-presets.json");
        if(legacy.open(QIODevice::ReadOnly)){
            const auto data=QJsonDocument::fromJson(legacy.readAll()).object();
            if(data.value("last_source").toObject().value("title").toString().contains(serial)){
                const auto records=data.value("regions").toObject();double best=-1;
                for(auto it=records.begin();it!=records.end();++it){auto item=it.value().toObject();auto size=item.value("size").toArray();auto roi=item.value("relative").toArray();
                    if(size.size()==2 && roi.size()==4 && size[0].toDouble()>best){best=size[0].toDouble();remembered=QRectF(roi[0].toDouble(),roi[1].toDouble(),roi[2].toDouble(),roi[3].toDouble());}
                }
            }
        }
        manualRegion=settings.value(serial+"/manual",false).toBool();
        QRectF stored=settings.value(serial+"/region",remembered).toRectF();
        if(stored.width()>.02 && stored.height()>.01 && QRectF(0,0,1,1).contains(stored))overlay->region=stored;
        overlay->refresh();
    }
    void enable(bool value){
        enabled=value;abortInput();overlay->setVisible(value||editing);overlay->raise();
        if(!value){status(QStringLiteral("已关闭"));return;}
        if(!editing && setMapping)setMapping(true);
        if(process.state()==QProcess::NotRunning){
            const QString path=QCoreApplication::applicationDirPath()+"/DirectionWorker/DirectionWorker.exe";
            if(!QFileInfo::exists(path)){status(QStringLiteral("缺少方向识别模块"));return;}
            ready=false;buffer.clear();process.start(path,QStringList());status(QStringLiteral("正在加载识别"));
        }else status(QStringLiteral("等待完整方向序列"));
    }
    void edit(bool value){editing=value;abortInput();overlay->setEditing(value);overlay->setVisible(enabled||value);overlay->raise();if(setMapping)setMapping(!value && enabled);if(!value)save();}
    void resetRegion(){manualRegion=false;located=false;lastLocate=-1500;overlay->region=remembered;overlay->refresh();resetRegionState();save();}
    void stop(){enable(false);}
    void abortInput(){inputTimer.stop();if(heldKey && sendKey)sendKey(heldKey,false);heldKey=0;sequence.clear();index=0;}
    void frame(int w,int h,const uint8_t *y,const uint8_t *u,const uint8_t *v,int sy,int su,int sv){
        lastFrame=elapsed.elapsed();
        if(!eligible() || !ready || busy || !sequence.isEmpty() || lastFrame-lastSample<40 || w<2 || h<2)return;
        if(frameSize!=QSize(w,h)){frameSize=QSize(w,h);located=false;resetRegionState();}
        lastSample=lastFrame;
        const bool locating=!manualRegion && !located && lastFrame-lastLocate>=800;
        if(locating)lastLocate=lastFrame;
        const int ow=locating?1000:495,oh=locating?qBound(50,qRound(h*1000.0/w),1080):54;
        QByteArray rgb(ow*oh*3,Qt::Uninitialized);unsigned char *out=reinterpret_cast<unsigned char*>(rgb.data());const QRectF r=locating?QRectF(0,0,1,1):overlay->region;
        for(int py=0;py<oh;++py){int iy=qBound(0,int((r.y()+(py+.5)*r.height()/oh)*h),h-1);
            for(int px=0;px<ow;++px){int ix=qBound(0,int((r.x()+(px+.5)*r.width()/ow)*w),w-1);
                int c=int(y[iy*sy+ix])-16,d=int(u[(iy/2)*su+ix/2])-128,e=int(v[(iy/2)*sv+ix/2])-128;
                *out++=static_cast<unsigned char>(qBound(0,(298*c+409*e+128)>>8,255));*out++=static_cast<unsigned char>(qBound(0,(298*c-100*d-208*e+128)>>8,255));*out++=static_cast<unsigned char>(qBound(0,(298*c+516*d+128)>>8,255));
            }
        }
        QJsonObject object{{"epoch",epoch},{"time",elapsed.elapsed()/1000.0},{"rgb",QString::fromLatin1(rgb.toBase64())},{"locate",locating},{"width",ow},{"height",oh}};
        write(object);busy=true;requestAt=elapsed.elapsed();
    }
private:
    QProcess process;QTimer inputTimer,monitor;QElapsedTimer elapsed;QByteArray buffer;QString serial;
    bool ready=false,busy=false;int epoch=0,index=0,heldKey=0;QStringList sequence;QSize frameSize;
    bool manualRegion=false,located=false;QRectF remembered=DirectionRegion::defaultRegion();qint64 lastLocate=-1500;
    qint64 lastSample=-100,lastFrame=0,requestAt=0;
    bool eligible()const{return enabled && !editing && canSend && canSend();}
    void status(const QString &s){if(showStatus)showStatus(s);}
    void save(){if(serial.isEmpty())return;QSettings settings(QCoreApplication::applicationDirPath()+"/direction-assist.ini",QSettings::IniFormat);settings.setValue(serial+"/region",overlay->region);settings.setValue(serial+"/manual",manualRegion);}
    void resetRegionState(){++epoch;abortInput();}
    void write(const QJsonObject &object){process.write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');}
    void readReply(){
        buffer+=process.readAllStandardOutput();int newline;
        while((newline=buffer.indexOf('\n'))>=0){QByteArray line=buffer.left(newline);buffer.remove(0,newline+1);const auto object=QJsonDocument::fromJson(line).object();
            if(object.value("ready").toBool()){ready=true;if(enabled)status(QStringLiteral("等待完整方向序列"));continue;}
            busy=false;
            if(object.contains("error")){status(QStringLiteral("识别异常：")+object.value("error").toString());continue;}
            if(object.value("locate").toBool()){
                if(object.value("epoch").toInt()!=epoch || !enabled || editing || manualRegion)continue;
                const auto a=object.value("region").toArray();
                if(a.size()==4){QRectF found(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble());
                    if(found.width()>.05 && found.height()>.015 && QRectF(0,0,1,1).contains(found)){
                        located=true;
                        // Do not rearm an already committed sequence after a small region correction.
                        overlay->region=found;overlay->refresh();save();status(QStringLiteral("已按四方向按钮定位"));
                    }
                }
                continue;
            }
            if(object.value("epoch").toInt()!=epoch || !eligible() || elapsed.elapsed()-requestAt>300 || !sequence.isEmpty())continue;
            const auto array=object.value("sequence").toArray();
            if(array.isEmpty()){status(QStringLiteral("识别中 · 等待稳定序列 · %1 ms").arg(object.value("ms").toDouble(),0,'f',1));continue;}
            for(const auto &value:array){const QString key=value.toString();if(key!="up" && key!="down" && key!="left" && key!="right"){sequence.clear();break;}sequence.append(key);}
            if(sequence.size()<3 || sequence.size()>9){sequence.clear();continue;}
            QString arrows;for(const auto &key:sequence)arrows+=key=="up"?QStringLiteral("↑"):key=="down"?QStringLiteral("↓"):key=="left"?QStringLiteral("←"):QStringLiteral("→");
            status(QStringLiteral("输入 %1 · %2%").arg(arrows).arg(qRound(object.value("confidence").toDouble()*100)));
            index=0;inputStep();
        }
    }
    void inputStep(){
        if(heldKey){if(sendKey)sendKey(heldKey,false);heldKey=0;if(!eligible()){abortInput();return;}inputTimer.start(20+int(QRandomGenerator::global()->bounded(21)));return;}
        if(!eligible()){abortInput();return;}
        if(index>=sequence.size()){sequence.clear();return;}
        if(index==0){QJsonArray committed;for(const auto &key:sequence)committed.append(key);write(QJsonObject{{"epoch",epoch},{"time",elapsed.elapsed()/1000.0},{"commit",committed}});}
        const QString key=sequence[index++];heldKey=key=="up"?Qt::Key_Up:key=="down"?Qt::Key_Down:key=="left"?Qt::Key_Left:Qt::Key_Right;
        if(sendKey)sendKey(heldKey,true);inputTimer.start(26+int(QRandomGenerator::global()->bounded(9)));
    }
};
