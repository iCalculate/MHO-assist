#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// A bounded producer queue: input/render callbacks never open or flush files.
class DiagnosticLog {
public:
    ~DiagnosticLog() { stop(); }
    void start(const QString &folder) {
        m_thread = std::thread([this, folder] {
            QDir().mkpath(folder);
            QFile file(folder + "/input-video.log");
            file.open(QIODevice::WriteOnly | QIODevice::Append);
            for (;;) {
                std::deque<Entry> batch;
                bool stopping;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_ready.wait_for(lock,std::chrono::milliseconds(250),[this] { return m_stop; });
                    batch.swap(m_queue); stopping=m_stop;
                }
                QByteArray bytes;
                for (const auto &entry:batch) {
                    bytes += QDateTime::fromMSecsSinceEpoch(entry.time).toString(Qt::ISODateWithMs).toUtf8();
                    bytes += ' '; bytes += entry.text.toUtf8(); bytes += '\n';
                }
                if (!bytes.isEmpty()) {
                    file.write(bytes); file.flush();
                    if (file.size()>5*1024*1024) {
                        file.close(); QFile::remove(file.fileName()+".1");
                        QFile::rename(file.fileName(),file.fileName()+".1");
                        file.open(QIODevice::WriteOnly | QIODevice::Append);
                    }
                }
                if(stopping) break;
            }
        });
    }
    void append(const QString &msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_queue.size()>=4096) m_queue.pop_front();
        m_queue.push_back({QDateTime::currentMSecsSinceEpoch(),msg});
    }
    void stop() {
        { std::lock_guard<std::mutex> lock(m_mutex); m_stop=true; }
        m_ready.notify_one();
        if(m_thread.joinable()) m_thread.join();
    }
private:
    struct Entry { qint64 time; QString text; };
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<Entry> m_queue;
    std::thread m_thread;
    bool m_stop=false;
};
