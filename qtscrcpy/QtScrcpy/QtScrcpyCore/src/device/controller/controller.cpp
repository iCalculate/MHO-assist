#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QSharedPointer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "controller.h"
#include "controlmsg.h"
#include "inputconvertgame.h"
#include "receiver.h"
#include "videosocket.h"

Controller::Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript, QObject *parent, const QString &serial)
    : QObject(parent)
    , m_sendData(sendData)
    , m_deviceSerial(serial)
{
    m_inputClock.start();
    m_receiver = new Receiver(this);
    Q_ASSERT(m_receiver);

    updateScript(gameScript);
}

Controller::~Controller()
{
    m_shuttingDown = true;
    delete m_inputConvert;
    m_inputConvert = nullptr;
    cancelDeviceTouches();
}

bool Controller::cancelDeviceTouches()
{
    if (m_deviceSerial.isEmpty() || m_cameraMode) { return true; }
    QCoreApplication::sendPostedEvents(this, ControlMsg::Control);
    if (m_shuttingDown) {
        // Shutdown has no active renderer. Finish any barrier before disconnecting.
        if (m_cancelProcess) {
            m_cancelProcess->waitForFinished(1400);
            return !m_cancelProcess;
        }
        QProcess process;
        process.start(QDir(QCoreApplication::applicationDirPath()).filePath("adb.exe"),
                      {"-s", m_deviceSerial, "shell", "input", "touchscreen", "-d", "0", "motionevent", "CANCEL", "0", "0"});
        if (!process.waitForFinished(1200)) { process.kill(); process.waitForFinished(200); return false; }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    if (m_cancelProcess) { return true; }
    auto process = new QProcess(this);
    m_cancelProcess = process;
    auto timeout = new QTimer(process);
    timeout->setSingleShot(true);
    auto elapsed = QSharedPointer<QElapsedTimer>::create();
    elapsed->start();
    connect(timeout, &QTimer::timeout, process, [process]() {
        qWarning() << "device touch cancel timed out";
        process->kill();
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, timeout, elapsed](int code, QProcess::ExitStatus status) {
        timeout->stop();
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_cancelProcess = nullptr;
        qInfo() << "device touch cancel:" << (ok ? "success" : "failed") << "elapsed_ms=" << elapsed->elapsed();
        m_lastCancelMs = static_cast<int>(elapsed->elapsed());
        emit deviceTouchCancelFinished(ok);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, timeout](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) { return; }
        timeout->stop();
        m_cancelProcess = nullptr;
        qWarning() << "device touch cancel failed to start:" << process->errorString();
        emit deviceTouchCancelFinished(false);
        process->deleteLater();
    });
    process->setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("adb.exe"));
    process->setArguments({"-s", m_deviceSerial, "shell", "input", "touchscreen", "-d", "0",
                          "motionevent", "CANCEL", "0", "0"});
    process->start();
    timeout->start(1200);
    // Input is gated until finished, but the video/render event loop stays running.
    return true;
}

void Controller::postControlMsg(ControlMsg *controlMsg)
{
    if (!controlMsg) {
        return;
    }

    if (m_cameraMode) {
        const auto type = controlMsg->type();
        const bool isCameraControl = type == ControlMsg::CMT_CAMERA_SET_TORCH
                || type == ControlMsg::CMT_CAMERA_ZOOM_IN
                || type == ControlMsg::CMT_CAMERA_ZOOM_OUT;
        if (!isCameraControl) {
            qWarning() << "Ignoring display control message in camera mode:" << type;
            delete controlMsg;
            return;
        }
    }

    m_postedAt.insert(controlMsg, m_inputClock.nsecsElapsed());
    QCoreApplication::postEvent(this, controlMsg);
}

void Controller::setCameraMode(bool cameraMode)
{
    m_cameraMode = cameraMode;
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    if (!m_receiver) {
        return;
    }

    m_receiver->recvDeviceMsg(deviceMsg);
}

void Controller::test(QRect rc)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_MOUSE), AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_PRIMARY, rc, 1.0f);
    postControlMsg(controlMsg);
}

void Controller::updateScript(QString gameScript)
{
    if (m_inputConvert) {
        delete m_inputConvert;
    }
    if (!gameScript.isEmpty()) {
        InputConvertGame *convertgame = new InputConvertGame(this);
        convertgame->loadKeyMap(gameScript);
        m_inputConvert = convertgame;
    } else {
        m_inputConvert = new InputConvertNormal(this);
    }
    Q_ASSERT(m_inputConvert);
    connect(m_inputConvert, &InputConvertBase::grabCursor, this, &Controller::grabCursor);
}

bool Controller::isCurrentCustomKeymap()
{
    if (!m_inputConvert) {
        return false;
    }

    return m_inputConvert->isCurrentCustomKeymap();
}

void Controller::postBackOrScreenOn(bool down)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    controlMsg->setBackOrScreenOnData(down);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::postGoHome()
{
    postKeyCodeClick(AKEYCODE_HOME);
}

void Controller::postGoMenu()
{
    postKeyCodeClick(AKEYCODE_MENU);
}

void Controller::postGoBack()
{
    postKeyCodeClick(AKEYCODE_BACK);
}

void Controller::postAppSwitch()
{
    postKeyCodeClick(AKEYCODE_APP_SWITCH);
}

void Controller::postPower()
{
    postKeyCodeClick(AKEYCODE_POWER);
}

void Controller::postVolumeUp()
{
    postKeyCodeClick(AKEYCODE_VOLUME_UP);
}

void Controller::postVolumeDown()
{
    postKeyCodeClick(AKEYCODE_VOLUME_DOWN);
}

void Controller::copy()
{
    postKeyCodeClick(AKEYCODE_COPY);
}

void Controller::cut()
{
    postKeyCodeClick(AKEYCODE_CUT);
}

void Controller::expandNotificationPanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::expandSettingsPanel()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_EXPAND_SETTINGS_PANEL));
}

void Controller::collapsePanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_COLLAPSE_PANELS);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::rotateDevice()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_ROTATE_DEVICE));
}

void Controller::startApp(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_START_APP);
    controlMsg->setStartAppData(name);
    postControlMsg(controlMsg);
}

void Controller::scanFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SCAN_FILE);
    controlMsg->setScanFileData(path);
    postControlMsg(controlMsg);
}

void Controller::resizeDisplay(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return;
    }
    m_pendingResize = size;
    if (m_resizeQueued) {
        return;
    }
    m_resizeQueued = true;
    QTimer::singleShot(0, this, &Controller::sendPendingResize);
}

void Controller::sendPendingResize()
{
    m_resizeQueued = false;
    if (m_pendingResize.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_RESIZE_DISPLAY);
    controlMsg->setResizeDisplayData(m_pendingResize);
    m_pendingResize = QSize();
    postControlMsg(controlMsg);
}

void Controller::requestDeviceClipboard()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::getDeviceClipboard(bool cut)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    ControlMsg::GetClipboardCopyKey copyKey = cut ? ControlMsg::GCCK_CUT : ControlMsg::GCCK_COPY;
    controlMsg->setGetClipboardMsgData(copyKey);
    postControlMsg(controlMsg);
}

void Controller::setDeviceClipboard(bool pause)
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    controlMsg->setSetClipboardMsgData(text, pause);
    postControlMsg(controlMsg);
}

void Controller::clipboardPaste()
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    postTextInput(text);
}

void Controller::postTextInput(QString &text)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TEXT);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectTextMsgData(text);
    postControlMsg(controlMsg);
}

void Controller::setDisplayPower(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_DISPLAY_POWER);
    if (!controlMsg) {
        return;
    }
    controlMsg->setDisplayPowerData(on);
    postControlMsg(controlMsg);
}

void Controller::setCameraTorch(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_CAMERA_SET_TORCH);
    controlMsg->setCameraTorchData(on);
    postControlMsg(controlMsg);
}

void Controller::cameraZoomIn()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_IN));
}

void Controller::cameraZoomOut()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_OUT));
}

void Controller::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (deviceCancelPending()) { return; }
    if (m_inputConvert) {
        m_inputConvert->mouseEvent(from, frameSize, showSize);
    }
}

void Controller::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (deviceCancelPending()) { return; }
    if (m_inputConvert) {
        m_inputConvert->wheelEvent(from, frameSize, showSize);
    }
}

void Controller::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
#ifdef Q_OS_WIN
    if (from && from->timestamp()) {
        const quint32 delay = static_cast<quint32>(GetTickCount()) - static_cast<quint32>(from->timestamp());
        m_lastHostEventMs = delay < 30000 ? static_cast<int>(delay) : -1;
    }
#endif
    if (deviceCancelPending()) { return; }
    if (m_inputConvert) {
        m_inputConvert->keyEvent(from, frameSize, showSize);
    }
}

bool Controller::event(QEvent *event)
{
    if (event && static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        ControlMsg *controlMsg = dynamic_cast<ControlMsg *>(event);
        if (controlMsg) {
            const qint64 queuedAt = m_postedAt.take(controlMsg);
            m_lastQueueUs = queuedAt ? (m_inputClock.nsecsElapsed() - queuedAt) / 1000 : 0;
            const qint64 start = m_inputClock.nsecsElapsed();
            if (!sendControl(controlMsg->serializeData())) { ++m_writeErrors; }
            m_lastWriteUs = (m_inputClock.nsecsElapsed() - start) / 1000;
            ++m_controlPackets;
        }
        return true;
    }
    return QObject::event(event);
}

bool Controller::sendControl(const QByteArray &buffer)
{
    if (buffer.isEmpty()) {
        return false;
    }
    qint32 len = 0;
    if (m_sendData) {
        len = static_cast<qint32>(m_sendData(buffer));
    }
    if (len != buffer.length()) { qWarning() << "input control write failed expected=" << buffer.length() << "accepted=" << len; }
    return len == buffer.length();
}

void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    ControlMsg *controlEventDown = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventDown) {
        return;
    }
    controlEventDown->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventDown);

    ControlMsg *controlEventUp = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventUp) {
        return;
    }
    controlEventUp->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_UP, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventUp);
}

QVariantMap Controller::inputDiagnostics() const
{
    QVariantMap data = m_inputConvert ? m_inputConvert->inputDiagnostics() : QVariantMap();
    data.insert("pending", deviceCancelPending());
    data.insert("queueMs", m_lastQueueUs / 1000.0);
    data.insert("writeMs", m_lastWriteUs / 1000.0);
    data.insert("hostMs", m_lastHostEventMs);
    data.insert("packets", QVariant::fromValue(m_controlPackets));
    data.insert("errors", m_writeErrors);
    data.insert("cancelMs", m_lastCancelMs);
    return data;
}
