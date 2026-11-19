#include "fishingassistant.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegion>
#include <QStandardPaths>
#include <QVector>
#include <QWidget>
#include <QtEndian>
#include <cstring>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr int frameInterval = 40;
constexpr int responseDeadline = 385;
constexpr int startupDeadline = 6000;

void packPlane(char *&destination, const uint8_t *source, int width, int rows, int stride)
{
    for (int row = 0; row < rows; ++row) {
        std::memcpy(destination, source, width);
        source += stride;
        destination += width;
    }
}
}

class FishingOverlay final : public QWidget {
public:
    explicit FishingOverlay(QWidget *video)
        : QWidget(video->parentWidget() ? video->parentWidget() : video), m_video(video)
    {
        setObjectName(QStringLiteral("fishingRecognitionOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        QFont labelFont(QStringLiteral("Microsoft YaHei UI"));
        labelFont.setPixelSize(12);
        setFont(labelFont);
        video->installEventFilter(this);
        hide();
    }

    void setMarks(const QJsonArray &marks)
    {
        if (marks == m_marks) return;
        m_marks = marks;
        refresh();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_video && (event->type() == QEvent::Resize || event->type() == QEvent::Move
                                  || event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
            refresh();
        }
        return QWidget::eventFilter(object, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(font());
        for (const auto &mark : m_visibleMarks) {
            if (mark.dot) {
                painter.setPen(QPen(QColor(13, 24, 31, 220), 1.5));
                painter.setBrush(mark.color);
                painter.drawEllipse(mark.shape);
            } else {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(13, 24, 31, 205), 3));
                painter.drawRoundedRect(mark.shape, 3, 3);
                painter.setPen(QPen(mark.color, 1.25));
                painter.drawRoundedRect(mark.shape, 3, 3);
            }
            if (!mark.label.isEmpty()) {
                painter.fillRect(mark.labelBox, QColor(14, 26, 36, 218));
                painter.setPen(mark.color);
                painter.drawText(mark.labelBox.adjusted(6, 0, -4, 0), Qt::AlignVCenter, mark.label);
            }
        }
    }

private:
    struct Mark {
        QRectF shape;
        QRect labelBox;
        QString label;
        QColor color;
        bool dot = false;
    };
    QPointer<QWidget> m_video;
    QJsonArray m_marks;
    QVector<Mark> m_visibleMarks;

    static QColor colorFor(const QString &tone)
    {
        if (tone == QLatin1String("alert")) return QColor("#ff7977");
        if (tone == QLatin1String("valid")) return QColor("#8cedb5");
        if (tone == QLatin1String("target")) return QColor("#ffd47d");
        return QColor("#7bdcf0");
    }

    void refresh()
    {
        if (m_marks.isEmpty() || !m_video || !m_video->isVisible()) {
            hide();
            return;
        }
        setGeometry(parentWidget() == m_video ? m_video->rect() : m_video->geometry());
        QRegion mask;
        m_visibleMarks.clear();
        const QFontMetrics metrics(font());
        for (int i = 0, count = qMin<int>(m_marks.size(), 24); i < count; ++i) {
            const auto values = m_marks[i].toArray();
            if (values.size() != 6) continue;
            const double x = values[0].toDouble(-1), y = values[1].toDouble(-1);
            const double w = values[2].toDouble(-1), h = values[3].toDouble(-1);
            if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1.002 || y + h > 1.002) continue;
            Mark mark;
            mark.dot = w == 0 && h == 0;
            mark.color = colorFor(values[5].toString());
            mark.shape = QRectF(x * width(), y * height(), w * width(), h * height());
            if (mark.dot) {
                const int radius = values[5].toString() == QLatin1String("target") ? 6 : 4;
                mark.shape.adjust(-radius, -radius, radius, radius);
                mask += mark.shape.adjusted(-2, -2, 2, 2).toAlignedRect();
            } else {
                const auto outer = mark.shape.adjusted(-3, -3, 3, 3).toAlignedRect();
                const auto inner = mark.shape.adjusted(3, 3, -3, -3).toAlignedRect();
                mask += QRegion(outer).subtracted(QRegion(inner));
            }
            mark.label = metrics.elidedText(values[4].toString().left(48), Qt::ElideRight, qMax(0, width() - 14));
            if (!mark.label.isEmpty()) {
                const int labelWidth = metrics.horizontalAdvance(mark.label) + 12;
                const int labelHeight = metrics.height() + 5;
                const int labelX = qBound(0, qRound(mark.shape.left()), qMax(0, width() - labelWidth));
                const int labelY = qBound(0, qRound(mark.shape.top()) - labelHeight - 2, qMax(0, height() - labelHeight));
                mark.labelBox = QRect(labelX, labelY, labelWidth, labelHeight);
                mask += mark.labelBox;
            }
            m_visibleMarks.append(mark);
        }
        mask &= QRegion(rect());
        if (mask.isEmpty()) {
            hide();
            return;
        }
        setMask(mask);
        if (isHidden()) {
            show();
            raise();
        }
        update();
    }
};

FishingAssistant::FishingAssistant(QObject *parent, QWidget *video)
    : QObject(parent), m_overlay(video ? new FishingOverlay(video) : nullptr)
{
    m_clock.start();
    m_watchdog.setInterval(frameInterval);
    connect(&m_watchdog, &QTimer::timeout, this, &FishingAssistant::watch);
    connect(&m_worker, &QProcess::readyReadStandardOutput, this, &FishingAssistant::readReply);
    connect(&m_worker, &QProcess::readyReadStandardError, this, [this] {
        m_error = (m_error + m_worker.readAllStandardError()).right(4096);
    });
    connect(&m_worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_enabled) {
            stop(QStringLiteral("钓鱼识别器启动或通信失败"));
        }
    });
    connect(&m_worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (!m_enabled) {
            return;
        }
        stop(m_error.contains("ModuleNotFoundError")
                 ? QStringLiteral("钓鱼助手缺少依赖，请安装 opencv-python 和 numpy")
                 : QStringLiteral("钓鱼识别器已退出"));
    });
#ifdef Q_OS_WIN
    m_worker.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags &= ~CREATE_NEW_CONSOLE;
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}

FishingAssistant::~FishingAssistant()
{
    showStatus = nullptr;
    stop();
    if (m_worker.state() != QProcess::NotRunning) {
        m_worker.waitForFinished(300);
    }
    delete m_overlay.data();
}

void FishingAssistant::start(int rod, int bait)
{
    if (!canSend || !canSend()) {
        status(QStringLiteral("请先连接开启触控的横屏投屏"));
        return;
    }
    if (m_worker.state() != QProcess::NotRunning) {
        status(QStringLiteral("识别器正在退出，请再次点击启动"));
        return;
    }
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("FishingWorker/worker.py"));
#ifdef Q_OS_WIN
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
#else
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
#endif
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        status(python.isEmpty() ? QStringLiteral("钓鱼助手需要可从 PATH 启动的 Python 3.10 或更新版本")
                                : QStringLiteral("钓鱼识别器文件缺失"));
        return;
    }
    m_enabled = true;
    m_ready = m_pending = false;
    m_size = QSize();
    m_sequence = m_caught = 0;
    m_output.clear();
    m_error.clear();
    m_started = m_lastFrame = m_clock.elapsed();
    m_requested = m_started - frameInterval;
    m_watchdog.start();
    status(QStringLiteral("正在启动钓鱼识别"));
    m_worker.start(python, {QStringLiteral("-B"), QStringLiteral("-u"), script,
                           QStringLiteral("--rod"), QString::number(rod),
                           QStringLiteral("--bait"), QString::number(bait)});
}

void FishingAssistant::stop(const QString &reason)
{
    if (!m_enabled) {
        return;
    }
    m_enabled = m_ready = m_pending = false;
    m_watchdog.stop();
    releaseTouches();
    if (m_overlay) m_overlay->setMarks({});
    if (m_worker.state() != QProcess::NotRunning) {
        m_worker.kill();
    }
    status(reason);
}

void FishingAssistant::releaseTouches()
{
    for (int finger = 0; finger < static_cast<int>(m_contacts.size()); ++finger) {
        auto &contact = m_contacts[finger];
        if (contact.held) {
            if (sendTouch) {
                sendTouch(finger, qsc::AssistTouchPhase::Release, contact.position, m_size);
            }
            contact.held = false;
        }
    }
}

void FishingAssistant::frame(int width, int height, const uint8_t *y, const uint8_t *u, const uint8_t *v,
                             int strideY, int strideU, int strideV)
{
    if (!m_enabled || !m_ready) {
        return;
    }
    // Releasing the host keymap may briefly block touch while the device clears old contacts.
    if (m_size.isEmpty() && (!canSend || !canSend())) {
        return;
    }
    if (width < 2 || width > 4096 || height < 2 || height > 2400 ||
        width % 2 || height % 2 || qAbs(width * 862.0 / (height * 1920.0) - 1.0) > 0.035 ||
        strideY < width || strideU < width / 2 || strideV < width / 2 || !y || !u || !v) {
        stop(QStringLiteral("当前投屏比例与钓鱼识别布局不匹配"));
        return;
    }
    const QSize size(width, height);
    if (!m_size.isEmpty() && m_size != size) {
        stop(QStringLiteral("投屏尺寸变化，钓鱼已暂停"));
        return;
    }
    const qint64 now = m_lastFrame = m_clock.elapsed();
    if (m_pending || now - m_requested < frameInterval) {
        return;
    }
    m_size = size;
    QByteArray packet(12 + width * height * 3 / 2, Qt::Uninitialized);
    char *bytes = packet.data();
    qToBigEndian<quint32>(++m_sequence, bytes);
    qToBigEndian<quint32>(width, bytes + 4);
    qToBigEndian<quint32>(height, bytes + 8);
    bytes += 12;
    packPlane(bytes, y, width, height, strideY);
    packPlane(bytes, u, width / 2, height / 2, strideU);
    packPlane(bytes, v, width / 2, height / 2, strideV);
    m_pending = true;
    m_requested = now;
    if (m_worker.write(packet) != packet.size()) {
        stop(QStringLiteral("无法发送投屏帧，钓鱼已暂停"));
    }
}

void FishingAssistant::readReply()
{
    m_output += m_worker.readAllStandardOutput();
    int end;
    while ((end = m_output.indexOf('\n')) >= 0) {
        const auto reply = QJsonDocument::fromJson(m_output.left(end)).object();
        m_output.remove(0, end + 1);
        if (!m_enabled) {
            continue;
        }
        if (reply.value("ready").toBool()) {
            m_ready = true;
            m_lastFrame = m_clock.elapsed();
            status(QStringLiteral("等待钓鱼界面"));
            continue;
        }
        if (!m_pending || reply.value("id").toInt(-1) != m_sequence ||
            m_clock.elapsed() - m_requested > responseDeadline) {
            stop(QStringLiteral("识别帧无效或超时，钓鱼已暂停"));
            return;
        }
        m_pending = false;
        const int previousCaught = m_caught;
        m_caught = reply.value("caught").toInt();
        if (onCatch && m_caught > previousCaught) {
            const QString name = reply.value("catchName").toString();
            const auto encoded = reply.value("catchLabel").toString().toLatin1();
            onCatch(name, encoded.isEmpty() ? QImage() : QImage::fromData(QByteArray::fromBase64(encoded), "JPEG"));
        }
        const QString message = reply.value("status").toString();
        if (!reply.value("active").toBool()) {
            stop(message);
            if (reply.value("completed").toBool() && onCompleted) {
                onCompleted(m_caught);
            }
            return;
        }
        if (!canSend || !canSend()) {
            stop(QStringLiteral("手机触控不可用，钓鱼已暂停"));
            return;
        }
        if (!applyTouches(reply.value("touches").toArray())) {
            stop(QStringLiteral("无法执行钓鱼触控，已暂停"));
            return;
        }
        if (m_overlay) m_overlay->setMarks(reply.value("marks").toArray());
        status(message);
    }
    if (m_enabled && m_output.size() > 16384) {
        stop(QStringLiteral("钓鱼识别器返回了无效数据"));
    }
}

bool FishingAssistant::applyTouches(const QJsonArray &commands)
{
    using Phase = qsc::AssistTouchPhase;
    for (const auto &command : commands) {
        const auto values = command.toArray();
        const int finger = values.at(0).toInt(-1);
        if (values.size() != 4 || finger < 0 || finger >= static_cast<int>(m_contacts.size())) {
            return false;
        }
        const QString action = values.at(1).toString();
        const QPointF point(values.at(2).toDouble(-1), values.at(3).toDouble(-1));
        if (point.x() < 0 || point.x() > 1 || point.y() < 0 || point.y() > 1) {
            return false;
        }
        auto &contact = m_contacts[finger];
        Phase phase;
        if (action == QLatin1String("down") && !contact.held) {
            phase = Phase::Press;
        } else if (action == QLatin1String("move") && contact.held) {
            phase = Phase::Move;
        } else if (action == QLatin1String("up") && contact.held) {
            phase = Phase::Release;
        } else {
            return false;
        }
        if (!sendTouch || !sendTouch(finger, phase, point, m_size)) {
            return false;
        }
        contact.position = point;
        contact.held = phase != Phase::Release;
    }
    return true;
}

void FishingAssistant::watch()
{
    const qint64 now = m_clock.elapsed();
    if (m_size.isEmpty()) {
        if (now - m_started > startupDeadline) {
            stop(!m_ready ? QStringLiteral("钓鱼识别器启动超时")
                          : QStringLiteral("等待投屏画面或触控超时，钓鱼已暂停"));
        }
    } else if (!canSend || !canSend()) {
        stop(QStringLiteral("手机触控不可用，钓鱼已暂停"));
    } else if (now - m_lastFrame > 500 || (m_pending && now - m_requested > responseDeadline)) {
        stop(QStringLiteral("投屏画面或识别中断，钓鱼已暂停"));
    }
}

void FishingAssistant::status(const QString &text)
{
    if (showStatus) {
        showStatus(m_enabled, text, m_caught);
    }
}
