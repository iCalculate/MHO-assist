#include "fishingreport.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStyleFactory>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <cstring>
#include <memory>

#include "config.h"

#if defined(Q_OS_WIN) && defined(_MSC_VER)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace {
#if defined(Q_OS_WIN) && defined(_MSC_VER)
QString readLabel(const QImage &label, const winrt::Windows::Media::Ocr::OcrEngine &engine)
{
    if (label.isNull()) return {};
    using namespace winrt::Windows::Graphics::Imaging;
    const auto image = label.scaled(label.size() * 3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_Grayscale8);
    const auto size = static_cast<uint32_t>(image.width() * image.height());
    winrt::Windows::Storage::Streams::Buffer buffer(size);
    buffer.Length(size);
    auto *pixels = buffer.data();
    for (int row = 0; row < image.height(); ++row) {
        std::memcpy(pixels + row * image.width(), image.constScanLine(row), image.width());
    }
    const auto bitmap = SoftwareBitmap::CreateCopyFromBuffer(
        buffer, BitmapPixelFormat::Gray8, image.width(), image.height(), BitmapAlphaMode::Ignore);
    const auto text = engine.RecognizeAsync(bitmap).get().Text();
    return QString::fromWCharArray(text.c_str(), static_cast<int>(text.size()));
}
#endif

void recognizeCatch(FishingCatch &fish)
{
#if defined(Q_OS_WIN) && defined(_MSC_VER)
    using namespace winrt::Windows::Media::Ocr;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment {
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;
        const auto engine = OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"zh-Hans-CN"));
        if (!engine) return;
        if (!fish.nameImage.isNull()) {
            static const QRegularExpression nonHan(QStringLiteral("[^\\p{Han}]"));
            static const QRegularExpression rarity(QStringLiteral("^(普通|[优優]良|精良|稀有|卓越|史[诗詩]|[传傳][说說]|神[话話])"));
            QString name = readLabel(fish.nameImage, engine).remove(nonHan);
            const auto rank = rarity.match(name);
            if (rank.hasMatch()) {
                if (fish.rarity.isEmpty()) fish.rarity = rank.captured(1);
                name.remove(0, rank.capturedLength());
            }
            if (name.endsWith(QStringLiteral("新"))) name.chop(1);
            if (fish.name.isEmpty() && name.size() >= 2 && name.size() <= 14) fish.name = name;
        }
        if (!fish.sizeImage.isNull()) {
            static const QRegularExpression whitespace(QStringLiteral("\\s+"));
            static const QRegularExpression crown(QStringLiteral("金冠|[银銀]冠"));
            static const QRegularExpression giant(QStringLiteral("^巨型[种種]$"));
            static const QRegularExpression length(QStringLiteral("^[0-9]{1,6}(?:[.,][0-9]{1,3})?(?:cm|厘米|m|米)?$"));
            QString size = readLabel(fish.sizeImage, engine).normalized(QString::NormalizationForm_KC).remove(whitespace);
            const auto badge = crown.match(size);
            if (badge.hasMatch() && fish.crown.isEmpty()) {
                fish.crown = badge.captured() == QStringLiteral("金冠") ? QStringLiteral("金冠") : QStringLiteral("银冠");
            }
            size.remove(crown);
            if (fish.size.isEmpty()) {
                if (giant.match(size).hasMatch()) fish.size = QStringLiteral("巨型种");
                else if (length.match(size).hasMatch()) fish.size = size;
            }
        }
    } catch (const winrt::hresult_error &) {
        // The catch and any recognized fields have already been saved.
    }
#else
    Q_UNUSED(fish)
#endif
}

enum CatchColumn { Name, Rarity, Size, Crown, Time, ColumnCount };

void updateCatch(QJsonObject &entry, const FishingCatch &fish)
{
    entry.insert(QStringLiteral("name"), fish.name);
    entry.insert(QStringLiteral("rarity"), fish.rarity);
    entry.insert(QStringLiteral("size"), fish.size);
    entry.insert(QStringLiteral("crown"), fish.crown);
}

class FishingHistory final : public QAbstractTableModel {
public:
    FishingHistory()
        : QAbstractTableModel(qApp),
          m_path(QDir(Config::getInstance().getConfigDirectory()).filePath(QStringLiteral("fishing-history.json")))
    {
        QFile file(m_path);
        if (!file.exists()) return;
        if (file.open(QIODevice::ReadOnly)) {
            const auto document = QJsonDocument::fromJson(file.readAll());
            if (document.isArray()) {
                m_rows = document.array();
                return;
            }
        }
        m_canWrite = m_saved = false;
    }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        static const QString titles[] = {QStringLiteral("鱼名"), QStringLiteral("等级"), QStringLiteral("体型"),
                                         QStringLiteral("皇冠"), QStringLiteral("捕获时间")};
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < ColumnCount) {
            return titles[section];
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid)) return {};
        static const QString keys[] = {QStringLiteral("name"), QStringLiteral("rarity"), QStringLiteral("size"),
                                       QStringLiteral("crown"), QStringLiteral("caughtAt")};
        const auto position = m_rows.size() - 1 - index.row();
        const auto entry = m_rows.at(position).toObject();
        const auto column = index.column();
        if (role == Qt::UserRole) return entry.value(QStringLiteral("session")).toString();
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::ForegroundRole) return {};
        QString text = entry.value(keys[column]).toString();
        if (role == Qt::ForegroundRole) {
            if (text.isEmpty() || column == Time) return QColor(QStringLiteral("#9db0bc"));
            if (column == Crown) {
                if (text == QStringLiteral("金冠")) return QColor(QStringLiteral("#f6cd77"));
                if (text == QStringLiteral("银冠")) return QColor(QStringLiteral("#d4e4f4"));
            }
            if (column == Rarity) {
                if (text == QStringLiteral("精良")) return QColor(QStringLiteral("#a2d8b4"));
                if (text == QStringLiteral("稀有")) return QColor(QStringLiteral("#a7d1ff"));
                if (text == QStringLiteral("卓越")) return QColor(QStringLiteral("#d6b8ff"));
            }
            return {};
        }
        if (column == Time) {
            text = QDateTime::fromString(text, Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        if (!text.isEmpty()) return text;
        if (m_reading.contains(position)) return QStringLiteral("识别中");
        if (role == Qt::ToolTipRole) {
            return entry.contains(keys[column]) ? QStringLiteral("此项未识别") : QStringLiteral("此项未记录");
        }
        return entry.contains(keys[column]) ? QStringLiteral("未识别") : QStringLiteral("—");
    }

    void record(const QString &session, const FishingCatch &fish, CatchRecognized recognized)
    {
        const int position = m_rows.size();
        QJsonObject entry{
            {QStringLiteral("session"), session},
            {QStringLiteral("caughtAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        };
        updateCatch(entry, fish);
        const bool needsOcr = !fish.nameImage.isNull() || !fish.sizeImage.isNull();
        if (needsOcr) m_reading.insert(position);
        beginInsertRows({}, 0, 0);
        m_rows.append(entry);
        save();
        endInsertRows();
        if (!needsOcr) {
            if (recognized) recognized(fish);
            return;
        }

        auto result = std::make_shared<FishingCatch>(fish);
        auto *reader = QThread::create([result] { recognizeCatch(*result); });
        connect(reader, &QThread::finished, this, [this, result, position, recognized] {
            auto entry = m_rows.at(position).toObject();
            const auto previous = entry;
            updateCatch(entry, *result);
            if (entry != previous) {
                m_rows.replace(position, entry);
                save();
            }
            m_reading.remove(position);
            const auto row = m_rows.size() - 1 - position;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            if (recognized) recognized(*result);
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
    }

    bool saved() const { return m_saved; }

private:
    void save()
    {
        if (!m_canWrite) return;
        const QByteArray data = QJsonDocument(m_rows).toJson(QJsonDocument::Compact);
        QSaveFile file(m_path);
        m_saved = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }

    QString m_path;
    QJsonArray m_rows;
    QSet<int> m_reading;
    bool m_canWrite = true;
    bool m_saved = true;
};

FishingHistory &history()
{
    static auto *journal = new FishingHistory;
    return *journal;
}

void showReport(QWidget *anchor, const QString &session, int caught, bool completed)
{
    const bool summary = !session.isEmpty();
    const QString title = !summary ? QStringLiteral("渔获记录")
                                   : completed ? QStringLiteral("钓鱼完成") : QStringLiteral("钓鱼已结束");
    const auto *screen = anchor ? anchor->screen() : nullptr;
    if (!screen) screen = QApplication::primaryScreen();
    const QRect area = (screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768)).adjusted(12, 12, -12, -12);
    const int width = qMin(768, area.width());
    constexpr int outerMargin = 10;
    constexpr int cardPadding = 22;
    constexpr int headerHeight = 35;
    constexpr int rowHeight = 38;
    const int maxRows = qBound(1, (area.height() - 200) / rowHeight, summary ? 7 : 12);
    auto *window = new QDialog(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    window->setWindowTitle(title);
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setAttribute(Qt::WA_ShowWithoutActivating, summary);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setFixedWidth(width);
    window->setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    window->setStyleSheet(QStringLiteral(R"(
        QFrame#catchCard { background: #15212b; border: 1px solid #334653; border-radius: 16px; }
        QLabel { color: #edf5f7; background: transparent; border: none; font-size: 14px; }
        QLabel#catchStatus { background: #203e39; border: 1px solid #31544b; border-radius: 13px; }
        QLabel#catchTitle { font-size: 22px; font-weight: 600; }
        QLabel#catchTotal { color: #a9bdc9; font-size: 13px; }
        QLabel#catchEmpty { color: #b7c8d2; background: #192731; border: 1px solid #30434f; border-radius: 8px; }
        QLabel#catchWarning { color: #f0cc83; font-size: 13px; }
        QTableView { background: #1b2933; alternate-background-color: #1d2c37; color: #e8f0f4;
                     border: 1px solid #314451; outline: 0; font-size: 14px; }
        QHeaderView { background: #223440; }
        QHeaderView::section { background: #223440; color: #aebfca; font-size: 13px; font-weight: 600;
                               border: 0; border-bottom: 1px solid #344956; padding: 0 4px; }
        QScrollBar:vertical { background: #14202a; width: 13px; margin: 0; }
        QScrollBar:horizontal { background: #14202a; height: 13px; margin: 0; }
        QScrollBar::handle { background: #597585; border: 2px solid #14202a; border-radius: 6px; }
        QScrollBar::handle:hover { background: #7695a5; }
        QScrollBar::handle:vertical { min-height: 32px; }
        QScrollBar::handle:horizontal { min-width: 32px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QPushButton { color: #c9f3e4; background: #24463e; border: 1px solid #385e53;
                      border-radius: 8px; padding: 7px 13px; font-size: 13px; }
        QPushButton:hover, QPushButton:focus { background: #2d574c; border-color: #6a9f8f; }
        QPushButton#closeCatchReport { font-size: 22px; padding: 0; background: transparent;
                                      border-color: transparent; color: #a9beca; }
        QPushButton#closeCatchReport:hover, QPushButton#closeCatchReport:focus { background: #293c49; color: #ffffff; }
    )"));

    auto *outer = new QVBoxLayout(window);
    outer->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin + 3);
    auto *card = new QFrame(window);
    card->setObjectName(QStringLiteral("catchCard"));
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(22);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 135));
    card->setGraphicsEffect(shadow);
    outer->addWidget(card);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(cardPadding, 18, cardPadding, 17);
    layout->setSpacing(15);

    auto *header = new QHBoxLayout;
    header->setSpacing(13);
    if (summary) {
        auto *check = new QLabel(card);
        check->setObjectName(QStringLiteral("catchStatus"));
        check->setAlignment(Qt::AlignCenter);
        check->setFixedSize(46, 46);
        QPixmap mark(24, 24);
        mark.fill(Qt::transparent);
        QPainter painter(&mark);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#8be3be")), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF points[] = {{6.5, 12.5}, {10.5, 16.5}, {17.5, 8.5}};
        painter.drawPolyline(points, 3);
        painter.end();
        check->setPixmap(mark);
        header->addWidget(check, 0, Qt::AlignVCenter);
    }
    auto *heading = new QVBoxLayout;
    heading->setSpacing(3);
    auto *headingText = new QLabel(title, card);
    headingText->setObjectName(QStringLiteral("catchTitle"));
    heading->addWidget(headingText);
    auto *total = new QLabel(card);
    total->setObjectName(QStringLiteral("catchTotal"));
    heading->addWidget(total);
    header->addLayout(heading, 1);
    auto *close = new QPushButton(QStringLiteral("×"), card);
    close->setObjectName(QStringLiteral("closeCatchReport"));
    close->setAccessibleName(QStringLiteral("关闭"));
    close->setToolTip(QStringLiteral("关闭"));
    close->setFixedSize(31, 31);
    close->setAutoDefault(false);
    QObject::connect(close, &QPushButton::clicked, window, &QDialog::close);
    header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto *table = new QTableView(card);
    table->setAccessibleName(QStringLiteral("渔获记录"));
    if (auto *style = QStyleFactory::create(QStringLiteral("Fusion"))) {
        style->setParent(table);
        table->setStyle(style);
    }
    auto *entries = new QSortFilterProxyModel(table);
    entries->setSourceModel(&history());
    if (summary) {
        entries->setFilterRole(Qt::UserRole);
        entries->setFilterRegularExpression(QRegularExpression::anchoredPattern(QRegularExpression::escape(session)));
    }
    table->setModel(entries);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide();
    table->verticalHeader()->setMinimumSectionSize(rowHeight);
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->horizontalHeader()->setFixedHeight(headerHeight);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->horizontalHeader()->setSectionsClickable(false);
    table->ensurePolished();
    const int bar = table->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, table);
    const int usableWidth = width - 2 * (outerMargin + cardPadding) - 4 - bar;
    const int timeWidth = qMax(174, table->fontMetrics().horizontalAdvance(QStringLiteral("2026-11-19 00:00:00")) + 24);
    const int otherWidth = 90 + 104 + 90 + timeWidth;
    table->setColumnWidth(Name, qMax(220, usableWidth - otherWidth));
    table->setColumnWidth(Rarity, 90);
    table->setColumnWidth(Size, 104);
    table->setColumnWidth(Crown, 90);
    table->setColumnWidth(Time, timeWidth);
    layout->addWidget(table);

    auto *empty = new QLabel(summary ? QStringLiteral("本轮暂无渔获") : QStringLiteral("暂无渔获记录"), card);
    empty->setObjectName(QStringLiteral("catchEmpty"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setFixedHeight(69);
    layout->addWidget(empty);

    auto *warning = new QLabel(QStringLiteral("本地记录保存失败"), card);
    warning->setObjectName(QStringLiteral("catchWarning"));
    layout->addWidget(warning);
    if (summary) {
        auto *open = new QPushButton(QStringLiteral("查看历史"), card);
        open->setAutoDefault(false);
        QObject::connect(open, &QPushButton::clicked, window, [window] {
            showFishingHistory(window);
            window->close();
        });
        layout->addWidget(open, 0, Qt::AlignRight);
    }

    const auto fitRows = [=] {
        const int count = entries->rowCount();
        const int visibleRows = qMin(count, maxRows);
        total->setText(summary ? QStringLiteral("本次渔获 %1 条").arg(caught)
                               : QStringLiteral("累计渔获 %1 条").arg(count));
        const int viewWidth = usableWidth + (count <= maxRows ? bar : 0);
        const int horizontalBar = table->horizontalHeader()->length() > viewWidth ? bar : 0;
        table->setFixedHeight(headerHeight + rowHeight * visibleRows + 2 + horizontalBar);
        table->setVisible(count != 0);
        empty->setVisible(count == 0);
        warning->setVisible(!history().saved());
        window->adjustSize();
        window->move(area.right() + 1 - window->width(), area.bottom() + 1 - window->height());
    };
    QObject::connect(entries, &QAbstractItemModel::rowsInserted, window, fitRows);
    QObject::connect(&history(), &QAbstractItemModel::dataChanged, window, [warning] { warning->setVisible(!history().saved()); });
    fitRows();
    window->show();
}
}

void recordFishingCatch(const QString &session, const FishingCatch &fish, CatchRecognized recognized)
{
    history().record(session, fish, recognized);
}

void showFishingSummary(QWidget *anchor, const QString &session, int caught, bool completed)
{
    showReport(anchor, session, caught, completed);
}

void showFishingHistory(QWidget *anchor)
{
    showReport(anchor, {}, 0, false);
}
