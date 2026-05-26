QT += core gui network widgets charts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET   = BetterWebDAVServer
TEMPLATE = app
#RC_ICONS = icons/icon.ico
SOURCES += \
    main.cpp         \
    mainwindow.cpp   \
    settings.cpp \
    webdavserver.cpp \
    webdavworker.cpp \
    davhandlers.cpp  \
    davutils.cpp     \
    httputils.cpp    \
    filestreamer.cpp \
    monitor.cpp

HEADERS += \
    mainwindow.h     \
    settings.h \
    webdavserver.h   \
    webdavworker.h   \
    davhandlers.h    \
    davutils.h       \
    httputils.h      \
    filestreamer.h   \
    monitor.h

win32 {
    VERSION = 1.0.0.0
    QMAKE_TARGET_DESCRIPTION = "Better WebDAV Server"
    QMAKE_TARGET_COMPANY     = "Better WebDAV"
    QMAKE_TARGET_PRODUCT     = "Better WebDAV Server"
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target