#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVariantMap>
#include <QFontMetrics>
#include <QToolButton>
#include <QScrollBar>
#include <QElapsedTimer>
#include <QVector>
#include <array>

class DiagnosticPanel final : public QWidget {
public:
    QToolButton *assistButton, *regionButton, *resetButton, *pointerButton, *keymapButton;
    explicit DiagnosticPanel(QWidget *parent=nullptr) : QWidget(parent) {
        QFont f(QStringLiteral("Microsoft YaHei UI")); f.setPixelSize(12); setFont(f);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAccessibleName(QStringLiteral("顶部输入诊断"));
        setToolTip(QStringLiteral("本机排队是控制消息在电脑端等待发送的时间。UI 延迟是界面定时检查超出的时间。\n每 250 ms 采样，保留最近 60 秒。两条延迟曲线共用毫秒刻度，FPS 与丢帧分别缩放。\n丢帧显示电脑视频缓冲区每秒跳过的帧数。"));
        assistButton=new QToolButton(this);assistButton->setText(QStringLiteral("方向助手"));assistButton->setCheckable(true);
        assistButton->setToolTip(QStringLiteral("开启后识别框内方向序列并输入；失焦或普通鼠标模式暂停。F8 开关，F9 停止。"));
        keymapButton=new QToolButton(this);keymapButton->setText(QStringLiteral("按键配置"));
        keymapButton->setToolTip(QStringLiteral("打开现有游戏按键位置编辑页面。返回投屏后按 Ctrl 恢复映射。"));
        regionButton=new QToolButton(this);regionButton->setText(QStringLiteral("调整范围"));regionButton->setCheckable(true);
        regionButton->setToolTip(QStringLiteral("拖框移动；拖右下角缩放；框外拖动重新框选。调整时暂停输入。"));
        resetButton=new QToolButton(this);resetButton->setText(QStringLiteral("自动定位"));resetButton->setToolTip(QStringLiteral("恢复记忆范围，再根据四方向按钮连续确认并自动定位。手动调整后保持手动范围。"));
        pointerButton=new QToolButton(this);pointerButton->setText(QStringLiteral("手机光标"));pointerButton->setCheckable(true);pointerButton->setChecked(true);
        for(auto button:{assistButton,keymapButton,regionButton,resetButton,pointerButton}) {
            button->setFocusPolicy(Qt::NoFocus);button->setFont(f);
            button->setStyleSheet("QToolButton{color:#a9bbc9;background:#1b2938;border:1px solid #34485b;border-radius:4px;padding:2px 6px;} QToolButton:checked{color:#7de0b2;border-color:#7de0b2;background:#204539;}");
        }
        m_historyScroll = new QScrollBar(Qt::Horizontal, this);
        m_historyScroll->setObjectName(QStringLiteral("inputHistoryScrollBar"));
        m_historyScroll->setAccessibleName(QStringLiteral("按键历史"));
        m_historyScroll->setToolTip(QStringLiteral("拖动查看按键。亮色表示按住，× 表示连续重复次数。"));
        m_historyScroll->setFocusPolicy(Qt::NoFocus);
        m_historyScroll->setRange(0, 0);
        m_historyScroll->setSingleStep(24);
        m_historyScroll->setStyleSheet(QStringLiteral(
            "QScrollBar:horizontal{background:#1b2938;border:0;border-radius:3px;margin:0;}"
            "QScrollBar::handle:horizontal{background:#60778c;min-width:19px;border-radius:3px;}"
            "QScrollBar::handle:horizontal:hover{background:#8195a8;}"
            "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
            "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:none;}"));
        m_historyScroll->hide();
        connect(m_historyScroll, &QScrollBar::valueChanged, this, [this] { update(historyRect()); });
        clock.start();
    }
    void setAssistStatus(const QString &value) {if(status!=value){status=value;update();}}
    void setSnapshot(const QVariantMap &data) {
        const QVariantList history = data.value("historyEntries").toList();
        if (history != m_data.value("historyEntries").toList()) {
            refreshHistory(history);
        }
        m_data=data;queuePeak=qMax(queuePeak,data.value("queueMs").toDouble());uiPeak=qMax(uiPeak,data.value("uiGapMs").toDouble());
        const qint64 now = clock.elapsed();
        if (now >= nextSampleMs) {
            sample(now);
            nextSampleMs = (now / SampleIntervalMs + 1) * SampleIntervalMs;
        }
        setAccessibleDescription(QStringLiteral("FPS %1，%2").arg(m_fps).arg(status));update();
    }
    void addFps(int fps) {
        if (!m_data.contains("renderedFrameCount")) {
            m_fps = fps;
            update();
        }
    }
    void sample(qint64 now) {
        double fps = m_fps;
        double skipped = m_data.value("skipped").toDouble();
        if (m_data.contains("renderedFrameCount")) {
            const quint32 renderedFrames = m_data.value("renderedFrameCount").toUInt();
            const quint32 skippedFrames = m_data.value("skippedFrameCount").toUInt();
            if (lastFrameSampleMs >= 0 && now > lastFrameSampleMs &&
                renderedFrames >= lastRenderedFrames && skippedFrames >= lastSkippedFrames) {
                const double framesPerSecond = 1000.0 / (now - lastFrameSampleMs);
                fps = (renderedFrames - lastRenderedFrames) * framesPerSecond;
                skipped = (skippedFrames - lastSkippedFrames) * framesPerSecond;
            }
            lastFrameSampleMs = now;
            lastRenderedFrames = renderedFrames;
            lastSkippedFrames = skippedFrames;
        } else {
            lastFrameSampleMs = -1;
        }
        m_fps = qRound(fps);
        m_skipped = qRound(skipped);
        samples[next] = {now, {{fps, skipped, queuePeak, uiPeak}}};
        next = (next + 1) % SampleCapacity;
        count = qMin(int(SampleCapacity), count + 1);
        m_graphDirty = true;
        queuePeak = uiPeak = 0;
    }
    int heightForWidth(int width) const override { return width < 900 ? 260 : 136; }
protected:
    void resizeEvent(QResizeEvent *) override {
        const int buttonWidth=qMin(82,qMax(1,(width()-56)/5));int x=12;
        for(auto button:{assistButton,keymapButton,regionButton,resetButton,pointerButton}) {
            button->setGeometry(x,101,buttonWidth,27);x+=buttonWidth+8;
        }
        updateHistoryScroll(m_historyScroll->value() == m_historyScroll->maximum(), m_historyScroll->value());
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setFont(font());
        const QColor bg("#121b26"),line("#283747"),text("#d6e1eb"),muted("#8195a8"),cyan("#79d8f4"),green("#7de0b2"),amber("#f0c471"),red("#ff8892");
        p.fillRect(rect(),bg);p.setPen(line);p.drawLine(0,height()-1,width(),height()-1);const QFontMetrics fm(font());
        auto label=[&](int x,int y,const QString &s,QColor color,int w){p.setPen(color);p.drawText(QRect(x,y,qMax(0,w),20),Qt::AlignVCenter|Qt::AlignLeft,fm.elidedText(s,Qt::ElideRight,qMax(0,w)));};
        const bool compact = width() < 900;
        const int rightWidth = compact ? width() : qRound(width() * .42);
        const int graphW = qMax(0, (rightWidth - 42) / 2);
        const int right = inputRightEdge();
        const auto held = m_data.value("heldKeys").toStringList();
        constexpr std::array<char, 4> keys{'W', 'A', 'S', 'D'};
        constexpr std::array<int, 4> columns{1, 0, 1, 2};
        for (int i = 0; i < int(keys.size()); ++i) {
            const QString key(QChar::fromLatin1(keys[i]));
            const bool active = held.contains(key);
            const QRect bounds(12 + 27 * columns[i], i == 0 ? 8 : 35, 23, 23);
            p.setPen(active ? cyan : line);
            p.setBrush(active ? QColor("#20465b") : QColor("#192635"));
            p.drawRoundedRect(QRectF(bounds), 3, 3);
            p.setPen(active ? cyan : muted);
            p.drawText(bounds, Qt::AlignCenter, key);
        }
        label(InputX, 8, m_data.value("mode").toString(), muted, qMin(100, right - InputX));
        const int statusX = InputX + 111;
        label(statusX, 8, status, assistButton->isChecked() ? green : muted, right - statusX);
        label(InputX, 34, QStringLiteral("触点 %1").arg(m_data.value("touches", QStringLiteral("无")).toString()),
              muted, right - InputX);
        paintHistory(p, text, cyan);

        const int gx=compact?12:width()-rightWidth+12;const int gy=compact?132:8;const int tx=gx+graphW+22;
        label(gx,gy,QStringLiteral("FPS  %1").arg(m_fps),green,graphW);
        label(gx,gy+23,QStringLiteral("丢帧  %1 /s").arg(m_skipped),amber,graphW);
        label(tx,gy,QStringLiteral("本机排队  %1 ms").arg(m_data.value("queueMs").toDouble(),0,'f',1),cyan,graphW);
        label(tx,gy+23,QStringLiteral("UI 延迟  +%1 ms").arg(m_data.value("uiGapMs").toInt()),red,graphW);
        paintGraphs(p, gx, gy + 53, graphW, {green, amber, cyan, red});
    }
private:
    void paintGraphs(QPainter &painter, int x, int y, int graphWidth, const std::array<QColor, 4> &colors) {
        if (graphWidth <= 0 || count == 0) {
            return;
        }
        constexpr int gap = 22;
        constexpr int height = 64;
        const qreal scale = devicePixelRatioF();
        const QSize pixels = QSize(2 * graphWidth + gap + 2, height + 2) * scale;
        if (m_graphDirty || m_graphPixmap.size() != pixels || m_graphPixmap.devicePixelRatio() != scale) {
            m_graphPixmap = QPixmap(pixels);
            m_graphPixmap.setDevicePixelRatio(scale);
            m_graphPixmap.fill(Qt::transparent);
            QPainter graphPainter(&m_graphPixmap);
            graphPainter.setRenderHint(QPainter::Antialiasing);
            drawGraphCurves(graphPainter, QRectF(1, 1, graphWidth, height), graphWidth + gap, colors);
            m_graphDirty = false;
        }
        painter.drawPixmap(x - 1, y - 1, m_graphPixmap);
    }

    void drawGraphCurves(QPainter &p, const QRectF &plot, int dx, const std::array<QColor, 4> &colors) const {
        const qint64 newestMs = samples[(next + SampleCapacity - 1) % SampleCapacity].timestamp;
        const qint64 oldestMs = newestMs - HistoryWindowMs;
        auto at = [&](int i) -> const Sample & { return samples[(next - count + i + SampleCapacity) % SampleCapacity]; };
        double fpsMax = 60, dropMax = 1, timeMax = 10;
        for (int i = 0; i < count; ++i) {
            if (at(i).timestamp < oldestMs) continue;
            const auto &values = at(i).values;
            fpsMax = qMax(fpsMax, values[0]);
            dropMax = qMax(dropMax, values[1]);
            timeMax = qMax(timeMax, qMax(values[2], values[3]));
        }
        const std::array<double, 4> maximums{fpsMax, dropMax, timeMax, timeMax};
        p.setBrush(Qt::NoBrush);
        for (int column = 0; column < int(maximums.size()); ++column) {
            const QRectF bounds = column < 2 ? plot : plot.translated(dx, 0);
            QPainterPath path;
            qint64 previousMs = oldestMs - HistoryWindowMs;
            for (int i = 0; i < count; ++i) {
                const auto &point = at(i);
                if (point.timestamp < oldestMs) continue;
                const QPointF position(bounds.right() - (newestMs - point.timestamp) * bounds.width() / HistoryWindowMs,
                                       bounds.bottom() - point.values[column] * bounds.height() / maximums[column]);
                if (point.timestamp - previousMs > 3 * SampleIntervalMs) path.moveTo(position);
                else path.lineTo(position);
                previousMs = point.timestamp;
            }
            p.setPen(QPen(colors[column], 1.35));
            p.drawPath(path);
        }
    }

    struct HistoryChip {
        quint64 sequence;
        QString key;
        QString repeats;
        bool held;
        int x;
        int width;

        bool isMouse() const { return key.startsWith(QLatin1String("mouse-")); }
    };
    static constexpr int InputX = 105;
    static constexpr int HistoryGap = 5;
    QScrollBar *m_historyScroll = nullptr;
    QVector<HistoryChip> m_historyChips;

    int inputRightEdge() const { return width() - (width() < 900 ? 12 : qRound(width() * .42) + 16); }

    QRect historyRect() const {
        return QRect(InputX, 63, qBound(0, inputRightEdge() - InputX, 255), 21);
    }

    void refreshHistory(const QVariantList &entries) {
        const int oldOffset = m_historyScroll->value();
        quint64 anchor = 0;
        int offsetWithinChip = 0;
        const bool followLatest = oldOffset == m_historyScroll->maximum();
        if (!followLatest) {
            for (const auto &chip : m_historyChips) {
                if (chip.x + chip.width + HistoryGap > oldOffset) {
                    anchor = chip.sequence;
                    offsetWithinChip = oldOffset - chip.x;
                    break;
                }
            }
        }

        const QFontMetrics metrics(font());
        m_historyChips.clear();
        int x = 0;
        int scrollTo = anchor ? 0 : oldOffset;
        for (const auto &entry : entries) {
            const auto item = entry.toMap();
            const int count = item.value("count").toInt();
            HistoryChip chip{item.value("sequence").toULongLong(), item.value("key").toString(),
                             count > 1 ? QStringLiteral(" ×%1").arg(count) : QString(),
                             item.value("held").toBool(), x, 0};
            chip.width = (chip.isMouse() ? 16 : metrics.horizontalAdvance(chip.key))
                         + metrics.horizontalAdvance(chip.repeats) + 16;
            if (anchor && anchor == chip.sequence) {
                scrollTo = x + qMin(offsetWithinChip, chip.width + HistoryGap - 1);
            }
            m_historyChips.append(chip);
            x += chip.width + HistoryGap;
        }
        updateHistoryScroll(followLatest, scrollTo);
    }

    void updateHistoryScroll(bool followLatest, int scrollTo) {
        const QRect strip = historyRect();
        const int contentWidth = m_historyChips.isEmpty() ? 0 : m_historyChips.last().x + m_historyChips.last().width;
        m_historyScroll->setGeometry(strip.x(), strip.bottom() + 4, strip.width(), 7);
        m_historyScroll->setPageStep(strip.width());
        m_historyScroll->setRange(0, qMax(0, contentWidth - strip.width()));
        m_historyScroll->setValue(followLatest ? m_historyScroll->maximum() : scrollTo);
        m_historyScroll->setVisible(strip.width() > 0 && m_historyScroll->maximum() > 0);
    }

    void paintHistory(QPainter &painter, const QColor &text, const QColor &activeText) const {
        const QRect strip = historyRect();
        painter.save();
        painter.setClipRect(strip);
        for (const auto &chip : m_historyChips) {
            const QRect bounds(strip.x() + chip.x - m_historyScroll->value(), strip.y(), chip.width, strip.height());
            if (!strip.intersects(bounds)) {
                continue;
            }
            const QColor color = chip.held ? activeText : text;
            painter.setPen(Qt::NoPen);
            painter.setBrush(chip.held ? QColor("#20465b") : QColor("#1b2938"));
            painter.drawRoundedRect(QRectF(bounds), 4, 4);
            QRect label = bounds.adjusted(8, 0, -8, 0);
            if (chip.isMouse()) {
                const QRectF mouse(label.x(), bounds.y() + 4, 11, 13);
                painter.setPen(color);
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(mouse, 3, 3);
                painter.drawLine(QPointF(mouse.x(), mouse.y() + 6), QPointF(mouse.right(), mouse.y() + 6));
                painter.drawLine(QPointF(mouse.center().x(), mouse.y()), QPointF(mouse.center().x(), mouse.y() + 6));
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                if (chip.key == QLatin1String("mouse-left")) {
                    painter.drawRoundedRect(mouse.adjusted(1, 1, -6, -8), 1, 1);
                } else if (chip.key == QLatin1String("mouse-right")) {
                    painter.drawRoundedRect(mouse.adjusted(6, 1, -1, -8), 1, 1);
                } else {
                    painter.drawRect(QRectF(mouse.center().x() - 1, mouse.y() + 2, 2, 5));
                }
                label.setLeft(label.left() + 16);
            }
            painter.setPen(color);
            painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, (chip.isMouse() ? QString() : chip.key) + chip.repeats);
        }
        painter.restore();
    }

    enum { SampleIntervalMs = 250, HistoryWindowMs = 60000, SampleCapacity = HistoryWindowMs / SampleIntervalMs + 1 };
    struct Sample {
        qint64 timestamp;
        std::array<double, 4> values;
    };

    QVariantMap m_data;
    QString status = QStringLiteral("已关闭");
    std::array<Sample, SampleCapacity> samples{};
    QPixmap m_graphPixmap;
    bool m_graphDirty = true;
    int next = 0, count = 0, m_fps = 0, m_skipped = 0;
    quint32 lastRenderedFrames = 0, lastSkippedFrames = 0;
    qint64 lastFrameSampleMs = -1, nextSampleMs = SampleIntervalMs;
    double queuePeak = 0, uiPeak = 0;
    QElapsedTimer clock;
};
