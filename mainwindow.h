#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include <QSpinBox>
#include <QStackedWidget>

class Monitor;
class Settings;
class WebDavServer;

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

    void showMonitor();
    void showLogs();
    void showSettings();

private:
    void setupTopToolbar();
    void setupLogPage();
    void setupBottomToolbar();
    void applyStyles();

    QToolBar    *m_topToolbar   = nullptr;
    QToolButton *m_burgerButton = nullptr;
    QLineEdit   *m_pathEdit     = nullptr;
    QSpinBox    *m_portSpinBox  = nullptr;
    QPushButton *m_btnBrowse    = nullptr;
    QPushButton *m_btnStart     = nullptr;
    QPushButton *m_btnStop      = nullptr;
    QToolButton *m_btnMenu      = nullptr;

    QStackedWidget *m_stack     = nullptr;
    Monitor        *m_monitor   = nullptr;
    QTextEdit      *m_logEdit   = nullptr;
    Settings       *m_settings  = nullptr;

    QToolBar *m_bottomToolbar = nullptr;
    QLabel   *m_statusLabel   = nullptr;

    WebDavServer *m_server = nullptr;
    bool m_serverRunning = false;
};