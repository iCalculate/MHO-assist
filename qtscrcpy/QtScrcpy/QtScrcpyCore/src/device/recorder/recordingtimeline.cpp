#include "recordingtimeline.h"
#include "../../../../uibase/inputhistory.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QKeyEvent>
#include <QJsonDocument>

#include <algorithm>

namespace {
constexpr std::size_t recentFrameLimit = 256;

QJsonValue number(qint64 value)
{
    return static_cast<double>(value);
}

void addPointerPosition(QJsonObject &row, const QPointF &pos, const QSize &frame, const QSize &view)
{
    row.insert("view_x", pos.x());
    row.insert("view_y", pos.y());
    row.insert("frame_width", frame.width());
    row.insert("frame_height", frame.height());
    if (view.width() > 0 && view.height() > 0) {
        row.insert("x_norm", pos.x() / view.width());
        row.insert("y_norm", pos.y() / view.height());
    }
}
}

RecordingTimeline::RecordingTimeline(const QString &videoPath)
    : m_videoPath(videoPath), m_file(videoPath + ".inputs.jsonl")
{
    m_clock.start();
}

bool RecordingTimeline::open(const QSize &size)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qWarning() << "Could not record input timeline:" << m_file.fileName() << m_file.errorString();
        return false;
    }
    writeRow({{"type", "session"}, {"schema_version", 1},
              {"video", QFileInfo(m_videoPath).fileName()},
              {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
              {"frame_width", size.width()}, {"frame_height", size.height()}, {"host_us", 0}});
    return !m_writeFailed;
}

qint64 RecordingTimeline::nowUs() const
{
    return m_clock.nsecsElapsed() / 1000;
}

qint64 RecordingTimeline::receiveVideoFrame(qint64 sourcePtsUs, qint64 arrivalUs)
{
    QMutexLocker lock(&m_mutex);
    if (m_videoStartUs < 0) {
        m_videoStartUs = arrivalUs;
        m_sourceStartUs = sourcePtsUs;
        writeRow({{"type", "video_start"}, {"host_us", number(arrivalUs)},
                  {"source_pts_us", number(sourcePtsUs)}, {"video_us", 0}});
    }
    const qint64 index = m_framesReceived++;
    if (m_recentFrames.size() == recentFrameLimit) m_recentFrames.pop_front();
    m_recentFrames.push_back({index, sourcePtsUs - m_sourceStartUs, arrivalUs});
    return index;
}

void RecordingTimeline::videoFrame(qint64 index, qint64 captureUs, qint64 muxUs, qint64 durationUs,
                                   qint64 arrivalUs, bool keyframe)
{
    QMutexLocker lock(&m_mutex);
    ++m_framesWritten;
    writeRow({{"type", "frame"}, {"frame_index", number(index)}, {"duration_us", number(durationUs)},
              {"host_us", number(arrivalUs)}, {"capture_us", number(captureUs)},
              {"video_us", number(muxUs)}, {"keyframe", keyframe}});
}

void RecordingTimeline::previewFrame(qint64 sourcePtsUs)
{
    QMutexLocker lock(&m_mutex);
    if (m_videoStartUs < 0 || sourcePtsUs < m_sourceStartUs) {
        return;
    }
    m_previewUs = sourcePtsUs - m_sourceStartUs;
    m_previewHostUs = nowUs();
    const auto frame = std::find_if(m_recentFrames.crbegin(), m_recentFrames.crend(), [this](const FrameReference &ref) {
        return ref.captureUs == m_previewUs;
    });
    m_previewFrameIndex = frame == m_recentFrames.crend() ? -1 : frame->index;
    QJsonObject row{{"type", "preview"}, {"host_us", number(m_previewHostUs)}, {"capture_us", number(m_previewUs)}};
    if (m_previewFrameIndex >= 0) row.insert("frame_index", number(m_previewFrameIndex));
    writeRow(row);
}

void RecordingTimeline::setKeymap(const QString &name, const QString &script)
{
    QJsonObject row{{"type", "keymap"}, {"name", name}};
    if (!script.isEmpty()) {
        const auto json = QJsonDocument::fromJson(script.toUtf8());
        if (json.isObject()) {
            row.insert("mapping", json.object());
        } else {
            row.insert("mapping_text", script);
        }
    }
    QMutexLocker lock(&m_mutex);
    ++m_keymapRevision;
    writeRow(row);
}

void RecordingTimeline::keyEvent(const QKeyEvent &event, bool custom, bool blocked, bool handledLocally)
{
    const QString phase = event.type() == QEvent::KeyPress ? "down" : "up";
    const QString id = event.nativeScanCode()
        ? "scan:" + QString::number(event.nativeScanCode())
        : "qt:" + QString::number(event.key());
    recordInput({{"type", "key"}, {"key", event.key()},
                 {"name", readableKey(event.key())},
                 {"scan_code", number(event.nativeScanCode())}, {"virtual_key", number(event.nativeVirtualKey())},
                 {"modifiers", int(event.modifiers())}, {"auto_repeat", event.isAutoRepeat()},
                 {"qt_timestamp_ms", number(event.timestamp())},
                 {"custom_mapping", custom}, {"blocked", blocked}, {"handled_locally", handledLocally}},
                id, phase, event.isAutoRepeat());
}

void RecordingTimeline::mouseEvent(const QMouseEvent &event, const QSize &frame, const QSize &view, bool custom, bool blocked)
{
    QString phase = "move";
    if (event.type() == QEvent::MouseButtonPress) phase = "down";
    if (event.type() == QEvent::MouseButtonRelease) phase = "up";
    if (event.type() == QEvent::MouseButtonDblClick) phase = "double_down";
    QJsonObject row{{"type", "mouse"}, {"button", int(event.button())}, {"buttons", int(event.buttons())},
                    {"modifiers", int(event.modifiers())}, {"custom_mapping", custom}, {"blocked", blocked}};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    addPointerPosition(row, event.position(), frame, view);
#else
    addPointerPosition(row, event.localPos(), frame, view);
#endif
    recordInput(row, "mouse:" + QString::number(int(event.button())), phase, false);
}

void RecordingTimeline::wheelEvent(const QWheelEvent &event, const QSize &frame, const QSize &view, bool custom, bool blocked)
{
    QJsonObject row{{"type", "wheel"}, {"angle_x", event.angleDelta().x()}, {"angle_y", event.angleDelta().y()},
                    {"pixel_x", event.pixelDelta().x()}, {"pixel_y", event.pixelDelta().y()},
                    {"buttons", int(event.buttons())}, {"modifiers", int(event.modifiers())},
                    {"scroll_phase", int(event.phase())}, {"inverted", event.inverted()},
                    {"custom_mapping", custom}, {"blocked", blocked}};
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    addPointerPosition(row, event.position(), frame, view);
#else
    addPointerPosition(row, event.posF(), frame, view);
#endif
    QMutexLocker lock(&m_mutex);
    writeRow(row);
}

void RecordingTimeline::recordInput(QJsonObject data, const QString &id, const QString &phase, bool repeat)
{
    const qint64 timeUs = nowUs();
    QMutexLocker lock(&m_mutex);
    data.insert("phase", phase);
    data.insert("host_us", number(timeUs));
    if (phase == "move") {
        writeRow(data);
        return;
    }
    data.insert("input_id", id);
    const auto held = m_held.find(id);
    if (held == m_held.end() && !repeat && (phase == "down" || phase == "double_down")) {
        data.insert("press_seq", number(m_sequence));
        m_held.insert(id, {timeUs, data});
    } else if (held != m_held.end()) {
        data.insert("press_seq", held->event.value("press_seq"));
        if (!repeat && phase == "up") {
            data.insert("down_host_us", number(held->downUs));
            data.insert("hold_us", number(timeUs - held->downUs));
            m_held.erase(held);
        }
    }
    writeRow(data);
}

void RecordingTimeline::controlEvent(QJsonObject data, qint64 queuedUs, qint64 sentUs, bool accepted, bool assistant)
{
    QMutexLocker lock(&m_mutex);
    data.insert("type", "control");
    data.insert("host_us", number(sentUs));
    if (queuedUs >= 0) data.insert("queued_host_us", number(queuedUs));
    data.insert("accepted", accepted);
    data.insert("source", assistant ? "assistant" : "controller");
    writeRow(data);
}

void RecordingTimeline::cancelHeldInputs(const QString &reason)
{
    QMutexLocker lock(&m_mutex);
    cancelHeldInputsLocked(reason, nowUs());
}

void RecordingTimeline::cancelHeldInputsLocked(const QString &reason, qint64 timeUs)
{
    for (const HeldInput &held : m_held) {
        QJsonObject data = held.event;
        data.insert("phase", "cancel");
        data.insert("reason", reason);
        data.insert("host_us", number(timeUs));
        data.insert("down_host_us", number(held.downUs));
        data.insert("observed_hold_us", number(timeUs - held.downUs));
        data.remove("qt_timestamp_ms");
        writeRow(data);
    }
    m_held.clear();
}

void RecordingTimeline::close(bool videoComplete)
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen()) return;
    cancelHeldInputsLocked("recording_end", nowUs());
    writeRow({{"type", "end"}, {"video_complete", videoComplete && m_framesWritten > 0 && m_framesWritten == m_framesReceived},
              {"frame_count", number(m_framesWritten)}, {"received_frame_count", number(m_framesReceived)}});
    if (!m_file.flush() && !m_writeFailed) {
        qWarning() << "Could not finalize input timeline:" << m_file.fileName() << m_file.errorString();
        m_writeFailed = true;
    }
    m_file.close();
    if (!m_writeFailed) qInfo() << "Saved input timeline:" << m_file.fileName();
}

void RecordingTimeline::writeRow(QJsonObject data)
{
    if (m_writeFailed) return;
    const qint64 now = nowUs();
    if (!data.contains("host_us")) data.insert("host_us", number(now));
    data.insert("seq", number(m_sequence++));
    const auto type = data.value("type").toString();
    if (type == "key" || type == "mouse" || type == "wheel" || type == "control" || type == "keymap") {
        data.insert("keymap_revision", m_keymapRevision);
        const auto hostUs = static_cast<qint64>(data.value("host_us").toDouble());
        if (m_videoStartUs >= 0) {
            data.insert("video_estimate_us", number(hostUs - m_videoStartUs));
            const auto received = std::find_if(m_recentFrames.crbegin(), m_recentFrames.crend(), [hostUs](const FrameReference &ref) {
                return ref.arrivalUs <= hostUs;
            });
            if (received != m_recentFrames.crend()) data.insert("received_frame_index", number(received->index));
        }
        if (m_previewUs >= 0 && m_previewHostUs <= hostUs) {
            data.insert("preview_capture_us", number(m_previewUs));
            data.insert("preview_age_us", number(hostUs - m_previewHostUs));
            if (m_previewFrameIndex >= 0) data.insert("preview_frame_index", number(m_previewFrameIndex));
        }
    }
    const QByteArray line = QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n';
    const bool flushDue = now - m_lastFlushUs >= 1000000;
    if (m_file.write(line) != line.size() || (flushDue && !m_file.flush())) {
        m_writeFailed = true;
        qWarning() << "Could not write input timeline:" << m_file.fileName() << m_file.errorString();
    } else if (flushDue) {
        m_lastFlushUs = now;
    }
}
