#ifndef MY_LABEL_H
#define MY_LABEL_H

#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QHash>
class QPushButton;

// Game-area widget. Turns keyboard / mouse input into per-player signals.
//
//   Player 1: Space (or left click) flap, Left brake, Right thrust, Alt feather
//   Player 2: W flap, A brake, D thrust, S feather
//   Player 3: I flap, J brake, L thrust, K feather
//   Player 4: T flap, F brake, H thrust, G feather
class my_label : public QLabel
{
    Q_OBJECT
public:
    static constexpr int MAX_PLAYERS = 4;
    explicit my_label(QWidget *parent = nullptr);
    void setTouchControlsVisible(bool visible);
    void setMenuButtonVisible(bool visible);
    QSize sizeHint() const override { return QSize(320, 240); }
    QSize minimumSizeHint() const override { return QSize(1, 1); }

protected:
    void paintEvent(QPaintEvent *ev) override;
    bool event(QEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void focusOutEvent(QFocusEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private:
    enum class TouchAction { None, Brake, Thrust, Shoot };
    void layoutTouchControls();
    void updateTouchAction(int id, const QPointF &position, bool pressed);
    QPushButton *m_touchBrake = nullptr;
    QPushButton *m_touchThrust = nullptr;
    QPushButton *m_touchShoot = nullptr;
    QPushButton *m_menuButton = nullptr;
    bool m_touchControlsVisible = false;
    QHash<int, TouchAction> m_activeTouches;

signals:
    void flap(int player);
    void shoot(int player);
    void thrustHeld(int player, bool held);   // accelerate
    void brakeHeld(int player, bool held);    // reverse flap / brake
    void menuRequested();
};

#endif // MY_LABEL_H
