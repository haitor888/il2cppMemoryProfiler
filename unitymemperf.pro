#-------------------------------------------------
#
# Project created by QtCreator 2019-10-28T10:14:41
#
#-------------------------------------------------

QT       += core gui opengl network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = UnityMemPerf
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

INCLUDEPATH += $$PWD/include $$PWD/src

SOURCES += \
        src/adbprocess.cpp \
        src/detailswidget.cpp \
        src/globalLog.cpp \
        src/main.cpp \
        src/mainwindow.cpp \
        src/startappprocess.cpp \
        src/remoteprocess.cpp \
        src/umpcrawler.cpp \
        src/umpmodel.cpp

HEADERS += \
        include/adbprocess.h \
        include/detailswidget.h \
        include/globalLog.h \
        include/umpcrawler.h \
        include/umpmemory.h \
        include/umpmodel.h \
        include/mainwindow.h \
        include/startappprocess.h \
        include/remoteprocess.h

FORMS += \
        detailswidget.ui \
        mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res/icon.qrc

RC_ICONS = res/devices.ico
ICON = res/devices.icns
