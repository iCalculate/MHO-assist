#ifndef FPSCOUNTER_H
#define FPSCOUNTER_H
#include <QObject>
#include <atomic>

class FpsCounter : public QObject
{
    Q_OBJECT
public:
    FpsCounter(QObject *parent = Q_NULLPTR);
    virtual ~FpsCounter();

    void start();
    void stop();
    bool isStarted();
    quint32 skippedFrames() const { return m_curSkipped; }
    quint32 renderedFrameCount() const { return m_rendered.load(std::memory_order_relaxed); }
    quint32 skippedFrameCount() const { return m_skipped.load(std::memory_order_relaxed); }
    void addRenderedFrame();
    void addSkippedFrame();

signals:
    void updateFPS(quint32 fps);

protected:
    virtual void timerEvent(QTimerEvent *event);

private:
    void startCounterTimer();
    void stopCounterTimer();
    void resetCounter();

private:
    qint32 m_counterTimer = 0;
    quint32 m_previousRendered = 0;
    quint32 m_previousSkipped = 0;
    quint32 m_curSkipped = 0;

    std::atomic<quint32> m_rendered {0};
    std::atomic<quint32> m_skipped {0};
};

#endif // FPSCOUNTER_H
