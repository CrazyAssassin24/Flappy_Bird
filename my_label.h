#ifndef MY_LABEL_H
#define MY_LABEL_H

#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>

class my_label : public QLabel
{
    Q_OBJECT
public:
    explicit my_label(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

signals:
    void flap();
    void shoot();
};

#endif // MY_LABEL_H
