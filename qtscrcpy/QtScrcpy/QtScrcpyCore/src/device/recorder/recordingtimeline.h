#ifndef RECORDINGTIMELINE_H
#define RECORDINGTIMELINE_H

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QSize>

#include <deque>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

class RecordingTimeline
{
public:
    explicit RecordingTimeline(const QString &videoPath);

    bool open(const QSize &size);
    qint64 nowUs() const;
    qint64 receiveVideoFrame(qint64 sourcePtsUs, qint64 arrivalUs);
    void videoFrame(qint64 index, qint64 captureUs, qint64 muxUs, qint64 durationUs, qint64 arrivalUs, bool keyframe);
    void previewFrame(qint64 sourcePtsUs);
    void setKeymap(const QString &name, const QString &script);
    void keyEvent(const QKeyEvent &event, bool custom, bool blocked, bool handledLocally = false);
    void mouseEvent(const QMouseEvent &event, const QSize &frame, const QSize &view, bool custom, bool blocked);
    void wheelEvent(const QWheelEvent &event, const QSize &frame, const QSize &view, bool custom, bool blocked);
    void controlEvent(QJsonObject data, qint64 queuedUs, qint64 sentUs, bool accepted, bool assistant);
    void cancelHeldInputs(const QString &reason);
    void close(bool videoComplete);

private:
    struct HeldInput {
        qint64 downUs;
        QJsonObject event;
    };
    struct FrameReference {
        qint64 index;
        qint64 captureUs;
        qint64 arrivalUs;
    };

    void recordInput(QJsonObject data, const QString &id, const QString &phase, bool repeat);
    void cancelHeldInputsLocked(const QString &reason, qint64 timeUs);
    void writeRow(QJsonObject data);

    const QString m_videoPath;
    QFile m_file;
    QElapsedTimer m_clock;
    QMutex m_mutex;
    QHash<QString, HeldInput> m_held;
    std::deque<FrameReference> m_recentFrames;
    qint64 m_videoStartUs = -1;
    qint64 m_sourceStartUs = 0;
    qint64 m_previewUs = -1;
    qint64 m_previewFrameIndex = -1;
    qint64 m_previewHostUs = -1;
    qint64 m_lastFlushUs = 0;
    qint64 m_sequence = 0;
    qint64 m_framesReceived = 0;
    qint64 m_framesWritten = 0;
    int m_keymapRevision = 0;
    bool m_writeFailed = false;
};

#endif // RECORDINGTIMELINE_H
