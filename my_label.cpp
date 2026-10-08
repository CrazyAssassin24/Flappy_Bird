#include "my_label.h"

my_label::my_label(QWidget *parent) : QLabel(parent)
{
    // This custom label captures user input for the game area itself.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

void my_label::mousePressEvent(QMouseEvent *ev)
{
    // Click anywhere in the game frame to trigger a flap action.
    if (ev->button() == Qt::LeftButton) {
        emit flap();
    }
}

void my_label::keyPressEvent(QKeyEvent *ev)
{
    // Space is the main input for both gameplay and story skipping.
    if (ev->key() == Qt::Key_Space) {
        emit flap();
    } else if (ev->key() == Qt::Key_Alt) {
        // Alt fires the feather blade. Ignore auto-repeat and accept the event
        // so Alt is not treated as a menu mnemonic.
        if (!ev->isAutoRepeat())
            emit shoot();
        ev->accept();
    } else {
        QLabel::keyPressEvent(ev);
    }
}
