#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QToolButton>
#include <QSpinBox>
#include <QFrame>

#include "webdavserver.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onBrowse();
    void onStart();
    void onStop();
    void onQuit();
    void onLogMessage(const QString &msg, const QString &level);
    void onServerStarted(quint16 port);
    void onServerStartFailed(const QString &reason);
    void onServerStopped();

private:
    void setupTopToolbar();
    void setupLogArea();
    void setupBottomToolbar();
    void applyStyles();

    // Top toolbar
    QToolBar   *m_topToolbar   = nullptr;
    QLineEdit  *m_pathEdit     = nullptr;
    QSpinBox   *m_portSpinBox  = nullptr;
    QPushButton *m_btnBrowse   = nullptr;
    QPushButton *m_btnStart    = nullptr;
    QPushButton *m_btnStop     = nullptr;
    QToolButton *m_btnMenu     = nullptr;

    // Log
    QTextEdit  *m_logEdit      = nullptr;

    // Bottom toolbar
    QToolBar   *m_bottomToolbar = nullptr;
    QLabel     *m_statusLabel   = nullptr;

    // Server
    WebDavServer *m_server = nullptr;

    // State
    bool m_serverRunning = false;
};