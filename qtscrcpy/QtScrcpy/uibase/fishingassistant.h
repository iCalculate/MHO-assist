#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <array>
#include <functional>

#include "../QtScrcpyCore/include/QtScrcpyCore.h"

class QJsonArray;
class QImage;
class QWidget;
class FishingOverlay;

class FishingAssistant final : public QObject {
public:
    explicit FishingAssistant(QObject *parent, QWidget *video = nullptr);
    ~FishingAssistant() override;

    std::function<bool()> canSend;
    std::function<bool(int, qsc::AssistTouchPhase, const QPointF &, const QSize &)> sendTouch;
    std::function<void(bool, const QString &, int)> showStatus;
    std::function<void(const QString &, const QImage &)> onCatch;
    std::function<void(int)> onCompleted;

    bool isRunning() const { return m_enabled; }
    void start(int rod, int bait);
    void stop(const QString &reason = QStringLiteral("已暂停"));
    void frame(int width, int height, const uint8_t *y, const uint8_t *u, const uint8_t *v,
               int strideY, int strideU, int strideV);

private:
    void releaseTouches();
    void readReply();
    bool applyTouches(const QJsonArray &commands);
    void watch();
    void status(const QString &text);

    struct Contact {
        bool held = false;
        QPointF position;
    };
    std::array<Contact, 2> m_contacts;
    QPointer<FishingOverlay> m_overlay;
    QProcess m_worker;
    QTimer m_watchdog;
    QElapsedTimer m_clock;
    QByteArray m_output;
    QByteArray m_error;
    QSize m_size;
    int m_sequence = 0;
    int m_caught = 0;
    qint64 m_started = 0;
    qint64 m_lastFrame = 0;
    qint64 m_requested = 0;
    bool m_enabled = false;
    bool m_ready = false;
    bool m_pending = false;
};
