#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <array>
#include <cstdint>

#include "directionroundsignatures.h"

enum class DirectionMenu { None, Match, Confirm, Enter, Exit };

struct DirectionMenuMatch {
    DirectionMenu menu = DirectionMenu::None;
    QPointF position;
};

class DirectionMenuRecognizer {
public:
    static DirectionMenuMatch recognize(int width, int height, const uint8_t *luma, int stride)
    {
        if (!luma || width < height || height < 2 || stride < width) return {};
        const QImage source(luma, width, height, stride, QImage::Format_Grayscale8);
        const QImage frame = source.scaled(Width, Height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .convertToFormat(QImage::Format_Grayscale8);
        if (frame.isNull()) return {};

        using namespace DirectionRoundSignatures;
        // Match the button itself and a game-specific label. Generic Confirm and Exit
        // buttons elsewhere in the game must never trigger a click by themselves.
        const auto enter = find(frame, enterButton, {55, 35}, 32);
        if (enter.x() >= 0 && find(frame, enterPrompt, {55, 35}, 43).x() >= 0)
            return match(DirectionMenu::Enter, enter);

        const auto confirm = find(frame, confirmButton, {55, 32}, 33);
        if (confirm.x() >= 0 && find(frame, confirmPrompt, {54, 32}, 43).x() >= 0)
            return match(DirectionMenu::Confirm, confirm);

        const auto exit = find(frame, exitButton, {68, 31}, 35);
        if (exit.x() >= 0 && find(frame, resultsLabel, {60, 37}, 34).x() >= 0)
            return match(DirectionMenu::Exit, exit + QPoint(9, 0));

        const auto lobby = find(frame, lobbyButton, {65, 35}, 42);
        if (lobby.x() >= 0 && find(frame, lobbyTitle, {64, 32}, 46).x() >= 0)
            return match(DirectionMenu::Match, lobby);

        return {};
    }

private:
    enum { Width = 960, Height = 431, MaxEdges = 72 };

    static DirectionMenuMatch match(DirectionMenu menu, const QPoint &point)
    {
        DirectionMenuMatch result;
        result.menu = menu;
        result.position = QPointF(double(point.x()) / Width, double(point.y()) / Height);
        return result;
    }

    static QPoint find(const QImage &frame, const DirectionRoundSignatures::Signature &signature,
                       const QPoint &radius, int minContrast)
    {
        const int stride = frame.bytesPerLine();
        const auto *data = frame.constBits();
        const int count = int(signature.edges.size());
        const QRect search(signature.center - radius, signature.center + radius);
        QPoint best(-1, -1);
        int bestContrast = minContrast * count - 1;
        // Search translations and a small range of UI scales in the decoded frame.
        for (const int scale : {100, 94, 106, 88, 112}) {
            std::array<int, MaxEdges> light, dark;
            int left = 0, top = 0, right = 0, bottom = 0;
            int i = 0;
            for (const auto &edge : signature.edges) {
                const int lx = qRound(edge.lightX * scale / 100.0);
                const int ly = qRound(edge.lightY * scale / 100.0);
                const int dx = qRound(edge.darkX * scale / 100.0);
                const int dy = qRound(edge.darkY * scale / 100.0);
                light[i] = ly * stride + lx;
                dark[i++] = dy * stride + dx;
                left = qMin(left, qMin(lx, dx));
                top = qMin(top, qMin(ly, dy));
                right = qMax(right, qMax(lx, dx));
                bottom = qMax(bottom, qMax(ly, dy));
            }
            const QRect bounds = search.intersected(QRect(QPoint(-left, -top),
                                                          QPoint(Width - 1 - right, Height - 1 - bottom)));
            for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
                for (int x = bounds.left(); x <= bounds.right(); ++x) {
                    const auto *pixel = data + y * stride + x;
                    int signedContrast = 0, absoluteContrast = 0;
                    constexpr int firstEdges = 10;
                    for (int j = 0; j < firstEdges; ++j) {
                        const int delta = pixel[light[j]] - pixel[dark[j]];
                        signedContrast += delta;
                        absoluteContrast += qAbs(delta);
                    }
                    // The first edges have the largest contrast in the reference label.
                    if (signedContrast < minContrast * firstEdges / 2) continue;
                    for (int j = firstEdges; j < count; ++j) {
                        const int delta = pixel[light[j]] - pixel[dark[j]];
                        signedContrast += delta;
                        absoluteContrast += qAbs(delta);
                    }
                    if (signedContrast > bestContrast && signedContrast * 100 >= absoluteContrast * 81) {
                        bestContrast = signedContrast;
                        best = QPoint(x, y);
                    }
                }
            }
            if (bestContrast >= qMax(65, minContrast + 24) * count) break;
        }
        return best;
    }
};

class DirectionRoundFlow {
public:
    bool observe(const DirectionMenuMatch &match, qint64 now)
    {
        const QPointF delta = match.position - candidate.position;
        const bool stable = match.menu != DirectionMenu::None && match.menu == candidate.menu
            && qAbs(delta.x()) < 0.015 && qAbs(delta.y()) < 0.022
            && now - lastObservation <= 650;
        candidate = match;
        lastObservation = now;
        return stable;
    }

    bool shouldTap(DirectionMenu menu, qint64 now) const
    {
        if (menu == lastClicked) {
            // Back off if the page does not advance, but allow the loop to recover.
            const qint64 retryDelay = attempts < 2 ? 2400 : 15000;
            return now - lastClick >= retryDelay;
        }
        // The lobby may reappear briefly while the server is processing a match.
        if (menu == DirectionMenu::Match && (lastClicked == DirectionMenu::Confirm || lastClicked == DirectionMenu::Enter))
            return now - lastClick >= 45000;
        return true;
    }

    void clicked(DirectionMenu menu, qint64 now)
    {
        attempts = menu == lastClicked ? 2 : 1;
        lastClicked = menu;
        lastClick = now;
    }

    void clearObservation() { candidate = {}; }

private:
    DirectionMenuMatch candidate;
    DirectionMenu lastClicked = DirectionMenu::None;
    qint64 lastObservation = -1000;
    qint64 lastClick = -1000;
    int attempts = 0;
};
