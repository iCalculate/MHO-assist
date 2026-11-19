
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QElapsedTimer>
#include <QHash>

#include "inputconvertbase.h"

class QTcpSocket;
class QProcess;
class Receiver;
class InputConvertBase;
class DeviceMsg;
class RecordingTimeline;
class Controller : public QObject
{
    Q_OBJECT
public:
    Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript = "", QObject *parent = Q_NULLPTR, const QString &serial = QString());
    virtual ~Controller();

    void postControlMsg(ControlMsg *controlMsg, bool assistant = false);
    void setRecordingTimeline(RecordingTimeline *timeline) { m_timeline = timeline; }
    void setCameraMode(bool cameraMode);
    void recvDeviceMsg(DeviceMsg *deviceMsg);
    void test(QRect rc);
    void postAssistTouch(int finger, AndroidMotioneventAction action, const QPoint &position, const QSize &frameSize);

    QVariantMap inputDiagnostics() const;
    bool cancelDeviceTouches();
    bool deviceCancelPending() const { return !m_cancelProcess.isNull(); }
    void updateScript(QString gameScript = "");
    bool isCurrentCustomKeymap();
    void releaseCustomKeymap();
    QPointF mappedTapPosition(int key) const;

    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();
    void copy();
    void cut();
    void expandNotificationPanel();
    void expandSettingsPanel();
    void collapsePanel();
    void rotateDevice();
    void startApp(const QString &name);
    void scanFile(const QString &path);
    void resizeDisplay(const QSize &size);
    void setDisplayPower(bool on);
    void setCameraTorch(bool on);
    void cameraZoomIn();
    void cameraZoomOut();

    // for input convert
    void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

    // turn the screen on if it was off, press BACK otherwise
    // If the screen is off, it is turned on only on down
    void postBackOrScreenOn(bool down);
    void requestDeviceClipboard();
    void getDeviceClipboard(bool cut = false);
    void setDeviceClipboard(bool pause = true);
    void clipboardPaste();
    void postTextInput(QString &text);

signals:
    void grabCursor(bool grab);
    void deviceTouchCancelFinished(bool ok);

protected:
    bool event(QEvent *event);

private:
    bool sendControl(const QByteArray &buffer);
    void postKeyCodeClick(AndroidKeycode keycode);
    void sendPendingResize();

private:
    QPointer<Receiver> m_receiver;
    QPointer<InputConvertBase> m_inputConvert;
    std::function<qint64(const QByteArray&)> m_sendData = Q_NULLPTR;
    QSize m_pendingResize;
    bool m_resizeQueued = false;
    bool m_cameraMode = false;
    QString m_deviceSerial;
    QPointer<QProcess> m_cancelProcess;
    bool m_shuttingDown = false;
    QElapsedTimer m_inputClock;
    struct PostedControl {
        qint64 inputClockNs = 0;
        qint64 recordingUs = -1;
        bool assistant = false;
    };
    RecordingTimeline *m_timeline = nullptr;
    QHash<const ControlMsg *, PostedControl> m_postedAt;
    qint64 m_lastQueueUs = 0;
    qint64 m_lastWriteUs = 0;
    quint64 m_controlPackets = 0;
    int m_writeErrors = 0;
    int m_lastHostEventMs = -1;
    int m_lastCancelMs = 0;
};

#endif // CONTROLLER_H
