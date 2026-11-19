#include <QApplication>
#include <QWidget>
#include <QTest>
#include <QtEndian>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <stdexcept>
#include <iostream>
#include "controller.h"
#include "inputconvertgame.h"

class AuditHarness : public InputConvertGame {
public:
    explicit AuditHarness(Controller *controller) : InputConvertGame(controller) {}
    QSet<quint32> physicallyDown;
protected:
    bool hostKeyDown(quint32 key) const override { return physicallyDown.contains(key); }
};

static void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}

int main(int argc, char **argv) {
    try {
    QApplication app(argc, argv);
    QFile file(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    require(file.open(QIODevice::ReadOnly), "profile could not be opened");
    const QString profile = QString::fromUtf8(file.readAll());
    QSet<quint64> active;
    int downs = 0, ups = 0, moves = 0;
    struct TouchPacket { int action; QPoint pos; qint64 time; quint64 id; };
    QVector<TouchPacket> packets;
    QElapsedTimer packetClock; packetClock.start();
    Controller controller([&](const QByteArray &data) -> qint64 {
        if (data.size() >= 32 && static_cast<unsigned char>(data[0]) == 2) {
            const auto action = static_cast<unsigned char>(data[1]);
            const auto id = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(data.constData()+2));
            packets.append({action, QPoint(
                qFromBigEndian<qint32>(reinterpret_cast<const uchar *>(data.constData()+10)),
                qFromBigEndian<qint32>(reinterpret_cast<const uchar *>(data.constData()+14))), packetClock.elapsed(), id});
            if (action == 0) { require(!active.contains(id), "duplicate DOWN on active pointer"); active.insert(id); ++downs; }
            if (action == 1) { active.remove(id); ++ups; }
            if (action == 2) { ++moves; }
        }
        return data.size();
    }, profile);
    const QSize size(1920, 1080);
    auto flush = [] { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); };
    auto key = [&](int code, QEvent::Type type, bool repeat=false) {
        QKeyEvent event(type, code, Qt::NoModifier, QString(), repeat);
        controller.keyEvent(&event, size, size); flush();
    };
    auto toggle = [&] { key(Qt::Key_Control,QEvent::KeyPress); key(Qt::Key_Control,QEvent::KeyRelease); };
    auto mouse = [&](QEvent::Type type) {
        QMouseEvent event(type,QPointF(900,700),Qt::LeftButton,
                          type==QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,Qt::NoModifier);
        controller.mouseEvent(&event,size,size);flush();
    };

    require(!controller.isCurrentCustomKeymap(), "initial mode must be normal");
    mouse(QEvent::MouseButtonPress);
    require(!active.isEmpty(), "normal mouse press must create a touch");
    toggle();
    require(active.isEmpty(), "entering custom mode must release a held normal click");
    mouse(QEvent::MouseButtonRelease);
    toggle();
    QWidget normalWindow;
    mouse(QEvent::MouseButtonPress);
    QEvent normalDeactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&normalWindow,&normalDeactivate);flush();
    require(active.isEmpty(), "normal-mode focus loss must release mouse touch");
    mouse(QEvent::MouseButtonRelease);
    std::cout << "PASS: held normal mouse click cleaned on switch and focus loss\n";
    toggle();
    QJsonObject steer;
    for (const auto entry : QJsonDocument::fromJson(profile.toUtf8()).object()["keyMapNodes"].toArray()) {
        if (entry.toObject()["type"].toString()=="KMT_STEER_WHEEL") { steer=entry.toObject(); break; }
    }
    require(!steer.isEmpty(), "profile must contain the steering wheel");
    const QPointF center(steer["centerPos"].toObject()["x"].toDouble(), steer["centerPos"].toObject()["y"].toDouble());
    auto expectedPosition = [&](int direction) {
        QPointF position=center;
        if (direction==Qt::Key_W) position.ry()-=steer["upOffset"].toDouble();
        if (direction==Qt::Key_S) position.ry()+=steer["downOffset"].toDouble();
        if (direction==Qt::Key_A) position.rx()-=steer["leftOffset"].toDouble();
        if (direction==Qt::Key_D) position.rx()+=steer["rightOffset"].toDouble();
        return QPointF(position.x()*size.width(),position.y()*size.height()).toPoint();
    };
    for (const int direction : {Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D}) {
        packets.clear();
        key(direction,QEvent::KeyPress);
        require(packets.size()==1 && packets[0].action==0 && packets[0].pos==expectedPosition(direction),
                "first DOWN must immediately land on the configured direction, not the center");
        QTest::qWait(50); flush();
        require(packets.size()==1,"stationary hold must not require a delayed drag");
        key(direction,QEvent::KeyRelease);
        require(active.isEmpty() && packets.size()==2 && packets[1].action==1 && packets[1].pos==packets[0].pos,
                "direction must release immediately at its pressed position");
    }
    packets.clear();
    for (int i=0;i<40;++i) {
        const int direction=QVector<int>{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D}[i%4];
        key(direction,QEvent::KeyPress); key(direction,QEvent::KeyRelease);
        require(active.isEmpty() && packets.size()==2*(i+1),"each rapid tap must have its own immediate DOWN/UP pair");
        require(packets[2*i].action==0 && packets[2*i+1].action==1 &&
                packets[2*i].pos==expectedPosition(direction) && packets[2*i+1].pos==expectedPosition(direction),
                "rapid taps must keep their direction and must not merge or visit the center");
    }
    QTest::qWait(70); flush();
    require(packets.size()==80 && active.isEmpty(),"rapid taps must leave no delayed input");
    packets.clear();
    key(Qt::Key_W,QEvent::KeyPress); key(Qt::Key_D,QEvent::KeyPress); key(Qt::Key_W,QEvent::KeyRelease);
    require(packets.size()==3 && packets[1].action==2 && packets[2].action==2 &&
            packets[1].pos==QPoint(expectedPosition(Qt::Key_D).x(),expectedPosition(Qt::Key_W).y()) &&
            packets[2].pos==expectedPosition(Qt::Key_D),
            "W+D then release W must update the same touch to diagonal then right");
    key(Qt::Key_D,QEvent::KeyRelease);
    require(active.isEmpty(),"last chord release must end the touch");
    std::cout << "PASS: direct WASD positions, standalone holds, 40 rapid taps, immediate releases and chords\n";
    key(Qt::Key_W,QEvent::KeyPress); QTest::qWait(30); flush();
    require(!active.isEmpty(), "W must produce a touch down");
    toggle();
    require(active.isEmpty(), "Ctrl must release W before its physical key-up");
    key(Qt::Key_W,QEvent::KeyRelease);
    QTest::qWait(100); flush();
    require(active.isEmpty(), "old joystick timer must not restart a touch");
    std::cout << "PASS: held W -> Ctrl -> late W release\n";

    toggle(); key(Qt::Key_W,QEvent::KeyPress); key(Qt::Key_D,QEvent::KeyPress); mouse(QEvent::MouseButtonPress);
    require(active.size() >= 2, "diagonal movement plus attack must be multi-touch");
    toggle(); require(active.isEmpty(), "all held joystick and attack touches must end");
    key(Qt::Key_W,QEvent::KeyRelease); key(Qt::Key_D,QEvent::KeyRelease); mouse(QEvent::MouseButtonRelease);
    std::cout << "PASS: W+D+attack -> Ctrl clears all touches\n";

    toggle();
    key(Qt::Key_Control,QEvent::KeyPress,true);
    require(controller.isCurrentCustomKeymap(), "auto-repeat must not toggle the mode");
    toggle();
    std::cout << "PASS: repeated Ctrl press ignored\n";

    toggle();key(Qt::Key_W,QEvent::KeyPress);mouse(QEvent::MouseButtonPress);
    QWidget window;
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&window,&deactivate);flush();
    require(active.isEmpty() && !controller.isCurrentCustomKeymap(), "deactivation must clear input and enter normal mode");
    std::cout << "PASS: window deactivation releases input\n";

    toggle();key(Qt::Key_W,QEvent::KeyPress);mouse(QEvent::MouseButtonPress);
    controller.updateScript(profile);flush();
    require(active.isEmpty(), "script reload must release old touches");
    require(!QGuiApplication::overrideCursor(), "script reload must restore cursor");
    std::cout << "PASS: script reload releases input and restores cursor\n";

    toggle();key(Qt::Key_Alt,QEvent::KeyPress);toggle();
    QTest::qWait(120);flush();
    require(active.isEmpty(), "delayed small-eyes callback must not create a touch after exit");
    std::cout << "PASS: delayed small-eyes callbacks cancelled\n";

    toggle();
    key(Qt::Key_CapsLock,QEvent::KeyPress);
    QTest::qWait(80); flush();
    key(Qt::Key_CapsLock,QEvent::KeyRelease);
    QTest::qWait(650); flush();
    require(active.isEmpty(), "small-eyes release without further mouse movement must not leave a touch");
    std::cout << "PASS: small-eyes release without further mouse movement\n";
    key(Qt::Key_CapsLock,QEvent::KeyPress);
    key(Qt::Key_CapsLock,QEvent::KeyRelease);
    QTest::qWait(650); flush();
    require(active.isEmpty(), "quick small-eyes tap must invalidate delayed restart");
    std::cout << "PASS: quick small-eyes tap leaves no delayed touch\n";
    auto viewMove = [&](QPointF pos) {
        QMouseEvent event(QEvent::MouseMove,pos,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        controller.mouseEvent(&event,size,size);flush();
    };
    viewMove(QPointF(900,500)); viewMove(QPointF(1000,510));
    require(active.size()==1,"view movement must start a view touch");
    QTest::qWait(650);flush();
    require(active.isEmpty(),"idle mouse view must release without another mouse event");
    key(Qt::Key_CapsLock,QEvent::KeyPress);
    QTest::qWait(650);flush();
    require(active.size()==1,"deliberately held small-eyes key must remain held");
    key(Qt::Key_CapsLock,QEvent::KeyRelease);flush();
    require(active.isEmpty(),"small-eyes release must stop immediately");
    std::cout << "PASS: mouse view idle release; intentional small-eyes hold preserved\n";
    toggle();

    QJsonObject wheelProfileObject = QJsonDocument::fromJson(profile.toUtf8()).object();
    auto wheelNodes = wheelProfileObject["keyMapNodes"].toArray();
    wheelNodes.append(QJsonObject{
        {"type", "KMT_MOUSE_DRAG"}, {"key", "MiddleButton"},
        {"pos", QJsonObject{{"x", 0.75}, {"y", 0.50}}}, {"radius", 0.12}, {"sensitivity", 1.0},
    });
    wheelProfileObject["keyMapNodes"] = wheelNodes;
    const QString wheelProfile = QString::fromUtf8(QJsonDocument(wheelProfileObject).toJson(QJsonDocument::Compact));
    const QSize wheelViewSize(960, 540);
    const QPoint wheelAnchor(1440, 540);
    auto wheelMouse = [&](QEvent::Type type, const QPointF &pos, Qt::MouseButtons buttons) {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::MiddleButton;
        QMouseEvent event(type, pos, button, buttons, Qt::NoModifier);
        controller.mouseEvent(&event, size, wheelViewSize);
        flush();
    };
    auto wheelKey = [&](int code, QEvent::Type type) {
        QKeyEvent event(type, code, Qt::NoModifier);
        controller.keyEvent(&event, size, wheelViewSize);
        flush();
    };
    controller.updateScript(wheelProfile);
    toggle();
    packets.clear();
    wheelMouse(QEvent::MouseMove, QPointF(480, 270), Qt::NoButton);
    wheelMouse(QEvent::MouseMove, QPointF(500, 270), Qt::NoButton);
    require(active.size() == 1 && packets.size() >= 2 && packets.first().action == 0,
            "a view touch must be active before switching to the radial wheel");
    const quint64 viewID = packets.first().id;
    packets.clear();
    wheelMouse(QEvent::MouseButtonPress, QPointF(500, 270), Qt::MiddleButton);
    require(active.size() == 1 && packets.size() == 2 && packets[0].action == 1 && packets[0].id == viewID
            && packets[1].action == 0 && packets[1].pos == wheelAnchor,
            "middle press must lift the view before touching the wheel anchor");
    const quint64 wheelID = packets[1].id;
    QTest::qWait(550); flush();
    require(active.contains(wheelID) && packets.size() == 2, "holding the wheel must outlast the view idle timer");
    wheelMouse(QEvent::MouseMove, QPointF(520, 258), Qt::MiddleButton);
    require(packets.size() == 3 && packets.last().action == 2 && packets.last().id == wheelID
            && packets.last().pos == wheelAnchor + QPoint(40, -24),
            "relative mouse movement must drag the same wheel touch at the displayed scale");
    wheelKey(Qt::Key_W, QEvent::KeyPress);
    require(active.size() == 2 && packets.last().id != wheelID, "wheel and movement must use separate touches");
    wheelKey(Qt::Key_W, QEvent::KeyRelease);
    require(active.size() == 1 && active.contains(wheelID), "releasing movement must preserve the wheel touch");
    packets.clear();
    wheelMouse(QEvent::MouseMove, QPointF(650, 120), Qt::MiddleButton);
    require(packets.size() == 1 && packets.last().action == 2 && packets.last().id == wheelID
            && (packets.last().pos - (wheelAnchor + QPoint(92, -92))).manhattanLength() <= 2,
            "the wheel must preserve diagonal angles while clamping to its radius");
    const QPoint selection = packets.last().pos;
    wheelMouse(QEvent::MouseButtonRelease, QPointF(650, 120), Qt::NoButton);
    require(active.isEmpty() && packets.size() == 2 && packets.last().action == 1
            && packets.last().id == wheelID && packets.last().pos == selection,
            "middle release must lift the wheel at the selected position");
    wheelMouse(QEvent::MouseMove, QPointF(680, 120), Qt::NoButton);
    require(active.size() == 1 && packets.size() == 4 && packets[2].action == 0 && packets[2].pos != wheelAnchor
            && packets[3].action == 2,
            "the first movement after middle release must pan the view");

    wheelMouse(QEvent::MouseButtonDblClick, QPointF(680, 120), Qt::MiddleButton);
    require(active.size() == 1 && packets.last().action == 0 && packets.last().pos == wheelAnchor,
            "a Qt double-click must initiate the next wheel hold");
    wheelMouse(QEvent::MouseMove, QPointF(680, 160), Qt::MiddleButton);
    require(packets.last().action == 2 && packets.last().pos == wheelAnchor + QPoint(0, 80),
            "a new wheel hold must start with no previous drag offset");
    QEvent wheelDeactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&window, &wheelDeactivate); flush();
    require(active.isEmpty() && !controller.isCurrentCustomKeymap(), "focus loss must clear the held wheel");
    packets.clear();
    wheelMouse(QEvent::MouseButtonRelease, QPointF(680, 160), Qt::NoButton);
    require(packets.isEmpty(), "the late middle release after focus loss must not inject another touch");
    controller.updateScript(profile); flush();
    std::cout << "PASS: radial wheel anchor, relative drag, radius, simultaneous WASD, selection and camera restoration\n";

    for (int i=0;i<50;++i) {
        toggle();key(Qt::Key_W,QEvent::KeyPress);toggle();key(Qt::Key_W,QEvent::KeyRelease);
    }
    QTest::qWait(120);flush();
    require(active.isEmpty(), "repeated sessions must not leak pointers");
    require(!QGuiApplication::overrideCursor(), "repeated sessions must not leak cursor overrides");
    const QStringList history=controller.inputDiagnostics().value("history").toStringList();
    require(history.size()<=32 && !history.isEmpty() && !history.last().endsWith(QStringLiteral(" ↓")),"rapid input history must include releases and remain bounded");
    std::cout << "PASS: bounded event history retains rapid key releases\n";
    {
        AuditHarness audit(&controller);
        audit.loadKeyMap(profile);
        auto nativeKey = [&](int code, QEvent::Type type) {
            QKeyEvent event(type,code,Qt::NoModifier,0,static_cast<quint32>(code),0);
            audit.keyEvent(&event,size,size);flush();
        };
        nativeKey(Qt::Key_Control,QEvent::KeyPress);
        nativeKey(Qt::Key_Control,QEvent::KeyRelease);
        audit.physicallyDown = {Qt::Key_W,Qt::Key_D};
        nativeKey(Qt::Key_W,QEvent::KeyPress);
        nativeKey(Qt::Key_D,QEvent::KeyPress);
        audit.physicallyDown.remove(Qt::Key_W);
        QTest::qWait(350);flush();
        require(active.size()==1,"repair W must preserve held D on the shared joystick");
        audit.physicallyDown.remove(Qt::Key_D);
        QTest::qWait(350);flush();
        require(active.isEmpty(),"lost final D release must be repaired from OS state");
        audit.physicallyDown.insert(Qt::Key_W);
        nativeKey(Qt::Key_W,QEvent::KeyPress);
        nativeKey(Qt::Key_W,QEvent::KeyPress);
        QTest::qWait(650);flush();
        require(active.size()==1,"watchdog must preserve a genuinely held W; duplicate down must be harmless");
        nativeKey(Qt::Key_W,QEvent::KeyRelease);
        require(active.isEmpty(),"normal release must still work after watchdog");
        nativeKey(Qt::Key_Control,QEvent::KeyPress);
        nativeKey(Qt::Key_Control,QEvent::KeyRelease);
        std::cout << "PASS: lost W/D releases repaired; held D preserved; long holds and duplicate down safe\n";
#ifdef Q_OS_WIN
        audit.loadKeyMap(wheelProfile);
        nativeKey(Qt::Key_Control, QEvent::KeyPress);
        nativeKey(Qt::Key_Control, QEvent::KeyRelease);
        audit.physicallyDown.insert(VK_MBUTTON);
        const QMouseEvent middleDown(QEvent::MouseButtonPress, QPointF(500, 270), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        audit.mouseEvent(&middleDown, size, wheelViewSize); flush();
        const QMouseEvent middleDrag(QEvent::MouseMove, QPointF(500, 310), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        audit.mouseEvent(&middleDrag, size, wheelViewSize); flush();
        require(active.size() == 1 && packets.last().action == 2, "a wheel touch must still be held before simulated lost UP");
        audit.physicallyDown.remove(VK_MBUTTON);
        QTest::qWait(350); flush();
        require(active.isEmpty() && packets.last().action == 1, "the Windows mouse watchdog must recover a lost middle release");
        nativeKey(Qt::Key_Control, QEvent::KeyPress);
        nativeKey(Qt::Key_Control, QEvent::KeyRelease);
        std::cout << "PASS: lost middle release clears the radial wheel\n";
#endif
    }
    require(downs==ups, "every touch-down must have a matching touch-up");
    std::cout << "PASS: 50 toggle cycles, balanced touch lifecycle: " << downs << " down / " << ups << " up, " << moves << " moves\n";
    if (argc > 2) {
        int packets = 0, beats = 0;
        bool finished = false, success = false;
        Controller barrier([&](const QByteArray &data) -> qint64 { ++packets; return data.size(); }, profile, nullptr, QString::fromLocal8Bit(argv[2]));
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat,&QTimer::timeout,[&] { ++beats; });
        QObject::connect(&barrier,&Controller::deviceTouchCancelFinished,[&](bool ok) { finished=true;success=ok; });
        heartbeat.start();
        QElapsedTimer elapsed;elapsed.start();
        require(barrier.cancelDeviceTouches(),"cancel process must launch");
        require(elapsed.elapsed()<100,"cancel must return without blocking UI");
        require(barrier.deviceCancelPending(),"input barrier must remain closed until cancel completes");
        QKeyEvent press(QEvent::KeyPress,Qt::Key_Control,Qt::NoModifier);
        barrier.keyEvent(&press,size,size);flush();
        require(!barrier.isCurrentCustomKeymap() && packets==0,"input during cancellation must be gated");
        while (!finished && elapsed.elapsed()<1800) { QTest::qWait(10); }
        require(finished && success && !barrier.deviceCancelPending(),"real device cancellation must complete successfully");
        require(beats>0,"render/event loop must keep ticking during cancellation");
        std::cout << "PASS: real device async cancel " << elapsed.elapsed() << " ms, UI heartbeats=" << beats << ", input gated\n";
    }
    return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << std::endl; return 1;
    }
}

