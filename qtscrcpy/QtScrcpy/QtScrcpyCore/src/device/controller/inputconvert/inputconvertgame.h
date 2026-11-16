#include "../../../../../uibase/inputhistory.h"
#ifndef INPUTCONVERTGAME_H
#define INPUTCONVERTGAME_H

#include <QPointF>
#include <QQueue>
#include <QSet>
#include <QHash>
#include <QElapsedTimer>

#include "inputconvertnormal.h"
#include "keymap.h"

#define MULTI_TOUCH_MAX_NUM 10
class InputConvertGame : public InputConvertNormal
{
    Q_OBJECT
public:
    InputConvertGame(Controller *controller);
    virtual ~InputConvertGame();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual bool isCurrentCustomKeymap();

    void loadKeyMap(const QString &json);
    QVariantMap inputDiagnostics() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void releaseAllTouches();
    void auditHostInput();
    void recordInput(const QString &event);
    virtual bool hostKeyDown(quint32 key) const;
    QString touchOwner(int key) const;
    void updateSize(const QSize &frameSize, const QSize &showSize);
    void sendTouchDownEvent(int id, QPointF pos);
    void sendTouchMoveEvent(int id, QPointF pos);
    void sendTouchUpEvent(int id, QPointF pos);
    void sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action);
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);
    QPointF calcFrameAbsolutePos(QPointF relativePos);
    QPointF calcScreenAbsolutePos(QPointF relativePos);

    // multi touch id
    int attachTouchID(int key);
    void detachTouchID(int key);
    int getTouchID(int key);

    // steer wheel
    void processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from);

    // click
    void processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from);

    // click mutil
    void processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from);

    // drag
    void processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from);

    // android key
    void processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from);

    // mouse
    bool processMouseClick(const QMouseEvent *from);
    bool processMouseMove(const QMouseEvent *from);
    void moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel);
    void mouseMoveStartTouch(const QMouseEvent *from);
    void mouseMoveStopTouch();
    void startMouseMoveTimer();
    void stopMouseMoveTimer();

    bool switchGameMap();
    bool checkCursorPos(const QMouseEvent *from);
    void hideMouseCursor(bool hide);

    void getDelayQueue(const QPointF& start, const QPointF& end,
                       const double& distanceStep, const double& posStepconst,
                       quint32 lowestTimer, quint32 highestTimer,
                       QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer);

protected:
    void timerEvent(QTimerEvent *event);

private slots:
    void onDragTimer();

private:
    InputHistory m_inputHistory;
    QHash<int, quint32> m_hostKeys;
    QSet<int> m_missingHostKeys;
    QSet<int> m_observedHostMouse;
    QElapsedTimer m_auditClock;
    int m_auditTicks = 0;
    int m_repairCount = 0;
    QString m_lastRepair;
    qint64 m_lastUiGap = 0;
    quint64 m_viewGeneration = 0;
    QSize m_frameSize;
    QSize m_showSize;
    bool m_gameMap = false;
    bool m_needBackMouseMove = false;
    int m_multiTouchID[MULTI_TOUCH_MAX_NUM] = { 0 };
    QPointF m_touchPositions[MULTI_TOUCH_MAX_NUM];
    bool m_touchActive[MULTI_TOUCH_MAX_NUM] = { false };
    quint64 m_inputGeneration = 0;
    bool m_cursorHidden = false;
    bool m_deviceCancelOk = true;
    QSet<int> m_pressedMouseButtons;
    QSet<int> m_ignoreMouseRelease;
    KeyMap m_keyMap;

    bool m_processMouseMove = true;

    // steer wheel
    struct
    {
        // the first key pressed
        int touchKey = Qt::Key_unknown;
        bool pressedUp = false;
        bool pressedDown = false;
        bool pressedLeft = false;
        bool pressedRight = false;

        QPointF currentPos;
    } m_ctrlSteerWheel;

    // mouse move
    struct
    {
        QPointF lastConverPos;
        QPointF lastPos = { 0.0, 0.0 };
        bool touching = false;
        int timer = 0;
        bool smallEyes = false;
        int ignoreCount = 0;
    } m_ctrlMouseMove;

    // for drag delay
    struct {
        QPointF currentPos;
        QTimer* timer = nullptr;
        QQueue<QPointF> queuePos;
        QQueue<quint32> queueTimer;
        int pressKey = 0;
    } m_dragDelayData;
};

#endif // INPUTCONVERTGAME_H
