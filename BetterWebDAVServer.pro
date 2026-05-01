QT += core gui network widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET   = BetterWebDAVServer
TEMPLATE = app

SOURCES += \
    main.cpp         \
    mainwindow.cpp   \
    webdavserver.cpp \
    webdavworker.cpp \
    davhandlers.cpp  \
    davutils.cpp     \
    httputils.cpp    \
    filestreamer.cpp

HEADERS += \
    mainwindow.h     \
    webdavserver.h   \
    webdavworker.h   \
    davhandlers.h    \
    davutils.h       \
    httputils.h      \
    filestreamer.h

win32 {
    VERSION = 1.0.0.0
    QMAKE_TARGET_DESCRIPTION = "Better WebDAV Server"
    QMAKE_TARGET_COMPANY     = "Better WebDAV"
    QMAKE_TARGET_PRODUCT     = "Better WebDAV Server"
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target