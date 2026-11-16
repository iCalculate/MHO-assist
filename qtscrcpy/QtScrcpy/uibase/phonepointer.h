#pragma once
#include <QObject>
#include <QWidget>
#include <QProcess>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QCursor>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <functional>

class PhonePointer final : public QObject {
public:
    std::function<bool()> ordinaryMode;
    std::function<void(QString)> status;
    explicit PhonePointer(QWidget *video,QObject *parent):QObject(parent),video(video){
        clock.start();timer.setInterval(33);timeout.setSingleShot(true);retry.setSingleShot(true);
        connect(&timer,&QTimer::timeout,this,[this]{tick();});timer.start();
        connect(&timeout,&QTimer::timeout,this,[this]{command.kill();fail(QStringLiteral("手机光标准备超时"));});
        connect(&command,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){finished(code);});
        connect(&command,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){fail(QStringLiteral("无法启动 ADB 光标连接"));});
        connect(&socket,&QTcpSocket::readyRead,this,[this]{if(socket.readAll().contains("READY")){ready=true;attempts=0;notify(QStringLiteral("手机光标已连接：普通鼠标模式跟随显示"));}});
        connect(&retry,&QTimer::timeout,this,[this]{if(enabled && port){socket.abort();socket.connectToHost(QHostAddress::LocalHost,port);}});
        connect(&socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){ready=false;if(enabled && ++attempts<8)retry.start(400);else notify(QStringLiteral("手机光标未连接，可关闭后重开"));});
    }
    ~PhonePointer() override {
        enabled=false;timer.stop();retry.stop();timeout.stop();socket.write("0 0 0\n");socket.flush();socket.abort();
        command.kill();command.waitForFinished(300);
        if(port){QProcess cleanup;cleanup.start(adb(),QStringList{"-s",serial,"forward","--remove",QStringLiteral("tcp:%1").arg(port)});cleanup.waitForFinished(500);}
    }
    void buttonEvent(bool down){buttonDown=down;if(down)++clickSequence;tick();}
    void setSerial(const QString &value){serial=value;if(enabled)start();}
    void enable(bool value){enabled=value;if(value)start();else{socket.write("0 0 0\n");socket.flush();notify(QStringLiteral("手机光标已关闭"));}}
private:
    QWidget *video;QProcess command;QTcpSocket socket;QTimer timer,timeout,retry;QElapsedTimer clock;
    bool buttonDown=false;quint64 clickSequence=0;
    QString serial;bool enabled=true,ready=false,failed=false;int stage=0,port=0,attempts=0;qint64 lastSent=0;QByteArray lastMessage;
    QString adb()const{return QCoreApplication::applicationDirPath()+"/adb.exe";}
    void notify(const QString &s){if(status)status(s);}
    void fail(const QString &s){failed=true;timeout.stop();ready=false;notify(s);}
    void run(int next,const QStringList &args){stage=next;command.start(adb(),QStringList{"-s",serial}+args);timeout.start(next==1?45000:8000);}
    void start(){
        if(serial.isEmpty() || command.state()!=QProcess::NotRunning)return;
        if(ready)return;failed=false;attempts=0;notify(QStringLiteral("正在准备手机光标…"));
        run(0,QStringList{"shell","dumpsys","package","org.qtscrcpy.pointer"});
    }
    void finished(int code){
        timeout.stop();const QByteArray output=command.readAllStandardOutput()+command.readAllStandardError();if(failed || !enabled)return;
        if(stage==0){
            if(!QRegularExpression("versionCode=2\\b").match(QString::fromUtf8(output)).hasMatch()){run(1,QStringList{"install","--no-incremental","-r",QCoreApplication::applicationDirPath()+"/qtscrcpy-pointer.apk"});return;}
        }else if(code!=0 || output.contains("Error:") || output.contains("Exception") || output.contains("Failure")){
            fail(QStringLiteral("手机光标未就绪：")+QString::fromUtf8(output).trimmed().left(180));return;
        }
        if(stage<=1)run(2,QStringList{"shell","appops","set","org.qtscrcpy.pointer","SYSTEM_ALERT_WINDOW","allow"});
        else if(stage==2)run(3,QStringList{"shell","am","start-foreground-service","-n","org.qtscrcpy.pointer/.PointerService"});
        else if(stage==3){
            if(port){socket.abort();socket.connectToHost(QHostAddress::LocalHost,port);}
            else run(4,QStringList{"forward","tcp:0","localabstract:qtscrcpy-pointer"});
        }else if(stage==4){port=QString::fromUtf8(output).trimmed().toInt();if(!port){fail(QStringLiteral("手机光标端口创建失败"));return;}socket.connectToHost(QHostAddress::LocalHost,port);}
    }
    void tick(){
        if(!ready || socket.state()!=QAbstractSocket::ConnectedState)return;
        const QPoint point=video->mapFromGlobal(QCursor::pos());const bool show=enabled && ordinaryMode && ordinaryMode() && video->rect().contains(point);
        if(!show)buttonDown=false;
        QByteArray message=show?QByteArray::number(double(point.x())/qMax(1,video->width()-1),'f',6)+' '+QByteArray::number(double(point.y())/qMax(1,video->height()-1),'f',6)+(buttonDown?" 2 ":" 1 ")+QByteArray::number(clickSequence)+"\n":QByteArray("0 0 0 ")+QByteArray::number(clickSequence)+"\n";
        if(message==lastMessage && clock.elapsed()-lastSent<200)return;
        if(socket.bytesToWrite()>4096){socket.abort();ready=false;return;}
        socket.write(message);lastMessage=message;lastSent=clock.elapsed();
    }
};
