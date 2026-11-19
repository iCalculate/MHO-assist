#pragma once

#include <QString>

class QImage;
class QWidget;

void recordFishingCatch(const QString &session, const QString &name, const QImage &label);
void showFishingSummary(QWidget *anchor, const QString &session, int caught);
void showFishingHistory(QWidget *anchor);
