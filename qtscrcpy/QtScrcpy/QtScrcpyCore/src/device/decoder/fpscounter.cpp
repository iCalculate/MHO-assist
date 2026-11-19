#include <QDebug>
#include <QTimerEvent>

#include "fpscounter.h"

FpsCounter::FpsCounter(QObject *parent) : QObject(parent) {}

FpsCounter::~FpsCounter() {}

void FpsCounter::start()
{
    resetCounter();
    startCounterTimer();
}

void FpsCounter::stop()
{
    stopCounterTimer();
    resetCounter();
}

bool FpsCounter::isStarted()
{
    return m_counterTimer;
}

void FpsCounter::addRenderedFrame()
{
    m_rendered.fetch_add(1, std::memory_order_relaxed);
}

void FpsCounter::addSkippedFrame()
{
    m_skipped.fetch_add(1, std::memory_order_relaxed);
}

void FpsCounter::timerEvent(QTimerEvent *event)
{
    if (event && m_counterTimer == event->timerId()) {
        const quint32 rendered = renderedFrameCount();
        const quint32 skipped = skippedFrameCount();
        const quint32 intervalRendered = rendered - m_previousRendered;
        m_curSkipped = skipped - m_previousSkipped;
        m_previousRendered = rendered;
        m_previousSkipped = skipped;
        emit updateFPS(intervalRendered);
        qInfo("video stats rendered=%u skipped=%u", intervalRendered, m_curSkipped);
    }
}

void FpsCounter::startCounterTimer()
{
    stopCounterTimer();
    m_counterTimer = startTimer(1000);
}

void FpsCounter::stopCounterTimer()
{
    if (m_counterTimer) {
        killTimer(m_counterTimer);
        m_counterTimer = 0;
    }
}

void FpsCounter::resetCounter()
{
    m_rendered = 0;
    m_skipped = 0;
    m_previousRendered = 0;
    m_previousSkipped = 0;
    m_curSkipped = 0;
}
