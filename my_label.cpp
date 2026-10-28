#include "my_label.h"
#include <QPainter>

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
}

void my_label::paintEvent(QPaintEvent *ev)
{
    const QPixmap pm = pixmap();
    if (pm.isNull()) {
        QLabel::paintEvent(ev);
        return;
    }

    // The game grid is floor(width / cell), so on the host the picture is normally
    // within a few pixels of the widget. Let QLabel draw that case exactly as it
    // always has: no resampling, no change to how the picture looks.
    const int slackW = width() - pm.width();
    const int slackH = height() - pm.height();
    if (slackW >= 0 && slackH >= 0 && slackW < 8 && slackH < 8) {
        QLabel::paintEvent(ev);
        return;
    }

    // Otherwise (e.g. a LAN joinee showing the host's differently sized world) fit the
    // picture to the widget with its aspect ratio kept, letterboxed. Never crop it and
    // never let it resize the window.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 14, 10));

    const QSize fitted = pm.size().scaled(size(), Qt::KeepAspectRatio);
    if (fitted.isEmpty()) return;
    const QPoint topLeft((width() - fitted.width()) / 2, (height() - fitted.height()) / 2);

    const qreal dpr = devicePixelRatioF();
    const QSize deviceSize(qRound(fitted.width() * dpr), qRound(fitted.height() * dpr));
    if (deviceSize.width() < pm.width() || deviceSize.height() < pm.height()) {
        // Shrinking: plain bilinear sampling drops pixels and shimmers, so resample
        // with proper area averaging, straight to device pixels so it stays crisp.
        QPixmap scaled = pm.scaled(deviceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        painter.drawPixmap(topLeft, scaled);
    } else {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(QRect(topLeft, fitted), pm);
    }
}

void my_label::mousePressEvent(QMouseEvent *ev)
{
    // Click anywhere in the game frame to trigger a flap action (player 1).
    if (ev->button() == Qt::LeftButton) {
        emit flap(0);
    }
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
    for (int p = 0; p < MAX_PLAYERS; p++) {
        emit thrustHeld(p, false);
        emit brakeHeld(p, false);
    }
    QLabel::focusOutEvent(ev);
}
