#include "my_label.h"
#include <QPushButton>
#include <QResizeEvent>
#include <QFocusEvent>
#include <QTouchEvent>
#include <QPainter>
#include <QtGlobal>

namespace {
struct KeyMap { int flap, brake, thrust, shoot; };
const KeyMap kKeys[my_label::MAX_PLAYERS] = {
    { Qt::Key_Space, Qt::Key_Left, Qt::Key_Right, Qt::Key_Alt },
    { Qt::Key_W,     Qt::Key_A,    Qt::Key_D,     Qt::Key_S   },
    { Qt::Key_I,     Qt::Key_J,    Qt::Key_L,     Qt::Key_K   },
    { Qt::Key_T,     Qt::Key_F,    Qt::Key_H,     Qt::Key_G   },
};
}

my_label::my_label(QWidget *parent) : QLabel(parent)
{
    // This custom label captures user input for the game area itself.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
#ifdef Q_OS_ANDROID
    // On-screen controls are children of the game surface, so the original
    // renderer, sprites, and game simulation remain unchanged.
    const QString buttonStyle = QStringLiteral(
        "QPushButton { color: white; background: rgba(18, 28, 38, 185); "
        "border: 2px solid rgba(255,255,255,190); border-radius: 16px; "
        "font-weight: bold; font-size: 14px; padding: 4px; }"
        "QPushButton:pressed { background: rgba(70, 130, 180, 225); }");
    auto makeButton = [this, &buttonStyle](const QString &text) {
        auto *button = new QPushButton(text, this);
        button->setStyleSheet(buttonStyle);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRepeat(false);
        button->setVisible(false);
        return button;
    };
    m_touchBrake = makeButton(QStringLiteral("BRAKE"));
    m_touchShoot = makeButton(QStringLiteral("BLADE"));
    m_touchThrust = makeButton(QStringLiteral("BOOST"));
    for (QPushButton *button : {m_touchBrake, m_touchThrust, m_touchShoot})
        button->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_menuButton = makeButton(QStringLiteral("MENU"));
    m_menuButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: white; background: rgba(18, 28, 38, 210); "
        "border: 2px solid rgba(255,255,255,210); border-radius: 10px; "
        "font-weight: bold; font-size: 13px; padding: 4px; }"
        "QPushButton:pressed { background: rgba(70, 130, 180, 235); }"));

    connect(m_menuButton, &QPushButton::clicked, this, &my_label::menuRequested);
    layoutTouchControls();
#endif
}

void my_label::setTouchControlsVisible(bool visible)
{
    if (!visible) {
        const QList<int> ids = m_activeTouches.keys();
        for (int id : ids)
            updateTouchAction(id, QPointF(), false);
    }
    m_touchControlsVisible = visible;
#ifdef Q_OS_ANDROID
    setAttribute(Qt::WA_AcceptTouchEvents, visible);
    if (m_touchBrake) m_touchBrake->setVisible(visible);
    if (m_touchThrust) m_touchThrust->setVisible(visible);
    if (m_touchShoot) m_touchShoot->setVisible(visible);
#else
    Q_UNUSED(visible);
#endif
}

void my_label::setMenuButtonVisible(bool visible)
{
#ifdef Q_OS_ANDROID
    if (m_menuButton)
        m_menuButton->setVisible(visible);
#else
    Q_UNUSED(visible);
#endif
}

void my_label::layoutTouchControls()
{
#ifdef Q_OS_ANDROID
    if (!m_touchBrake || width() <= 0 || height() <= 0) return;
    const int margin = qMax(8, width() / 100);
    const int buttonW = qBound(62, width() / 8, 104);
    const int buttonH = qBound(48, height() / 7, 76);
    const int y = qMax(margin, height() - buttonH - margin);
    m_touchBrake->setGeometry(margin, y, buttonW, buttonH);
    m_touchShoot->setGeometry(width() - (buttonW * 2) - (margin * 2) - 12, y, buttonW + 12, buttonH);
    m_touchThrust->setGeometry(width() - buttonW - margin, y, buttonW, buttonH);
    const int menuW = qBound(60, width() / 12, 88);
    const int menuH = qBound(36, height() / 14, 48);
    m_menuButton->setGeometry(width() - menuW - margin, margin, menuW, menuH);
#endif
}

void my_label::updateTouchAction(int id, const QPointF &position, bool pressed)
{
    const TouchAction previous = m_activeTouches.value(id, TouchAction::None);
    TouchAction next = TouchAction::None;
#ifdef Q_OS_ANDROID
    if (pressed && m_touchControlsVisible) {
        if (m_touchBrake->geometry().contains(position.toPoint()))
            next = TouchAction::Brake;
        else if (m_touchThrust->geometry().contains(position.toPoint()))
            next = TouchAction::Thrust;
        else if (m_touchShoot->geometry().contains(position.toPoint()))
            next = TouchAction::Shoot;
    }
#else
    Q_UNUSED(position);
#endif

    if (pressed && previous == TouchAction::None && next == TouchAction::None)
        emit flap(0);

    if (previous != next) {
        if (previous == TouchAction::Brake) {
            m_touchBrake->setDown(false);
            emit brakeHeld(0, false);
        } else if (previous == TouchAction::Thrust) {
            m_touchThrust->setDown(false);
            emit thrustHeld(0, false);
        } else if (previous == TouchAction::Shoot) {
            m_touchShoot->setDown(false);
        }

        if (next == TouchAction::Brake) {
            m_touchBrake->setDown(true);
            emit brakeHeld(0, true);
        } else if (next == TouchAction::Thrust) {
            m_touchThrust->setDown(true);
            emit thrustHeld(0, true);
        } else if (next == TouchAction::Shoot) {
            m_touchShoot->setDown(true);
            emit shoot(0);
        }
    }

    if (pressed)
        m_activeTouches.insert(id, next);
    else
        m_activeTouches.remove(id);
}

bool my_label::event(QEvent *ev)
{
#ifdef Q_OS_ANDROID
    if (m_touchControlsVisible &&
        (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchUpdate ||
         ev->type() == QEvent::TouchEnd)) {
        auto *touchEvent = static_cast<QTouchEvent *>(ev);
        for (const QEventPoint &point : touchEvent->points()) {
            if (point.state() == QEventPoint::State::Pressed) {
                updateTouchAction(point.id(), point.position(), true);
            } else if (point.state() == QEventPoint::State::Updated) {
                updateTouchAction(point.id(), point.position(), true);
            } else if (point.state() == QEventPoint::State::Released) {
                updateTouchAction(point.id(), point.position(), false);
            }
        }
        ev->accept();
        return true;
    }
    if (m_touchControlsVisible && ev->type() == QEvent::TouchCancel) {
        const QList<int> ids = m_activeTouches.keys();
        for (int id : ids)
            updateTouchAction(id, QPointF(), false);
        ev->accept();
        return true;
    }
#endif
    return QLabel::event(ev);
}

void my_label::paintEvent(QPaintEvent *ev)
{
    const QPixmap pm = pixmap();
    if (pm.isNull()) {
        QLabel::paintEvent(ev);
        return;
    }
    QPainter painter(this);
    if (pm.size() == size()) {
        painter.drawPixmap(0, 0, pm);
    } else {
        painter.fillRect(rect(), QColor(8, 14, 10));
        // Smooth sampling: a large host world (e.g. a PC window) shrunk onto a phone
        // otherwise drops pixels and shimmers.
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QSize fitted = pm.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect targetRect((width() - fitted.width()) / 2,
                               (height() - fitted.height()) / 2,
                               fitted.width(), fitted.height());
        painter.drawPixmap(targetRect, pm);
    }
}

void my_label::resizeEvent(QResizeEvent *ev)
{
    QLabel::resizeEvent(ev);
    layoutTouchControls();
}

void my_label::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        updateTouchAction(-1, ev->position(), true);
        ev->accept();
        return;
    }
    QLabel::mousePressEvent(ev);
}

void my_label::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton && m_activeTouches.contains(-1)) {
        updateTouchAction(-1, ev->position(), false);
        ev->accept();
        return;
    }
    QLabel::mouseReleaseEvent(ev);
}

void my_label::keyPressEvent(QKeyEvent *ev)
{
    const int key = ev->key();
    for (int p = 0; p < MAX_PLAYERS; p++) {
        const KeyMap &k = kKeys[p];
        if (key == k.flap) {
            // Space repeats are allowed for player 1 (original behaviour);
            // letter keys ignore auto-repeat so holding one doesn't spam flaps.
            if (p == 0 || !ev->isAutoRepeat())
                emit flap(p);
            ev->accept();
            return;
        }
        if (key == k.shoot) {
            // Alt must be accepted so it isn't treated as a menu mnemonic.
            if (!ev->isAutoRepeat())
                emit shoot(p);
            ev->accept();
            return;
        }
        if (key == k.thrust) {
            if (!ev->isAutoRepeat())
                emit thrustHeld(p, true);
            ev->accept();
            return;
        }
        if (key == k.brake) {
            if (!ev->isAutoRepeat())
                emit brakeHeld(p, true);
            ev->accept();
            return;
        }
    }
    QLabel::keyPressEvent(ev);
}

void my_label::keyReleaseEvent(QKeyEvent *ev)
{
    const int key = ev->key();
    for (int p = 0; p < MAX_PLAYERS; p++) {
        const KeyMap &k = kKeys[p];
        if (key == k.thrust) {
            if (!ev->isAutoRepeat())
                emit thrustHeld(p, false);
            ev->accept();
            return;
        }
        if (key == k.brake) {
            if (!ev->isAutoRepeat())
                emit brakeHeld(p, false);
            ev->accept();
            return;
        }
    }
    QLabel::keyReleaseEvent(ev);
}

void my_label::focusOutEvent(QFocusEvent *ev)
{
    // Don't leave an arrow "stuck" if the window loses focus mid-press.
    const QList<int> ids = m_activeTouches.keys();
    for (int id : ids)
        updateTouchAction(id, QPointF(), false);
    for (int p = 0; p < MAX_PLAYERS; p++) {
        emit thrustHeld(p, false);
        emit brakeHeld(p, false);
    }
    QLabel::focusOutEvent(ev);
}
