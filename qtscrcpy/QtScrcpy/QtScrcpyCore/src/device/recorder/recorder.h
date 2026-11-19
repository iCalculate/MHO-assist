#ifndef RECORDER_H
#define RECORDER_H
#include <QMutex>
#include <QQueue>
#include <QSize>
#include <QString>
#include <QThread>
#include <QWaitCondition>

extern "C"
{
#include "libavformat/avformat.h"
}

class RecordingTimeline;
class Recorder : public QThread
{
    Q_OBJECT
public:
    enum RecorderFormat
    {
        RECORDER_FORMAT_NULL = 0,
        RECORDER_FORMAT_MP4,
        RECORDER_FORMAT_MKV,
    };

    Recorder(const QString &fileName, QObject *parent = Q_NULLPTR);
    virtual ~Recorder();

    void setFrameSize(const QSize &declaredFrameSize);
    const QString &fileName() const { return m_fileName; }
    void setFormat(Recorder::RecorderFormat format);
    bool open();
    bool close();
    void setTimeline(RecordingTimeline *timeline) { m_timeline = timeline; }
    bool startRecorder();
    void stopRecorder();
    bool push(const AVPacket *packet, qint64 arrivalUs = 0);

private:
    struct QueuedPacket {
        AVPacket *packet;
        qint64 arrivalUs;
        qint64 frameIndex;
    };
    bool write(const QueuedPacket &item);
    const AVOutputFormat *findMuxer(const char *name);
    bool recorderWriteHeader(const AVPacket *packet);
    void recorderRescalePacket(AVPacket *packet);
    QString recorderGetFormatName(Recorder::RecorderFormat format);
    RecorderFormat guessRecordFormat(const QString &fileName);

private:
    AVPacket *packetNew(const AVPacket *packet);
    void packetDelete(AVPacket *packet);
    void queueClear();

protected:
    void run();

private:
    QString m_fileName = "";
    AVFormatContext *m_formatCtx = Q_NULLPTR;
    QSize m_declaredFrameSize;
    bool m_headerWritten = false;
    RecorderFormat m_format = RECORDER_FORMAT_NULL;
    QMutex m_mutex;
    QWaitCondition m_recvDataCond;
    bool m_stopped = false; // set on recorder_stop() by the stream reader
    bool m_failed = false;  // set on packet write failure
    QQueue<QueuedPacket> m_queue;
    RecordingTimeline *m_timeline = nullptr;
    qint64 m_ptsOrigin = AV_NOPTS_VALUE;
};

#endif // RECORDER_H
