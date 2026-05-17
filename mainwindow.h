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
#include <QStackedWidget>
#include <QTimer>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>

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

    void showDashboard();
    void showLogs();
    void updateDashboard();

private:
    void setupTopToolbar();
    void setupDashboardPage();
    void setupLogPage();
    void setupBottomToolbar();
    void applyStyles();

    // Top toolbar
    QToolBar    *m_topToolbar   = nullptr;
    QToolButton *m_burgerButton = nullptr;
    QLineEdit   *m_pathEdit     = nullptr;
    QSpinBox    *m_portSpinBox  = nullptr;
    QPushButton *m_btnBrowse    = nullptr;
    QPushButton *m_btnStart     = nullptr;
    QPushButton *m_btnStop      = nullptr;
    QToolButton *m_btnMenu      = nullptr;

    // Central stack
    QStackedWidget *m_stack = nullptr;

    // Dashboard
    QWidget         *m_dashboardPage   = nullptr;
    QChartView      *m_memoryChartView = nullptr;
    QChart          *m_memoryChart     = nullptr;
    QLineSeries     *m_memorySeries    = nullptr;
    QDateTimeAxis   *m_memoryAxisX     = nullptr;
    QValueAxis      *m_memoryAxisY     = nullptr;

    QChartView      *m_networkChartView = nullptr;
    QChart          *m_networkChart     = nullptr;
    QLineSeries     *m_networkSeries    = nullptr;
    QDateTimeAxis   *m_networkAxisX     = nullptr;
    QValueAxis      *m_networkAxisY     = nullptr;

    QTimer *m_dashboardTimer = nullptr;

    // Log
    QTextEdit *m_logEdit = nullptr;

    // Bottom toolbar
    QToolBar *m_bottomToolbar = nullptr;
    QLabel   *m_statusLabel   = nullptr;

    // Server
    WebDavServer *m_server = nullptr;

    // State
    bool m_serverRunning = false;

    // Network statistics tracking
    qint64 m_lastBytesSent = 0;
    qint64 m_lastBytesReceived = 0;

    qint64 getProcessMemoryMB();
};