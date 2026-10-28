QT       += core gui multimedia multimediawidgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# Android packaging: Qt Creator uses this directory for the manifest and app metadata.
android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_PACKAGE_NAME = org.birdsvsplants.game
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    my_label.cpp \
    gamesettings.cpp \
    lan_network.cpp

HEADERS += \
    mainwindow.h \
    my_label.h \
    gamesettings.h \
    lan_network.h

FORMS += \
    mainwindow.ui

RESOURCES += comic.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
