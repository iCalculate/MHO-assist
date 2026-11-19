#pragma once

#include <QImage>
#include <QString>
#include <functional>

class QWidget;

struct FishingCatch {
    QString name;
    QString rarity;
    QString size;
    QString crown;
    QImage nameImage;
    QImage sizeImage;
};

using CatchRecognized = std::function<void(const FishingCatch &)>;
void recordFishingCatch(const QString &session, const FishingCatch &fish, CatchRecognized recognized = {});
void showFishingSummary(QWidget *anchor, const QString &session, int caught, bool completed = true);
void showFishingHistory(QWidget *anchor);
