QT       += core gui widgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    webdavserver.cpp \
    webdavworker.cpp

HEADERS += \
    mainwindow.h \
    webdavserver.h \
    webdavworker.h