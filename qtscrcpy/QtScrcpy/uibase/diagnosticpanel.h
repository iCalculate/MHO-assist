#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVariantMap>
#include <QFontMetrics>
#include <QToolButton>
#include <QCheckBox>
#include <QScrollBar>
#include <QElapsedTimer>
#include <QVector>
#include <QActionGroup>
#include <QMenu>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTime>
#include <array>
#include <functional>
#include "phonebattery.h"

class DiagnosticToolButton final : public QToolButton {
public:
    DiagnosticToolButton(const QString &label, const QString &shortLabel, QChar glyph, QChar fallback,
                         const QFont &iconFont, QWidget *parent)
        : QToolButton(parent), m_shortLabel(shortLabel),
          m_glyph(iconFont.family().contains(QStringLiteral("Awesome"), Qt::CaseInsensitive) ? glyph : fallback),
          m_iconFont(iconFont)
    {
        setText(label);
        setAccessibleName(label);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        const QRect content = style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButton, this);
        option.text.clear();
        option.icon = QIcon();
        QStylePainter painter(this);
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        if (isEnabled()) {
            painter.setPen(isChecked() ? QColor("#8debc5") : QColor("#c6d7e4"));
        } else {
            painter.setPen(QColor("#82909c"));
        }
        painter.setFont(font());
        const auto label = fontMetrics().horizontalAdvance(text()) + 8 <= content.width() ? text() : m_shortLabel;
        painter.drawText(QRect(content.x(), 3, content.width(), 15), Qt::AlignCenter, label);
        QFont glyphFont = m_iconFont.family().contains(QStringLiteral("Awesome"), Qt::CaseInsensitive) ? m_iconFont : font();
        glyphFont.setPixelSize(13);
        painter.setFont(glyphFont);
        painter.drawText(QRect(content.x(), 19, content.width(), 15), Qt::AlignCenter, QString(m_glyph));
    }

private:
    QString m_shortLabel;
    QChar m_glyph;
    QFont m_iconFont;
};

class DiagnosticPanel final : public QWidget {
public:
    QToolButton *assistButton, *regionButton, *resetButton, *pointerButton, *keymapButton, *fishingButton, *huntingButton;
    QCheckBox *continuousButton;
    QAction *fishingHistoryAction;
    std::function<void(int, int)> graphSettingsChanged;
    explicit DiagnosticPanel(QWidget *parent=nullptr, const QFont &iconFont = QFont())
        : QWidget(parent), m_iconFont(iconFont) {
        QFont f(QStringLiteral("Microsoft YaHei UI")); f.setPixelSize(12); setFont(f);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAccessibleName(QStringLiteral("顶部输入诊断"));
        assistButton = new DiagnosticToolButton(QStringLiteral("方向助手"), QStringLiteral("方向"), QChar(0xf047), QChar(0x2194), iconFont, this);
        assistButton->setCheckable(true);
        assistButton->setToolTip(QStringLiteral("投屏最小化后继续运行，电脑鼠标可照常使用。投屏窗口内 F8 开关，F9 停止。"));
        keymapButton = new DiagnosticToolButton(QStringLiteral("按键配置"), QStringLiteral("键位"), QChar(0xf11c), QChar(0x2328), iconFont, this);
        keymapButton->setToolTip(QStringLiteral("打开现有游戏按键位置编辑页面。返回投屏后按 Ctrl 恢复映射。"));
        regionButton = new DiagnosticToolButton(QStringLiteral("调整范围"), QStringLiteral("范围"), QChar(0xf065), QChar(0x25a1), iconFont, this);
        regionButton->setCheckable(true);
        regionButton->setToolTip(QStringLiteral("拖框移动；拖右下角缩放；框外拖动重新框选。调整时暂停输入。"));
        resetButton = new DiagnosticToolButton(QStringLiteral("自动定位"), QStringLiteral("定位"), QChar(0xf05b), QChar(0x2295), iconFont, this);
        resetButton->setToolTip(QStringLiteral("恢复记忆范围，再根据四方向按钮连续确认并自动定位。手动调整后保持手动范围。"));
        pointerButton = new DiagnosticToolButton(QStringLiteral("手机光标"), QStringLiteral("光标"), QChar(0xf245), QChar(0x2196), iconFont, this);
        pointerButton->setCheckable(true);
        pointerButton->setChecked(true);
        fishingButton = new DiagnosticToolButton(QStringLiteral("钓鱼助手"), QStringLiteral("钓鱼"), QChar(0xf13d), QChar(0x2693), iconFont, this);
        fishingButton->setObjectName(QStringLiteral("fishingAssistButton"));
        fishingButton->setCheckable(true);
        fishingButton->setToolTip(QStringLiteral("点击启停，支持最小化运行。点箭头选渔具，Esc 暂停"));
        auto *fishingMenu = new QMenu(fishingButton);
        addFishingChoices(fishingMenu->addMenu(QStringLiteral("鱼竿")), 3, m_rod);
        addFishingChoices(fishingMenu->addMenu(QStringLiteral("鱼饵")), 5, m_bait);
        fishingMenu->addSeparator();
        fishingHistoryAction = fishingMenu->addAction(QStringLiteral("渔获记录"));
        fishingButton->setMenu(fishingMenu);
        fishingButton->setPopupMode(QToolButton::MenuButtonPopup);
        huntingButton = new DiagnosticToolButton(QStringLiteral("狩猎助手"), QStringLiteral("狩猎"),
            QChar(0xf05b), QChar(0x2295), iconFont, this);
        huntingButton->setObjectName(QStringLiteral("huntingAssistButton"));
        huntingButton->setEnabled(false);
        huntingButton->setToolTip(QStringLiteral("狩猎助手暂未开放"));
        huntingButton->setAccessibleDescription(huntingButton->toolTip());
        auto *huntingMenu = new QMenu(huntingButton);
        huntingMenu->addAction(QStringLiteral("连续狩猎大贼龙"))->setEnabled(false);
        huntingMenu->addAction(QStringLiteral("观察画面（不发触控）"))->setEnabled(false);
        huntingButton->setMenu(huntingMenu);
        huntingButton->setPopupMode(QToolButton::MenuButtonPopup);
        continuousButton = new QCheckBox(QStringLiteral("连续运行"), this);
        continuousButton->setObjectName(QStringLiteral("directionContinuousCheckBox"));
        continuousButton->setFocusPolicy(Qt::NoFocus);
        continuousButton->setToolTip(QStringLiteral("勾选后，自动退出结算并匹配下一轮。取消勾选后，完成当前轮并停在结算。"));
        continuousButton->setStyleSheet(QStringLiteral("QCheckBox{color:#a9bbc9;} QCheckBox:checked{color:#7de0b2;}"));
        for(auto button:{assistButton,keymapButton,regionButton,resetButton,pointerButton,fishingButton,huntingButton}) {
            button->setFocusPolicy(Qt::NoFocus);button->setFont(f);
            button->setStyleSheet(QStringLiteral(
                "QToolButton{color:#c6d7e4;background:#1b2938;border:1px solid #34485b;border-radius:6px;}"
                "QToolButton:enabled:hover{background:#263a4b;border-color:#527388;}"
                "QToolButton:checked{color:#8debc5;border-color:#5baf91;background:#204539;}"
                "QToolButton:disabled{background:#1d252d;border-color:#35424d;}"));
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
        m_battery = new QLabel(this);
        m_battery->setObjectName(QStringLiteral("phoneBatteryLabel"));
        m_batteryIcon = new QLabel(this);
        m_batteryIcon->setObjectName(QStringLiteral("phoneBatteryIcon"));
        QFont batteryFont = m_iconFont.family().contains(QStringLiteral("Awesome"), Qt::CaseInsensitive) ? m_iconFont : f;
        batteryFont.setPixelSize(15);
        m_batteryIcon->setFont(batteryFont);
        m_batteryDetails = new QLabel(this);
        m_batteryDetails->setObjectName(QStringLiteral("phoneBatteryDetails"));
        m_batteryDetails->setStyleSheet(QStringLiteral("color:#9aafc4"));
        setBattery({});
        m_eventTitle = new QLabel(QStringLiteral("识别记录"), this);
        m_eventTitle->setStyleSheet(QStringLiteral("color:#a9bbc9"));
        m_events = new QPlainTextEdit(this);
        m_events->setObjectName(QStringLiteral("diagnosticEvents"));
        m_events->setAccessibleName(QStringLiteral("识别记录"));
        m_events->setReadOnly(true);
        m_events->setUndoRedoEnabled(false);
        m_events->setMaximumBlockCount(120);
        m_events->setFocusPolicy(Qt::NoFocus);
        m_events->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:#192634;color:#b9cbd8;border:1px solid #283c4d;border-radius:4px;padding:2px 4px;}"
            "QScrollBar:vertical{background:#192634;width:7px;margin:0;}"
            "QScrollBar::handle:vertical{background:#516b80;min-height:13px;border-radius:3px;}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}"));
        clock.start();
        initGraphSettings();
    }
    void appendEvent(const QString &message)
    {
        auto *scroll = m_events->verticalScrollBar();
        const int position = scroll->value();
        const bool following = position == scroll->maximum();
        m_events->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QLatin1Char(' ') + message);
        scroll->setValue(following ? scroll->maximum() : position);
    }
    void setBattery(const PhoneBattery &battery)
    {
        QString label = battery.percent < 0 ? QStringLiteral("电量不可用")
                                            : QStringLiteral("电量 %1%").arg(battery.percent);
        QString activity;
        switch (battery.state) {
        case PhoneBattery::Charging: activity = QStringLiteral("充电"); break;
        case PhoneBattery::Discharging: activity = QStringLiteral("放电"); break;
        case PhoneBattery::NotCharging: activity = QStringLiteral("未充电"); break;
        case PhoneBattery::Full: activity = QStringLiteral("已充满"); break;
        case PhoneBattery::Unknown: break;
        }
        if (!activity.isEmpty()) label += QStringLiteral(" · ") + activity;
        if (battery.watts >= 0.0) label += QStringLiteral(" %1 W").arg(battery.watts, 0, 'f', 1);
        m_battery->setText(label);
        const QString color = battery.percent < 0 ? QStringLiteral("#9aafc4")
            : battery.percent <= 20 ? QStringLiteral("#ff8e97") : QStringLiteral("#8ee0c3");
        for (auto *widget : {m_batteryIcon, m_battery}) widget->setStyleSheet(QStringLiteral("color:%1").arg(color));
        const int level = battery.percent < 0 ? 4 : qBound(0, (100 - battery.percent) / 25, 4);
        const auto family = m_batteryIcon->font().family();
        m_batteryIcon->setText(family.contains(QStringLiteral("Awesome"), Qt::CaseInsensitive)
            ? QString(QChar(0xf240 + level)) : QString(QChar(0x25a3)));

        QStringList details;
        if (qIsFinite(battery.temperatureC)) details.append(QStringLiteral("%1 °C").arg(battery.temperatureC, 0, 'f', 1));
        if (battery.voltage >= 0) details.append(QStringLiteral("%1 V").arg(battery.voltage, 0, 'f', 2));
        if (battery.currentAmps >= 0) details.append(QStringLiteral("%1 A").arg(battery.currentAmps, 0, 'f', 2));
        for (const auto &source : battery.powerSources) {
            if (source == QLatin1String("AC")) details.append(QStringLiteral("电源适配器"));
            else if (source == QLatin1String("USB")) details.append(QStringLiteral("USB 供电"));
            else if (source == QLatin1String("Wireless")) details.append(QStringLiteral("无线供电"));
            else if (source == QLatin1String("Dock")) details.append(QStringLiteral("底座供电"));
        }
        m_batteryDetailText = details.join(QStringLiteral(" · "));
        updateBatteryDetails();
        QString tooltip = label;
        if (!m_batteryDetailText.isEmpty()) tooltip += QLatin1Char('\n') + m_batteryDetailText;
        tooltip += QStringLiteral("\n电池温度、电压和电流由手机提供。瓦数为电池侧估算值。");
        for (auto *widget : {m_batteryIcon, m_battery, m_batteryDetails}) widget->setToolTip(tooltip);
    }
    void setAssistStatus(const QString &value)
    {
        if (status == value) return;
        status = value;
        if (value.startsWith(QStringLiteral("方向 "))) appendEvent(QStringLiteral("方向 · %1").arg(value.mid(3)));
        if (value.startsWith(QStringLiteral("连续运行 · "))) appendEvent(QStringLiteral("界面 · %1").arg(value.section(QStringLiteral(" · "), 1)));
        update();
    }
    int fishingRod() const { return m_rod; }
    int fishingBait() const { return m_bait; }
    void setFishingStatus(const QString &text, int caught)
    {
        const QString value = QStringLiteral("%1 · %2 条").arg(text).arg(caught);
        if (m_fishingStatus == value) {
            return;
        }
        m_fishingStatus = value;
        if (!fishingButton->isChecked()) {
            m_fishingRecognition.clear();
            m_pendingRecognition.clear();
            m_lastRecognitionMs = -1600;
        }
        fishingButton->setToolTip(value + QStringLiteral("\n点击启停，支持最小化运行。点箭头选渔具，Esc 暂停"));
        update();
    }
    void setFishingRecognition(const QString &text)
    {
        const auto now = clock.elapsed();
        if (m_pendingRecognition != text) {
            m_pendingRecognition = text;
            m_pendingRecognitionMs = now;
        } else if (m_fishingRecognition != text && now - m_pendingRecognitionMs >= 280 &&
                   (text.isEmpty() || now - m_lastRecognitionMs >= 1600)) {
            m_fishingRecognition = text;
            if (!text.isEmpty()) {
                appendEvent(QStringLiteral("钓鱼 · %1").arg(text));
                m_lastRecognitionMs = now;
            }
        }
    }
    void setSnapshot(const QVariantMap &data) {
        const QVariantList history = data.value("historyEntries").toList();
        if (history != m_data.value("historyEntries").toList()) {
            refreshHistory(history);
        }
        m_data=data;queuePeak=qMax(queuePeak,data.value("queueMs").toDouble());uiPeak=qMax(uiPeak,data.value("uiGapMs").toDouble());
        const qint64 now = clock.elapsed();
        if (now >= nextSampleMs) {
            sample(now);
            nextSampleMs += (1 + (now - nextSampleMs) / m_sampleIntervalMs) * m_sampleIntervalMs;
        }
        setAccessibleDescription(QStringLiteral("FPS %1，%2").arg(m_fps).arg(status));update();
    }
    void addFps(int fps) {
        if (!m_data.contains("renderedFrameCount")) {
            m_fps = fps;
            update();
        }
    }
    void setGraphSettings(int intervalMs, int windowSeconds)
    {
        const int interval = m_intervals.contains(intervalMs) ? intervalMs : 250;
        const int seconds = m_windows.contains(windowSeconds) ? windowSeconds : 60;
        if (interval != m_sampleIntervalMs) nextSampleMs = clock.elapsed() + interval;
        m_sampleIntervalMs = interval;
        m_historyWindowMs = seconds * 1000;
        for (auto *action : m_intervalMenu->actions()) action->setChecked(action->data().toInt() == interval);
        for (auto *action : m_windowMenu->actions()) action->setChecked(action->data().toInt() == seconds);
        m_chartSettings->setToolTip(QStringLiteral(
            "点击设置曲线 · 每 %1 ms 采样 · 最近 %2 秒\nFPS 图的参考线为 30 和 60 帧。两条延迟曲线共用毫秒刻度。")
            .arg(interval).arg(seconds));
        m_graphDirty = true;
        update();
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
        samples[next] = {now, m_sampleIntervalMs, {{fps, skipped, queuePeak, uiPeak}}};
        next = (next + 1) % SampleCapacity;
        count = qMin(int(SampleCapacity), count + 1);
        m_graphDirty = true;
        queuePeak = uiPeak = 0;
    }
    int heightForWidth(int width) const override { return width < WideLayout ? 260 : 136; }
protected:
    void resizeEvent(QResizeEvent *) override {
        const int available = inputRightEdge();
        const bool compactControls = available < 450;
        const int checkWidth = compactControls ? 50 : 76;
        const int buttonWidth = qBound(1, (available - 12 - checkWidth - 42) / 7, 82);
        continuousButton->setText(compactControls ? QStringLiteral("连续") : QStringLiteral("连续运行"));
        assistButton->setGeometry(12, 94, buttonWidth, 38);
        continuousButton->setGeometry(18 + buttonWidth, 95, checkWidth, 36);
        int x = 24 + buttonWidth + checkWidth;
        for (auto button : {keymapButton, regionButton, resetButton, pointerButton, fishingButton, huntingButton}) {
            button->setGeometry(x, 94, buttonWidth, 38);
            x += buttonWidth + 6;
        }
        const int batteryX = batteryLeft();
        m_batteryIcon->setGeometry(batteryX, 34, 22, 20);
        m_battery->setGeometry(batteryX + 24, 34, qMax(0, available - batteryX - 24), 20);
        m_batteryDetails->setGeometry(batteryX, 63, qMax(0, available - batteryX), 21);
        updateBatteryDetails();
        const bool compact = width() < WideLayout;
        const int eventX = compact ? 12 : available + 10;
        const int eventY = compact ? 134 : 8;
        const int eventWidth = qMax(0, graphLeft() - eventX - 12);
        m_eventTitle->setGeometry(eventX, eventY, eventWidth, 20);
        m_events->setGeometry(eventX, eventY + 23, eventWidth, 94);
        m_chartSettings->setGeometry(graphLeft(), eventY, qMax(0, width() - graphLeft() - 11), 119);
        updateHistoryScroll(m_historyScroll->value() == m_historyScroll->maximum(), m_historyScroll->value());
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setFont(font());
        const QColor bg("#121b26"),line("#283747"),text("#d6e1eb"),muted("#8195a8"),cyan("#79d8f4"),green("#7de0b2"),amber("#f0c471"),red("#ff8892");
        p.fillRect(rect(),bg);p.setPen(line);p.drawLine(0,height()-1,width(),height()-1);const QFontMetrics fm(font());
        auto label=[&](int x,int y,const QString &s,QColor color,int w){p.setPen(color);p.drawText(QRect(x,y,qMax(0,w),20),Qt::AlignVCenter|Qt::AlignLeft,fm.elidedText(s,Qt::ElideRight,qMax(0,w)));};
        const bool compact = width() < WideLayout;
        const int graphW = qMax(0, (width() - graphLeft() - 34) / 2);
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
        const bool showFishing = !assistButton->isChecked() && !regionButton->isChecked() && !m_fishingStatus.isEmpty();
        const QString &currentStatus = showFishing ? m_fishingStatus : status;
        label(statusX, 8, currentStatus, assistButton->isChecked() || fishingButton->isChecked() ? green : muted,
              right - statusX);
        label(InputX, 34, QStringLiteral("触点 %1").arg(m_data.value("touches", QStringLiteral("无")).toString()),
              muted, batteryLeft() - InputX - 4);
        paintHistory(p, text, cyan);

        const int gx=graphLeft();const int gy=compact?134:8;const int tx=gx+graphW+22;
        label(gx,gy,QStringLiteral("FPS  %1").arg(m_fps),green,graphW);
        label(gx,gy+23,QStringLiteral("丢帧  %1 /s").arg(m_skipped),amber,graphW);
        label(tx,gy,QStringLiteral("本机排队  %1 ms").arg(m_data.value("queueMs").toDouble(),0,'f',1),cyan,graphW - 19);
        p.setPen(Qt::NoPen);
        p.setBrush(muted);
        for (int i = 0; i < 3; ++i) p.drawEllipse(QPointF(tx + graphW - 12 + 4 * i, gy + 10), 1.1, 1.1);
        label(tx,gy+23,QStringLiteral("UI 延迟  +%1 ms").arg(m_data.value("uiGapMs").toInt()),red,graphW);
        paintGraphs(p, gx, gy + 53, graphW, {green, amber, cyan, red});
    }
private:
    void updateBatteryDetails()
    {
        m_batteryDetails->setText(m_batteryDetails->fontMetrics().elidedText(
            m_batteryDetailText, Qt::ElideRight, m_batteryDetails->width()));
    }

    void initGraphSettings()
    {
        m_chartSettings = new QToolButton(this);
        m_chartSettings->setObjectName(QStringLiteral("diagnosticChartSettings"));
        m_chartSettings->setAccessibleName(QStringLiteral("曲线设置"));
        m_chartSettings->setFocusPolicy(Qt::NoFocus);
        m_chartSettings->setCursor(Qt::PointingHandCursor);
        m_chartSettings->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;} QToolButton::menu-indicator{width:0px;image:none;}"));
        auto *menu = new QMenu(m_chartSettings);
        m_intervalMenu = menu->addMenu(QStringLiteral("采样间隔"));
        m_windowMenu = menu->addMenu(QStringLiteral("显示范围"));
        auto addChoices = [this](QMenu *submenu, const QList<int> &values, bool interval) {
            auto *group = new QActionGroup(submenu);
            for (const int value : values) {
                auto *action = submenu->addAction(interval ? QStringLiteral("%1 ms").arg(value)
                                                           : QStringLiteral("最近 %1 秒").arg(value));
                action->setData(value);
                action->setCheckable(true);
                group->addAction(action);
            }
            connect(group, &QActionGroup::triggered, this, [this, interval](QAction *action) {
                const int choice = action->data().toInt();
                setGraphSettings(interval ? choice : m_sampleIntervalMs, interval ? m_historyWindowMs / 1000 : choice);
                if (graphSettingsChanged) graphSettingsChanged(m_sampleIntervalMs, m_historyWindowMs / 1000);
            });
        };
        addChoices(m_intervalMenu, m_intervals, true);
        addChoices(m_windowMenu, m_windows, false);
        m_chartSettings->setMenu(menu);
        m_chartSettings->setPopupMode(QToolButton::InstantPopup);
        setGraphSettings(m_sampleIntervalMs, m_historyWindowMs / 1000);
    }

    void addFishingChoices(QMenu *menu, int count, int &selection)
    {
        auto *choices = new QActionGroup(menu);
        for (int index = 0; index <= count; ++index) {
            auto *action = menu->addAction(index == 0 ? QStringLiteral("沿用游戏选择")
                                                     : QStringLiteral("第 %1 格").arg(index));
            action->setCheckable(true);
            action->setChecked(index == selection);
            action->setData(index);
            choices->addAction(action);
        }
        connect(choices, &QActionGroup::triggered, this, [&selection](QAction *action) {
            selection = action->data().toInt();
        });
    }

    void paintGraphs(QPainter &painter, int x, int y, int graphWidth, const std::array<QColor, 4> &colors) {
        if (graphWidth <= 0) {
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
        const qint64 newestMs = count ? samples[(next + SampleCapacity - 1) % SampleCapacity].timestamp : 0;
        const qint64 oldestMs = newestMs - m_historyWindowMs;
        auto at = [&](int i) -> const Sample & { return samples[(next - count + i + SampleCapacity) % SampleCapacity]; };
        double fpsMax = 66, dropMax = 1, timeMax = 10;
        for (int i = 0; i < count; ++i) {
            if (at(i).timestamp < oldestMs) continue;
            const auto &values = at(i).values;
            fpsMax = qMax(fpsMax, values[0] * 1.08);
            dropMax = qMax(dropMax, values[1]);
            timeMax = qMax(timeMax, qMax(values[2], values[3]));
        }
        const std::array<double, 4> maximums{fpsMax, dropMax, timeMax, timeMax};
        p.setFont(font());
        p.setPen(QPen(QColor("#627b8e"), 1.0, Qt::DashLine));
        for (const int reference : {30, 60}) {
            const qreal y = plot.bottom() - reference * plot.height() / fpsMax;
            const qreal labelX = plot.right() - 21;
            p.drawLine(QPointF(plot.left(), y), QPointF(labelX, y));
            p.save();
            p.setPen(QColor("#92aabc"));
            p.drawText(QRectF(labelX + 2, y - 7, 19, 14), Qt::AlignCenter, QString::number(reference));
            p.restore();
        }
        p.setBrush(Qt::NoBrush);
        for (int column = 0; column < int(maximums.size()); ++column) {
            const QRectF bounds = column < 2 ? plot.adjusted(0, 0, -21, 0) : plot.translated(dx, 0);
            QPainterPath path;
            qint64 previousMs = oldestMs - m_historyWindowMs;
            int previousIntervalMs = m_sampleIntervalMs;
            for (int i = 0; i < count; ++i) {
                const auto &point = at(i);
                if (point.timestamp < oldestMs) continue;
                const QPointF position(bounds.right() - (newestMs - point.timestamp) * bounds.width() / m_historyWindowMs,
                                       bounds.bottom() - point.values[column] * bounds.height() / maximums[column]);
                if (point.timestamp - previousMs > 3 * qMax(previousIntervalMs, point.intervalMs)) path.moveTo(position);
                else path.lineTo(position);
                previousMs = point.timestamp;
                previousIntervalMs = point.intervalMs;
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

    static constexpr int WideLayout = 1050;
    int inputRightEdge() const { return width() < WideLayout ? width() - 12 : qMin(624, qRound(width() * .51)); }
    int graphLeft() const { return qRound(width() * (width() < WideLayout ? .44 : .69)); }
    int batteryLeft() const { return qMax(InputX + 112, qRound(inputRightEdge() * .58)); }

    QRect historyRect() const {
        return QRect(InputX, 63, qBound(0, batteryLeft() - InputX - 8, 255), 21);
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

    enum { SampleCapacity = 120000 / 125 + 1 };
    struct Sample {
        qint64 timestamp;
        int intervalMs;
        std::array<double, 4> values;
    };

    QVariantMap m_data;
    QFont m_iconFont;
    QLabel *m_battery = nullptr;
    QLabel *m_batteryIcon = nullptr;
    QLabel *m_batteryDetails = nullptr;
    QString m_batteryDetailText;
    QLabel *m_eventTitle = nullptr;
    QPlainTextEdit *m_events = nullptr;
    QString m_fishingStatus;
    QString m_fishingRecognition, m_pendingRecognition;
    qint64 m_lastRecognitionMs = -1600, m_pendingRecognitionMs = 0;
    int m_rod = 3, m_bait = 5;
    QString status = QStringLiteral("已关闭");
    std::array<Sample, SampleCapacity> samples{};
    QPixmap m_graphPixmap;
    QToolButton *m_chartSettings = nullptr;
    QMenu *m_intervalMenu = nullptr, *m_windowMenu = nullptr;
    const QList<int> m_intervals{125, 250, 500, 1000, 2000};
    const QList<int> m_windows{15, 30, 60, 120};
    int m_sampleIntervalMs = 250, m_historyWindowMs = 60000;
    bool m_graphDirty = true;
    int next = 0, count = 0, m_fps = 0, m_skipped = 0;
    quint32 lastRenderedFrames = 0, lastSkippedFrames = 0;
    qint64 lastFrameSampleMs = -1, nextSampleMs = 250;
    double queuePeak = 0, uiPeak = 0;
    QElapsedTimer clock;
};
