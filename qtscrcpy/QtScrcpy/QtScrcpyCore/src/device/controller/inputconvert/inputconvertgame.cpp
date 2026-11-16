#include <QDebug>
#include <QApplication>
#include <QWidget>
#include <QCursor>
#include <QGuiApplication>
#include <QTimer>
#include <QTime>
#include <QRandomGenerator>
#include <QKeySequence>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "inputconvertgame.h"
#include "controller.h"

#define CURSOR_POS_CHECK 50

InputConvertGame::InputConvertGame(Controller *controller) : InputConvertNormal(controller) {
    qApp->installEventFilter(this);
    connect(controller, &Controller::deviceTouchCancelFinished, this, [this](bool ok) {
        m_deviceCancelOk = ok;
        if (!ok) {
            m_gameMap = false;
            m_needBackMouseMove = false;
            hideMouseCursor(false);
            emit grabCursor(false);
            qWarning() << "input reset: cancellation failed; keymap mode: normal; retry Ctrl";
        }
    });
    m_auditClock.start();
    auto audit = new QTimer(this);
    audit->setInterval(100);
    connect(audit, &QTimer::timeout, this, &InputConvertGame::auditHostInput);
    audit->start();

}

InputConvertGame::~InputConvertGame()
{
    qApp->removeEventFilter(this);
    releaseAllTouches();
    hideMouseCursor(false);
    emit grabCursor(false);
}

void InputConvertGame::releaseAllTouches()
{
    if (!m_hostKeys.isEmpty() || !m_pressedMouseButtons.isEmpty() || m_ctrlMouseMove.touching || m_normalTouchDown) recordInput(QStringLiteral("重置"));
    m_ignoreMouseRelease.unite(m_pressedMouseButtons);
    m_pressedMouseButtons.clear();
    m_hostKeys.clear();
    m_missingHostKeys.clear();
    m_observedHostMouse.clear();
    ++m_viewGeneration;
    ++m_inputGeneration; // Invalidate queued callbacks from the old input session.
    stopMouseMoveTimer();
    m_ctrlSteerWheel.pressedUp = false;
    m_ctrlSteerWheel.pressedDown = false;
    m_ctrlSteerWheel.pressedLeft = false;
    m_ctrlSteerWheel.pressedRight = false;
    m_ctrlSteerWheel.touchKey = Qt::Key_unknown;
    if (m_dragDelayData.timer) {
        m_dragDelayData.timer->stop();
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;
    }
    m_dragDelayData.queuePos.clear();
    m_dragDelayData.queueTimer.clear();
    m_dragDelayData.pressKey = 0;
    for (int id = 0; id < MULTI_TOUCH_MAX_NUM; ++id) {
        if (m_touchActive[id]) {
            sendTouchUpEvent(id, m_touchPositions[id]);
        }
        m_multiTouchID[id] = 0;
    }
    m_ctrlMouseMove.touching = false;
    m_ctrlMouseMove.smallEyes = false;
    m_ctrlMouseMove.lastPos = QPointF();
    m_ctrlMouseMove.ignoreCount = 0;
    m_processMouseMove = true;
    releaseNormalTouch();
    m_deviceCancelOk = !m_controller || m_controller->cancelDeviceTouches();
}

bool InputConvertGame::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *window = qobject_cast<QWidget *>(watched);
    if (window && window->isWindow() && event->type() == QEvent::WindowDeactivate && (m_gameMap || m_normalTouchDown)) {
        releaseAllTouches();
        m_gameMap = false;
        m_needBackMouseMove = false;
        hideMouseCursor(false);
        emit grabCursor(false);
        qInfo() << "input reset: window deactivated; keymap mode: normal";
    }
    return InputConvertNormal::eventFilter(watched, event);
}

void InputConvertGame::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!from) { return; }
    const int button = static_cast<int>(from->button());
    if (from->type() == QEvent::MouseButtonRelease && m_ignoreMouseRelease.remove(button)) { return; }
    if (from->type() == QEvent::MouseButtonPress) { m_ignoreMouseRelease.remove(button); }
    if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonRelease) {
        QString name = button==Qt::LeftButton?QStringLiteral("mouse-left"):button==Qt::RightButton?QStringLiteral("mouse-right"):button==Qt::MiddleButton?QStringLiteral("mouse-middle"):touchOwner(button);
        recordInput(name + (from->type()==QEvent::MouseButtonPress?QStringLiteral(" ↓"):QStringLiteral(" ↑")));
    }

    // 处理开关按键
    if (m_keyMap.isSwitchOnKeyboard() == false && m_keyMap.getSwitchKey() == static_cast<int>(from->button())) {
        if (from->type() != QEvent::MouseButtonPress) {
            return;
        }
        if (!switchGameMap()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    if (!m_needBackMouseMove && m_gameMap) {
        updateSize(frameSize, showSize);
        // mouse move
        if (m_keyMap.isValidMouseMoveMap()) {
            if (processMouseMove(from)) {
                return;
            }
        }
        // mouse click
        if (processMouseClick(from)) {
            return;
        }
    }
    InputConvertNormal::mouseEvent(from, frameSize, showSize);
}

void InputConvertGame::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_gameMap) {
        updateSize(frameSize, showSize);
    } else {
        InputConvertNormal::wheelEvent(from, frameSize, showSize);
    }
}

void InputConvertGame::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!from) { return; }
    if (!from->isAutoRepeat()) {
        recordInput(readableKey(from->key()) + (from->type()==QEvent::KeyPress?QStringLiteral(" ↓"):QStringLiteral(" ↑")));
        if (from->type() == QEvent::KeyRelease) {
            m_hostKeys.remove(from->key());
            m_missingHostKeys.remove(from->key());
        } else if (m_gameMap && from->nativeVirtualKey()) {
            m_hostKeys.insert(from->key(), from->nativeVirtualKey());
            m_missingHostKeys.remove(from->key());
        }
    }
    // 处理开关按键
    if (m_keyMap.isSwitchOnKeyboard() && m_keyMap.getSwitchKey() == from->key()) {
        if (QEvent::KeyPress != from->type() || from->isAutoRepeat()) {
            return;
        }
        if (!switchGameMap()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeKey(from->key());
    // 处理特殊按键：可以释放出鼠标的按键
    if (m_needBackMouseMove && KeyMap::KMT_CLICK == node.type && node.data.click.switchMap) {
        updateSize(frameSize, showSize);
        // Qt::Key_Tab Qt::Key_M for PUBG mobile
        processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
        return;
    }

    if (m_gameMap) {
        updateSize(frameSize, showSize);
        if (!from || from->isAutoRepeat()) {
            return;
        }

        // A released small-eyes key must never re-press a stationary view finger.
        if (m_keyMap.isValidMouseMoveMap() && from->key() == m_keyMap.getMouseMoveMap().data.mouseMove.smallEyes.key) {
            const quint64 viewGeneration = ++m_viewGeneration;
            m_ctrlMouseMove.smallEyes = (QEvent::KeyPress == from->type());
            stopMouseMoveTimer();
            mouseMoveStopTouch();
            m_ctrlMouseMove.lastPos = QPointF();
            m_processMouseMove = !m_ctrlMouseMove.smallEyes;
            if (m_ctrlMouseMove.smallEyes) {
                const quint64 generation = m_inputGeneration;
                QTimer::singleShot(60, this, [this, generation, viewGeneration]() {
                    if (generation != m_inputGeneration || viewGeneration != m_viewGeneration
                        || !m_gameMap || !m_ctrlMouseMove.smallEyes) { return; }
                    mouseMoveStartTouch(nullptr);
                    m_processMouseMove = true;
                });
            }
            return;
        }

        switch (node.type) {
        // 处理方向盘
        case KeyMap::KMT_STEER_WHEEL:
            processSteerWheel(node, from);
            return;
        // 处理普通按键
        case KeyMap::KMT_CLICK:
            processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
            processAndroidKey(node.data.click.keyNode.androidKey, from);
            return;
        case KeyMap::KMT_CLICK_TWICE:
            processKeyClick(node.data.clickTwice.keyNode.pos, true, false, from);
            processAndroidKey(node.data.clickTwice.keyNode.androidKey, from);
            return;
        case KeyMap::KMT_CLICK_MULTI:
            processKeyClickMulti(node.data.clickMulti.keyNode.delayClickNodes, node.data.clickMulti.keyNode.delayClickNodesCount, from);
            return;
        case KeyMap::KMT_DRAG:
            processKeyDrag(node.data.drag.keyNode.pos, node.data.drag.keyNode.extendPos,
                         node.data.drag.startDelay, node.data.drag.dragSpeed, from);
            return;
        case KeyMap::KMT_ANDROID_KEY:
            processAndroidKey(node.data.androidKey.keyNode.androidKey, from);
        default:
            break;
        }
    } else {
        InputConvertNormal::keyEvent(from, frameSize, showSize);
    }
}

bool InputConvertGame::isCurrentCustomKeymap()
{
    return m_gameMap;
}

void InputConvertGame::loadKeyMap(const QString &json)
{
    m_keyMap.loadKeyMap(json);
}

void InputConvertGame::updateSize(const QSize &frameSize, const QSize &showSize)
{
    if (showSize != m_showSize) {
        if (m_gameMap && m_keyMap.isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            // show size change, resize grab cursor
            emit grabCursor(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;
}

void InputConvertGame::sendTouchDownEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_DOWN);
}

void InputConvertGame::sendTouchMoveEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_MOVE);
}

void InputConvertGame::sendTouchUpEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_UP);
}

void InputConvertGame::sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action)
{
    if (0 > id || MULTI_TOUCH_MAX_NUM - 1 < id) {
        return; // A late key-up after a reset has no active touch.
    }
    if (action == AMOTION_EVENT_ACTION_UP && !m_touchActive[id]) { return; }
    if (action == AMOTION_EVENT_ACTION_DOWN && m_touchActive[id]) { return; }
    //qDebug() << "id:" << id << " pos:" << pos << " action" << action;
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (!controlMsg) {
        return;
    }

    QPoint absolutePos = calcFrameAbsolutePos(pos).toPoint();
    if (AMOTION_EVENT_ACTION_MOVE == action &&
        (!m_touchActive[id] || calcFrameAbsolutePos(m_touchPositions[id]).toPoint() == absolutePos)) {
        delete controlMsg;
        return;
    }
    if (action != AMOTION_EVENT_ACTION_MOVE) {
        qInfo().noquote() << "input touch" << (action == AMOTION_EVENT_ACTION_DOWN ? "DOWN" : "UP")
                         << "id=" << id << "owner=" << touchOwner(m_multiTouchID[id]) << "pos=" << pos;
    }
    m_touchPositions[id] = pos;
    m_touchActive[id] = action != AMOTION_EVENT_ACTION_UP;

    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(id),
        action,
        static_cast<AndroidMotioneventButtons>(0),
        static_cast<AndroidMotioneventButtons>(0),
        QRect(absolutePos, m_frameSize),
        AMOTION_EVENT_ACTION_UP == action ? 0.0f : 1.0f);
    sendControlMsg(controlMsg);
}

void InputConvertGame::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode) {
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlMsg) {
        return;
    }

    controlMsg->setInjectKeycodeMsgData(action, keyCode, 0, AMETA_NONE);
    sendControlMsg(controlMsg);
}

QPointF InputConvertGame::calcFrameAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_frameSize.width() * relativePos.x());
    absolutePos.setY(m_frameSize.height() * relativePos.y());
    return absolutePos;
}

QPointF InputConvertGame::calcScreenAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_showSize.width() * relativePos.x());
    absolutePos.setY(m_showSize.height() * relativePos.y());
    return absolutePos;
}

int InputConvertGame::attachTouchID(int key)
{
    const int existing = getTouchID(key);
    if (existing >= 0) { return existing; }
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (0 == m_multiTouchID[i]) {
            m_multiTouchID[i] = key;
            return i;
        }
    }
    return -1;
}

void InputConvertGame::detachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            m_multiTouchID[i] = 0;
            return;
        }
    }
}

int InputConvertGame::getTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            return i;
        }
    }
    return -1;
}

// -------- steer wheel event --------

void InputConvertGame::getDelayQueue(const QPointF& start, const QPointF& end,
                                     const double& distanceStep, const double& posStepconst,
                                     quint32 lowestTimer, quint32 highestTimer,
                                     QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer) {
    double x1 = start.x();
    double y1 = start.y();
    double x2 = end.x();
    double y2 = end.y();

    double dx=x2-x1;
    double dy=y2-y1;
    double e=(fabs(dx)>fabs(dy))?fabs(dx):fabs(dy);
    e /= distanceStep;
    dx/=e;
    dy/=e;

    QQueue<QPointF> queue;
    QQueue<quint32> queue2;
    for(int i=1;i<=e;i++) {
        QPointF pos(x1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst), y1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst));
        queue.enqueue(pos);
        queue2.enqueue(QRandomGenerator::global()->bounded(lowestTimer, highestTimer));
        x1+=dx;
        y1+=dy;
    }

    queuePos = queue;
    queueTimer = queue2;
}

void InputConvertGame::processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from)
{
    int key = from->key();
    bool flag = from->type() == QEvent::KeyPress;
    // identify keys
    if (key == node.data.steerWheel.up.key) {
        m_ctrlSteerWheel.pressedUp = flag;
    } else if (key == node.data.steerWheel.right.key) {
        m_ctrlSteerWheel.pressedRight = flag;
    } else if (key == node.data.steerWheel.down.key) {
        m_ctrlSteerWheel.pressedDown = flag;
    } else { // left
        m_ctrlSteerWheel.pressedLeft = flag;
    }

    // calc offset and pressed number
    QPointF offset(0.0, 0.0);
    int pressedNum = 0;
    if (m_ctrlSteerWheel.pressedUp) {
        ++pressedNum;
        offset.ry() -= node.data.steerWheel.up.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedRight) {
        ++pressedNum;
        offset.rx() += node.data.steerWheel.right.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedDown) {
        ++pressedNum;
        offset.ry() += node.data.steerWheel.down.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedLeft) {
        ++pressedNum;
        offset.rx() -= node.data.steerWheel.left.extendOffset;
    }
    qInfo().noquote() << "input steer" << readableKey(key)
                     << (flag ? "DOWN" : "UP") << "held U/D/L/R="
                     << m_ctrlSteerWheel.pressedUp << m_ctrlSteerWheel.pressedDown
                     << m_ctrlSteerWheel.pressedLeft << m_ctrlSteerWheel.pressedRight;

    if (pressedNum == 0) {
        sendTouchUpEvent(getTouchID(m_ctrlSteerWheel.touchKey), m_ctrlSteerWheel.currentPos);
        detachTouchID(m_ctrlSteerWheel.touchKey);
        m_ctrlSteerWheel.touchKey = Qt::Key_unknown;
        return;
    }

    // The configured wheel is fixed: press the requested direction directly.
    // A center-first gesture can consume a short tap without moving the player.
    m_ctrlSteerWheel.currentPos = node.data.steerWheel.centerPos + offset;
    if (getTouchID(m_ctrlSteerWheel.touchKey) < 0) {
        m_ctrlSteerWheel.touchKey = from->key();
        const int id = attachTouchID(m_ctrlSteerWheel.touchKey);
        if (id < 0) { m_ctrlSteerWheel.touchKey = Qt::Key_unknown; return; }
        sendTouchDownEvent(id, m_ctrlSteerWheel.currentPos);
    } else {
        // Keep one pointer when adding/removing a direction in a chord.
        sendTouchMoveEvent(getTouchID(m_ctrlSteerWheel.touchKey), m_ctrlSteerWheel.currentPos);
    }
}

// -------- key event --------

void InputConvertGame::processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from)
{
    if (switchMap && QEvent::KeyRelease == from->type()) {
        m_needBackMouseMove = !m_needBackMouseMove;
        hideMouseCursor(!m_needBackMouseMove);
    }

    if (QEvent::KeyPress == from->type()) {
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, clickPos);
        if (clickTwice) {
            sendTouchUpEvent(getTouchID(from->key()), clickPos);
            detachTouchID(from->key());
        }
    } else if (QEvent::KeyRelease == from->type()) {
        if (clickTwice) {
            int id = attachTouchID(from->key());
            sendTouchDownEvent(id, clickPos);
        }
        sendTouchUpEvent(getTouchID(from->key()), clickPos);
        detachTouchID(from->key());
    }
}

void InputConvertGame::processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from)
{
    if (QEvent::KeyPress != from->type() || from->isAutoRepeat()) {
        return;
    }

    const quint64 generation = m_inputGeneration;
    int key = from->key();
    int delay = 0;
    QPointF clickPos;

    for (int i = 0; i < count; i++) {
        delay += nodes[i].delay;
        clickPos = nodes[i].pos;
        QTimer::singleShot(delay, this, [this, key, clickPos, generation]() {
            if (generation != m_inputGeneration || !m_gameMap) { return; }
            int id = attachTouchID(key);
            sendTouchDownEvent(id, clickPos);
        });

        // Don't up it too fast
        delay += 20;
        QTimer::singleShot(delay, this, [this, key, clickPos, generation]() {
            if (generation != m_inputGeneration || !m_gameMap) { return; }
            int id = getTouchID(key);
            sendTouchUpEvent(id, clickPos);
            detachTouchID(key);
        });
    }
}

void InputConvertGame::onDragTimer() {
    if(m_dragDelayData.queuePos.empty()) {
        return;
    }
    int id = getTouchID(m_dragDelayData.pressKey);
    m_dragDelayData.currentPos = m_dragDelayData.queuePos.dequeue();
    sendTouchMoveEvent(id, m_dragDelayData.currentPos);

    if(m_dragDelayData.queuePos.empty()) {
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;

        sendTouchUpEvent(id, m_dragDelayData.currentPos);
        detachTouchID(m_dragDelayData.pressKey);

        m_dragDelayData.currentPos = QPointF();
        m_dragDelayData.pressKey = 0;
        return;
    }

    if(!m_dragDelayData.queuePos.empty()) {
        m_dragDelayData.timer->start(m_dragDelayData.queueTimer.dequeue());
    }
}

void InputConvertGame::processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from)
{
    if (QEvent::KeyPress == from->type()) {
        // stop last
        if (m_dragDelayData.timer && m_dragDelayData.timer->isActive()) {
            m_dragDelayData.timer->stop();
            delete m_dragDelayData.timer;
            m_dragDelayData.timer = nullptr;
            m_dragDelayData.queuePos.clear();
            m_dragDelayData.queueTimer.clear();

            sendTouchUpEvent(getTouchID(m_dragDelayData.pressKey), m_dragDelayData.currentPos);
            detachTouchID(m_dragDelayData.pressKey);

            m_dragDelayData.currentPos = QPointF();
            m_dragDelayData.pressKey = 0;
        }

        // start this
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, startPos);

        m_dragDelayData.timer = new QTimer(this);
        m_dragDelayData.timer->setSingleShot(true);
        connect(m_dragDelayData.timer, &QTimer::timeout, this, &InputConvertGame::onDragTimer);
        m_dragDelayData.pressKey = from->key();
        m_dragDelayData.currentPos = startPos;
        m_dragDelayData.queuePos.clear();
        m_dragDelayData.queueTimer.clear();

        // Clamp dragSpeed to 0-1 range
        const float speed = qBound(0.0f, static_cast<float>(dragSpeed), 1.0f);
        
        // Calculate delays based on dragSpeed
        // dragSpeed = 1 -> minDelay = 1, maxDelay = 2 (fastest)
        // dragSpeed = 0 -> minDelay = 30, maxDelay = 40 (slowest)
        const quint32 minDelay = static_cast<quint32>(1 + (1.0f - speed) * 29);  // 1 to 30
        const quint32 maxDelay = minDelay + static_cast<quint32>((1.0f - speed) * 9) + 1;  // // min + (0 to 9) + 1

        getDelayQueue(startPos, endPos,
                      0.01f, 0.0005f,
                      minDelay,
                      maxDelay,
                      m_dragDelayData.queuePos,
                      m_dragDelayData.queueTimer);

        m_dragDelayData.timer->start(startDelay);
    }
}

void InputConvertGame::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (from->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

// -------- mouse event --------

bool InputConvertGame::processMouseClick(const QMouseEvent *from)
{
    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeMouse(from->button());
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (QEvent::MouseButtonPress == from->type() || QEvent::MouseButtonDblClick == from->type()) {
        m_missingHostKeys.remove(-static_cast<int>(from->button()));
        m_observedHostMouse.remove(static_cast<int>(from->button()));
#ifdef Q_OS_WIN
        const int vk = from->button() == Qt::LeftButton ? VK_LBUTTON : from->button() == Qt::RightButton ? VK_RBUTTON : from->button() == Qt::MiddleButton ? VK_MBUTTON : 0;
        if (vk && hostKeyDown(static_cast<quint32>(vk))) { m_observedHostMouse.insert(static_cast<int>(from->button())); }
#endif
        m_pressedMouseButtons.insert(static_cast<int>(from->button()));
        int id = attachTouchID(from->button());
        sendTouchDownEvent(id, node.data.click.keyNode.pos);
        return true;
    }
    if (QEvent::MouseButtonRelease == from->type()) {
        m_missingHostKeys.remove(-static_cast<int>(from->button()));
        m_observedHostMouse.remove(static_cast<int>(from->button()));
        m_pressedMouseButtons.remove(static_cast<int>(from->button()));
        int id = getTouchID(from->button());
        sendTouchUpEvent(id, node.data.click.keyNode.pos);
        detachTouchID(from->button());
        return true;
    }
    return false;
}

bool InputConvertGame::processMouseMove(const QMouseEvent *from)
{
    if (QEvent::MouseMove != from->type()) {
        return false;
    }

    if (checkCursorPos(from)) {
        m_ctrlMouseMove.lastPos = QPointF(0.0, 0.0);
        return true;
    }

    auto lastPos = m_ctrlMouseMove.lastPos;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_ctrlMouseMove.lastPos = from->localPos();
#else
    m_ctrlMouseMove.lastPos = from->position();
#endif

    if (m_ctrlMouseMove.ignoreCount > 0) {
        --m_ctrlMouseMove.ignoreCount;
        return true;
    }

    if (!lastPos.isNull() && m_processMouseMove) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF distance_raw{from->localPos() - lastPos};
#else
        QPointF distance_raw{from->position() - lastPos};
#endif
        QPointF speedRatio  {m_keyMap.getMouseMoveMap().data.mouseMove.speedRatio};
        QPointF distance    {distance_raw.x() / speedRatio.x(), distance_raw.y() / speedRatio.y()};

        if (distance_raw.isNull()) { return true; }
        mouseMoveStartTouch(from);
        startMouseMoveTimer();

        m_ctrlMouseMove.lastConverPos.setX(m_ctrlMouseMove.lastConverPos.x() + distance.x() / m_showSize.width());
        m_ctrlMouseMove.lastConverPos.setY(m_ctrlMouseMove.lastConverPos.y() + distance.y() / m_showSize.height());

        if (m_ctrlMouseMove.lastConverPos.x() < 0.05 || m_ctrlMouseMove.lastConverPos.x() > 0.95 || m_ctrlMouseMove.lastConverPos.y() < 0.05
            || m_ctrlMouseMove.lastConverPos.y() > 0.95) {
            if (m_ctrlMouseMove.smallEyes) {
                m_processMouseMove = false;
                const quint64 generation = m_inputGeneration;
                const quint64 viewGeneration = ++m_viewGeneration;
                int delay = 30;
                QTimer::singleShot(delay, this, [this, generation, viewGeneration]() { if (generation == m_inputGeneration && viewGeneration == m_viewGeneration) { mouseMoveStopTouch(); } });
                QTimer::singleShot(delay * 2, this, [this, generation, viewGeneration]() {
                    if (generation != m_inputGeneration || viewGeneration != m_viewGeneration || !m_gameMap || !m_ctrlMouseMove.smallEyes) { return; }
                    mouseMoveStartTouch(nullptr);
                    m_processMouseMove = true;
                });
            } else {
                mouseMoveStopTouch();
                m_ctrlMouseMove.ignoreCount = 5;
                return true;
            }
        }

        sendTouchMoveEvent(getTouchID(Qt::ExtraButton24), m_ctrlMouseMove.lastConverPos);
    }

    return true;
}

bool InputConvertGame::checkCursorPos(const QMouseEvent *from)
{
    bool moveCursor = false;
    QPoint pos = from->pos();
    if (pos.x() < CURSOR_POS_CHECK) {
        pos.setX(m_showSize.width() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.x() > m_showSize.width() - CURSOR_POS_CHECK) {
        pos.setX(CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() < CURSOR_POS_CHECK) {
        pos.setY(m_showSize.height() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() > m_showSize.height() - CURSOR_POS_CHECK) {
        pos.setY(CURSOR_POS_CHECK);
        moveCursor = true;
    }

    if (moveCursor) {
        moveCursorTo(from, pos);
    }

    return moveCursor;
}

void InputConvertGame::moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPoint posOffset = from->pos() - localPosPixel;
    QPoint globalPos = from->globalPos();
#else
    QPoint posOffset = from->position().toPoint() - localPosPixel;
    QPoint globalPos = from->globalPosition().toPoint();
#endif
    globalPos -= posOffset;
    //qDebug()<<"move cursor to "<<globalPos<<" offset "<<posOffset;
#ifdef Q_OS_MACOS
    // On macOS, QCursor::setPos() posts a synthetic mouse-moved event (CGEventPost)
    // and does NOT re-associate the hardware mouse with the on-screen cursor, so the
    // warp does not "stick": the next hardware delta snaps the cursor back. That makes
    // mouseMoveMap re-centering a no-op, and the camera can no longer pan past the edge.
    // CGWarpMouseCursorPosition performs a real warp, and
    // CGAssociateMouseAndMouseCursorPosition(true) immediately restores the
    // mouse<->cursor association so subsequent hardware deltas keep flowing. This
    // mirrors the approach already used in util/mousetap/cocoamousetap.mm.
    CGWarpMouseCursorPosition(CGPointMake(globalPos.x(), globalPos.y()));
    CGAssociateMouseAndMouseCursorPosition(true);
#else
    QElapsedTimer warpClock; warpClock.start();
    QCursor::setPos(globalPos);
    if (warpClock.elapsed() >= 20) {
        qInfo() << "input perf cursor warp ms=" << warpClock.elapsed();
    }
#endif
}

void InputConvertGame::mouseMoveStartTouch(const QMouseEvent *from)
{
    Q_UNUSED(from)
    if (!m_ctrlMouseMove.touching) {
        QPointF mouseMoveStartPos
            = m_ctrlMouseMove.smallEyes ? m_keyMap.getMouseMoveMap().data.mouseMove.smallEyes.pos : m_keyMap.getMouseMoveMap().data.mouseMove.startPos;
        int id = attachTouchID(Qt::ExtraButton24);
        sendTouchDownEvent(id, mouseMoveStartPos);
        m_ctrlMouseMove.lastConverPos = mouseMoveStartPos;
        m_ctrlMouseMove.touching = true;
    }
}

void InputConvertGame::mouseMoveStopTouch()
{
    if (m_ctrlMouseMove.touching) {
        sendTouchUpEvent(getTouchID(Qt::ExtraButton24), m_ctrlMouseMove.lastConverPos);
        detachTouchID(Qt::ExtraButton24);
        m_ctrlMouseMove.touching = false;
    }
}

void InputConvertGame::startMouseMoveTimer()
{
    stopMouseMoveTimer();
    m_ctrlMouseMove.timer = startTimer(500, Qt::PreciseTimer);
}

void InputConvertGame::stopMouseMoveTimer()
{
    if (0 != m_ctrlMouseMove.timer) {
        killTimer(m_ctrlMouseMove.timer);
        m_ctrlMouseMove.timer = 0;
    }
}

bool InputConvertGame::switchGameMap()
{
    const bool nextMode = !m_gameMap;
    releaseAllTouches();
    m_gameMap = nextMode && m_deviceCancelOk;
    qInfo() << QString("current keymap mode: %1").arg(m_gameMap ? "custom" : "normal");

    if (!m_keyMap.isValidMouseMoveMap()) {
        return m_gameMap;
    }
#ifdef QT_NO_DEBUG
    // grab cursor and set cursor only mouse move map
    emit grabCursor(m_gameMap);
#endif
    hideMouseCursor(m_gameMap);

    if (!m_gameMap) {
        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }

    return m_gameMap;
}

void InputConvertGame::hideMouseCursor(bool hide)
{
    if (hide == m_cursorHidden) { return; }
    if (hide) {
#ifdef QT_NO_DEBUG
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
#else
        QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
#endif
    } else {
        QGuiApplication::restoreOverrideCursor();
    }
    m_cursorHidden = hide;
}

void InputConvertGame::timerEvent(QTimerEvent *event)
{
    if (m_ctrlMouseMove.timer == event->timerId()) {
        stopMouseMoveTimer();
        // Don't auto-reset view when smallEyes mode is active
        if (!m_ctrlMouseMove.smallEyes) {
            mouseMoveStopTouch();
        }
    }
}


QString InputConvertGame::touchOwner(int key) const
{
    if (key == Qt::ExtraButton24) { return QStringLiteral("mouse-view"); }
    if (key == Qt::LeftButton) { return QStringLiteral("mouse-left"); }
    if (key == Qt::RightButton) { return QStringLiteral("mouse-right"); }
    if (key == Qt::MiddleButton) { return QStringLiteral("mouse-middle"); }
    return readableKey(key);
}

bool InputConvertGame::hostKeyDown(quint32 key) const
{
#ifdef Q_OS_WIN
    return (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
#else
    Q_UNUSED(key)
    return true;
#endif
}

void InputConvertGame::auditHostInput()
{
    const qint64 gap = m_auditClock.restart();
    m_lastUiGap = qMax<qint64>(0, gap - 100);
    if (gap > 250) { qWarning() << "input/video UI event-loop gap ms=" << gap; }
#ifdef Q_OS_WIN
    if (m_normalTouchDown) {
        if (hostKeyDown(VK_LBUTTON) || hostKeyDown(VK_RBUTTON) || hostKeyDown(VK_MBUTTON)) {
            m_missingHostKeys.remove(-100);
        } else if (m_missingHostKeys.contains(-100)) {
            qWarning() << "input watchdog: missing normal mouse UP; repairing";
            ++m_repairCount; m_lastRepair = QStringLiteral("普通鼠标");
            releaseNormalTouch();
            m_missingHostKeys.remove(-100);
        } else { m_missingHostKeys.insert(-100); }
    } else { m_missingHostKeys.remove(-100); }
    const auto buttons = m_pressedMouseButtons.values();
    for (int button : buttons) {
        const int vk = button == Qt::LeftButton ? VK_LBUTTON : button == Qt::RightButton ? VK_RBUTTON : button == Qt::MiddleButton ? VK_MBUTTON : 0;
        if (!vk) { continue; }
        if (hostKeyDown(static_cast<quint32>(vk))) {
            m_observedHostMouse.insert(button); m_missingHostKeys.remove(-button); continue;
        }
        if (!m_observedHostMouse.contains(button)) { continue; }
        if (!m_missingHostKeys.contains(-button)) { m_missingHostKeys.insert(-button); continue; }
        qWarning().noquote() << "input watchdog: missing mouse UP; repairing" << touchOwner(button);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(), static_cast<Qt::MouseButton>(button), Qt::NoButton, Qt::NoModifier);
        ++m_repairCount; m_lastRepair = touchOwner(button);
        processMouseClick(&release);
        m_missingHostKeys.remove(-button);
    }
#endif
    if (!m_gameMap) { return; }
    // Two successive OS-up samples prevent a transient state from cutting a hold.
    const auto keys = m_hostKeys.keys();
    for (int key : keys) {
        if (hostKeyDown(m_hostKeys.value(key))) { m_missingHostKeys.remove(key); continue; }
        if (!m_missingHostKeys.contains(key)) { m_missingHostKeys.insert(key); continue; }
        qWarning().noquote() << "input watchdog: missing key UP; repairing" << readableKey(key);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        ++m_repairCount; m_lastRepair = readableKey(key);
        keyEvent(&release, m_frameSize, m_showSize);
    }
    if (++m_auditTicks % 10 == 0) {
        QStringList active;
        for (int id = 0; id < MULTI_TOUCH_MAX_NUM; ++id) {
            if (m_touchActive[id]) { active << QString("%1:%2").arg(id).arg(touchOwner(m_multiTouchID[id])); }
        }
        if (!active.isEmpty()) { qInfo().noquote() << "input active touches=" << active.join(','); }
    }
}

void InputConvertGame::recordInput(const QString &event)
{
    m_inputHistory.record(event);
}

QVariantMap InputConvertGame::inputDiagnostics() const
{
    QStringList held, touches;
    for (int key : m_hostKeys.keys()) { held << readableKey(key); }
    for (int button : m_pressedMouseButtons) { held << touchOwner(button); }
    held.sort();
    for (int id = 0; id < MULTI_TOUCH_MAX_NUM; ++id) {
        if (m_touchActive[id]) {
            QString owner = m_multiTouchID[id] == m_ctrlSteerWheel.touchKey ? QStringLiteral("摇杆") : touchOwner(m_multiTouchID[id]);
            touches << QString("#%1 %2").arg(id).arg(owner);
        }
    }
    if (m_normalTouchDown) { touches << QStringLiteral("普通鼠标"); }
    return {{"heldKeys",held},{"history",m_inputHistory.labels()},{"historyEntries",m_inputHistory.entries()},{"held",held.isEmpty()?QStringLiteral("无"):held.join(" + ")},
            {"touches",touches.isEmpty()?QStringLiteral("无"):touches.join(", ")},
            {"repairs",m_repairCount},{"lastRepair",m_lastRepair},{"uiGapMs",m_lastUiGap},
            {"view",m_ctrlMouseMove.smallEyes?QStringLiteral("小眼睛"):m_ctrlMouseMove.touching?QStringLiteral("移动中"):QStringLiteral("已松开")}};
}
