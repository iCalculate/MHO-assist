#include "fishingreport.h"

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
#include <QThread>
#include <QTreeWidget>
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
QString readCatchName(const QImage &label)
{
#if defined(Q_OS_WIN) && defined(_MSC_VER)
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Media::Ocr;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment {
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;
        const auto engine = OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"zh-Hans-CN"));
        if (!engine) return {};
        const QImage image = label.scaled(label.size() * 3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
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
        QString name = QString::fromWCharArray(text.c_str(), static_cast<int>(text.size()));
        name.remove(QRegularExpression(QStringLiteral("[^\\p{Han}]")));
        name.remove(QRegularExpression(QStringLiteral("^(普通|优良|精良|稀有|卓越|史诗|传说|神话)")));
        if (name.endsWith(QStringLiteral("新"))) name.chop(1);
        return name.size() >= 2 && name.size() <= 14 ? name : QString();
    } catch (const winrt::hresult_error &) {
        return {};
    }
#else
    Q_UNUSED(label)
    return {};
#endif
}

class FishingHistory final : public QObject {
public:
    FishingHistory()
        : QObject(qApp), m_path(QDir(Config::getInstance().getConfigDirectory()).filePath(QStringLiteral("fishing-history.json")))
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

    void record(const QString &session, const QString &name, const QImage &label)
    {
        const int index = m_rows.size();
        m_rows.append(QJsonObject{
            {QStringLiteral("session"), session},
            {QStringLiteral("caughtAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("name"), name},
        });
        save();
        if (!name.isEmpty() || label.isNull()) return;

        auto result = std::make_shared<QString>();
        auto *reader = QThread::create([label, result] { *result = readCatchName(label); });
        connect(reader, &QThread::finished, this, [this, result, index] {
            if (result->isEmpty()) return;
            auto row = m_rows.at(index).toObject();
            row.insert(QStringLiteral("name"), *result);
            m_rows.replace(index, row);
            save();
        });
        connect(reader, &QThread::finished, reader, &QObject::deleteLater);
        reader->start();
    }

    QJsonArray entries(const QString &session) const
    {
        if (session.isEmpty()) return m_rows;
        QJsonArray rows;
        for (const auto &entry : m_rows) {
            if (entry.toObject().value(QStringLiteral("session")).toString() == session) rows.append(entry);
        }
        return rows;
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
    bool m_canWrite = true;
    bool m_saved = true;
};

FishingHistory &history()
{
    static auto *journal = new FishingHistory;
    return *journal;
}

void showReport(QWidget *anchor, const QString &session, int caught)
{
    const bool summary = !session.isEmpty();
    const auto rows = history().entries(session);
    const QString title = summary ? QStringLiteral("钓鱼完成") : QStringLiteral("渔获记录");
    auto *window = new QDialog(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    window->setWindowTitle(title);
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setAttribute(Qt::WA_ShowWithoutActivating, summary);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setFixedWidth(summary ? 416 : 466);
    window->setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    window->setStyleSheet(QStringLiteral(R"(
        QFrame#catchCard { background: #111c28; border: 1px solid #364b5b; border-radius: 15px; }
        QLabel { color: #eaf4f8; background: transparent; border: none; font-size: 14px; }
        QLabel#catchStatus { background: #193b36; border-radius: 18px; }
        QLabel#catchTitle { font-size: 19px; font-weight: 600; }
        QLabel#catchTotal, QLabel#catchMuted { color: #afc2d0; font-size: 13px; }
        QTreeWidget { background: #152330; color: #e0edf3; border: 1px solid #2e4151; border-radius: 8px;
                      outline: 0; alternate-background-color: #182733; font-size: 14px; }
        QTreeWidget::item { height: 34px; padding-left: 9px; padding-right: 9px; border: 0; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 3px 1px 3px 0; }
        QScrollBar::handle:vertical { background: #526d7d; border-radius: 3px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QPushButton { color: #9be5cd; background: transparent; border: 1px solid #365364;
                      border-radius: 7px; padding: 6px 12px; font-size: 13px; }
        QPushButton:hover, QPushButton:focus { background: #223c47; border-color: #72c2ab; }
        QPushButton#closeCatchReport { font-size: 21px; padding: 0; border-color: transparent; color: #b4c7d3; }
        QPushButton#closeCatchReport:hover { background: #293c4b; color: #ffffff; }
    )"));

    auto *outer = new QVBoxLayout(window);
    outer->setContentsMargins(13, 13, 13, 15);
    auto *card = new QFrame(window);
    card->setObjectName(QStringLiteral("catchCard"));
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(22);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 135));
    card->setGraphicsEffect(shadow);
    outer->addWidget(card);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(17, 16, 17, 15);
    layout->setSpacing(13);

    auto *header = new QHBoxLayout;
    header->setSpacing(11);
    if (summary) {
        auto *check = new QLabel(card);
        check->setObjectName(QStringLiteral("catchStatus"));
        check->setAlignment(Qt::AlignCenter);
        check->setFixedSize(36, 36);
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
    auto *total = new QLabel(summary ? QStringLiteral("本轮钓获 %1 条").arg(caught)
                                     : QStringLiteral("累计记录 %1 条").arg(rows.size()), card);
    total->setObjectName(QStringLiteral("catchTotal"));
    heading->addWidget(total);
    header->addLayout(heading, 1);
    auto *close = new QPushButton(QStringLiteral("×"), card);
    close->setObjectName(QStringLiteral("closeCatchReport"));
    close->setAccessibleName(QStringLiteral("关闭"));
    close->setToolTip(QStringLiteral("关闭"));
    close->setFixedSize(30, 30);
    close->setAutoDefault(false);
    QObject::connect(close, &QPushButton::clicked, window, &QDialog::close);
    header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);

    if (rows.isEmpty()) {
        auto *empty = new QLabel(summary ? QStringLiteral("本轮暂无渔获") : QStringLiteral("暂无渔获记录"), card);
        empty->setObjectName(QStringLiteral("catchMuted"));
        layout->addWidget(empty);
    } else {
        auto *fish = new QTreeWidget(card);
        fish->setAccessibleName(QStringLiteral("鱼名和捕获时间"));
        fish->setHeaderLabels({QStringLiteral("鱼名"), QStringLiteral("捕获时间")});
        fish->setHeaderHidden(true);
        fish->setRootIsDecorated(false);
        fish->setItemsExpandable(false);
        fish->setIndentation(0);
        fish->setUniformRowHeights(true);
        fish->setSelectionMode(QAbstractItemView::NoSelection);
        fish->setAlternatingRowColors(true);
        fish->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        fish->header()->setStretchLastSection(false);
        fish->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        fish->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        for (int index = rows.size() - 1; index >= 0; --index) {
            const auto entry = rows.at(index).toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const auto caughtAt = QDateTime::fromString(entry.value(QStringLiteral("caughtAt")).toString(), Qt::ISODateWithMs).toLocalTime();
            const auto time = caughtAt.toString(summary ? QStringLiteral("HH:mm:ss") : QStringLiteral("yyyy-MM-dd HH:mm"));
            auto *row = new QTreeWidgetItem(fish, {name.isEmpty() ? QStringLiteral("鱼名未识别") : name, time});
            row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            row->setForeground(1, QColor(QStringLiteral("#a3bac8")));
        }
        fish->setFixedHeight(34 * qMin(rows.size(), summary ? 5 : 10) + 2);
        layout->addWidget(fish);
    }

    if (!history().saved()) {
        auto *warning = new QLabel(QStringLiteral("本地记录保存失败"), card);
        warning->setObjectName(QStringLiteral("catchMuted"));
        layout->addWidget(warning);
    }
    if (summary) {
        auto *open = new QPushButton(QStringLiteral("查看历史"), card);
        open->setAutoDefault(false);
        QObject::connect(open, &QPushButton::clicked, window, [window] {
            showFishingHistory(window);
            window->close();
        });
        layout->addWidget(open, 0, Qt::AlignRight);
    }

    window->adjustSize();
    const auto *screen = anchor ? anchor->screen() : QApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry().adjusted(8, 8, -8, -8);
        window->move(area.right() - window->width(), area.bottom() - window->height());
    }
    window->show();
}
}

void recordFishingCatch(const QString &session, const QString &name, const QImage &label)
{
    history().record(session, name, label);
}

void showFishingSummary(QWidget *anchor, const QString &session, int caught)
{
    showReport(anchor, session, caught);
}

void showFishingHistory(QWidget *anchor)
{
    showReport(anchor, {}, 0);
}
