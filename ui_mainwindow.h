/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.11.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <my_label.h>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QHBoxLayout *mainLayout;
    my_label *frame;
    QScrollArea *sideScrollArea;
    QWidget *sideContent;
    QVBoxLayout *sidePanel;
    QLabel *titleLabel;
    QLabel *scoreLabel;
    QLabel *highScoreLabel;
    QSpacerItem *verticalSpacer;
    QLabel *statusLabel;
    QPushButton *easyButton;
    QPushButton *hardButton;
    QPushButton *trollButton;
    QPushButton *multiplayerButton;
    QPushButton *localMultiplayerButton;
    QPushButton *hostLanButton;
    QPushButton *joinLanButton;
    QPushButton *multiplayerBackButton;
    QPushButton *settingsButton;
    QPushButton *restartButton;
    QSpacerItem *verticalSpacer2;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(900, 747);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        mainLayout = new QHBoxLayout(centralwidget);
        mainLayout->setObjectName("mainLayout");
        frame = new my_label(centralwidget);
        frame->setObjectName("frame");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(1);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(frame->sizePolicy().hasHeightForWidth());
        frame->setSizePolicy(sizePolicy);
        frame->setMinimumSize(QSize(0, 0));
        frame->setAlignment(Qt::AlignmentFlag::AlignCenter);

        mainLayout->addWidget(frame);

        sideScrollArea = new QScrollArea(centralwidget);
        sideScrollArea->setObjectName("sideScrollArea");
        sideScrollArea->setMinimumSize(QSize(130, 0));
        sideScrollArea->setFrameShape(QFrame::Shape::NoFrame);
        sideScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
        sideScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        sideScrollArea->setWidgetResizable(true);
        sideContent = new QWidget();
        sideContent->setObjectName("sideContent");
        sideContent->setGeometry(QRect(0, 0, 251, 763));
        sidePanel = new QVBoxLayout(sideContent);
        sidePanel->setSpacing(15);
        sidePanel->setObjectName("sidePanel");
        titleLabel = new QLabel(sideContent);
        titleLabel->setObjectName("titleLabel");
        QFont font;
        font.setPointSize(16);
        font.setBold(true);
        titleLabel->setFont(font);
        titleLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        sidePanel->addWidget(titleLabel);

        scoreLabel = new QLabel(sideContent);
        scoreLabel->setObjectName("scoreLabel");
        QFont font1;
        font1.setPointSize(14);
        font1.setBold(true);
        scoreLabel->setFont(font1);
        scoreLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        sidePanel->addWidget(scoreLabel);

        highScoreLabel = new QLabel(sideContent);
        highScoreLabel->setObjectName("highScoreLabel");
        QFont font2;
        font2.setPointSize(12);
        highScoreLabel->setFont(font2);
        highScoreLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        sidePanel->addWidget(highScoreLabel);

        verticalSpacer = new QSpacerItem(0, 0, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        sidePanel->addItem(verticalSpacer);

        statusLabel = new QLabel(sideContent);
        statusLabel->setObjectName("statusLabel");
        QFont font3;
        font3.setPointSize(10);
        statusLabel->setFont(font3);
        statusLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);
        statusLabel->setWordWrap(true);

        sidePanel->addWidget(statusLabel);

        easyButton = new QPushButton(sideContent);
        easyButton->setObjectName("easyButton");
        easyButton->setMinimumSize(QSize(120, 40));
        easyButton->setFont(font2);

        sidePanel->addWidget(easyButton);

        hardButton = new QPushButton(sideContent);
        hardButton->setObjectName("hardButton");
        hardButton->setMinimumSize(QSize(120, 40));
        hardButton->setFont(font2);

        sidePanel->addWidget(hardButton);

        trollButton = new QPushButton(sideContent);
        trollButton->setObjectName("trollButton");
        trollButton->setMinimumSize(QSize(120, 40));
        trollButton->setFont(font2);

        sidePanel->addWidget(trollButton);

        multiplayerButton = new QPushButton(sideContent);
        multiplayerButton->setObjectName("multiplayerButton");
        multiplayerButton->setMinimumSize(QSize(120, 40));
        multiplayerButton->setFont(font2);

        sidePanel->addWidget(multiplayerButton);

        localMultiplayerButton = new QPushButton(sideContent);
        localMultiplayerButton->setObjectName("localMultiplayerButton");
        localMultiplayerButton->setMinimumSize(QSize(120, 40));
        localMultiplayerButton->setFont(font2);

        sidePanel->addWidget(localMultiplayerButton);

        hostLanButton = new QPushButton(sideContent);
        hostLanButton->setObjectName("hostLanButton");
        hostLanButton->setMinimumSize(QSize(120, 40));
        hostLanButton->setFont(font2);

        sidePanel->addWidget(hostLanButton);

        joinLanButton = new QPushButton(sideContent);
        joinLanButton->setObjectName("joinLanButton");
        joinLanButton->setMinimumSize(QSize(120, 40));
        joinLanButton->setFont(font2);

        sidePanel->addWidget(joinLanButton);

        multiplayerBackButton = new QPushButton(sideContent);
        multiplayerBackButton->setObjectName("multiplayerBackButton");
        multiplayerBackButton->setMinimumSize(QSize(120, 40));
        multiplayerBackButton->setFont(font2);

        sidePanel->addWidget(multiplayerBackButton);

        settingsButton = new QPushButton(sideContent);
        settingsButton->setObjectName("settingsButton");
        settingsButton->setMinimumSize(QSize(120, 40));
        settingsButton->setFont(font2);

        sidePanel->addWidget(settingsButton);

        restartButton = new QPushButton(sideContent);
        restartButton->setObjectName("restartButton");
        restartButton->setMinimumSize(QSize(120, 40));
        restartButton->setFont(font2);

        sidePanel->addWidget(restartButton);

        verticalSpacer2 = new QSpacerItem(0, 0, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        sidePanel->addItem(verticalSpacer2);

        sideScrollArea->setWidget(sideContent);

        mainLayout->addWidget(sideScrollArea);

        MainWindow->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "Birds vs Plants", nullptr));
        frame->setText(QString());
        titleLabel->setText(QCoreApplication::translate("MainWindow", "BIRDS VS PLANTS", nullptr));
        scoreLabel->setText(QCoreApplication::translate("MainWindow", "Score: 0", nullptr));
        highScoreLabel->setText(QCoreApplication::translate("MainWindow", "Best: 0", nullptr));
        statusLabel->setText(QCoreApplication::translate("MainWindow", "SPACE skip", nullptr));
        easyButton->setText(QCoreApplication::translate("MainWindow", "Easy", nullptr));
        hardButton->setText(QCoreApplication::translate("MainWindow", "Hard", nullptr));
        trollButton->setText(QCoreApplication::translate("MainWindow", "Troll Mode", nullptr));
        multiplayerButton->setText(QCoreApplication::translate("MainWindow", "Multiplayer", nullptr));
        localMultiplayerButton->setText(QCoreApplication::translate("MainWindow", "Local (same keyboard)", nullptr));
        hostLanButton->setText(QCoreApplication::translate("MainWindow", "Host LAN", nullptr));
        joinLanButton->setText(QCoreApplication::translate("MainWindow", "Join LAN", nullptr));
        multiplayerBackButton->setText(QCoreApplication::translate("MainWindow", "Back", nullptr));
        settingsButton->setText(QCoreApplication::translate("MainWindow", "Settings", nullptr));
        restartButton->setText(QCoreApplication::translate("MainWindow", "Restart", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
