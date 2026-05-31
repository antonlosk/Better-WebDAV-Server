#pragma once

#include <QWidget>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

class WebDavServer;

class Monitor : public QWidget
{
    Q_OBJECT
public:
    explicit Monitor(QWidget *parent = nullptr);
    ~Monitor();

    void setServer(WebDavServer *server);
    void startUpdates();
    void stopUpdates();

    static qint64 getProcessMemoryMB();

    void setDarkMode(bool dark);   // переключает оформление графиков

private slots:
    void updateCharts();

private:
    void setupUi();
    double getCpuUsagePercent();   // возвращает загрузку CPU в % (0..100)

    // CPU chart
    QChartView    *m_cpuChartView = nullptr;
    QChart        *m_cpuChart     = nullptr;
    QLineSeries   *m_cpuSeries    = nullptr;
    QDateTimeAxis *m_cpuAxisX     = nullptr;
    QValueAxis    *m_cpuAxisY     = nullptr;

    // Memory chart
    QChartView    *m_memoryChartView = nullptr;
    QChart        *m_memoryChart     = nullptr;
    QLineSeries   *m_memorySeries    = nullptr;
    QDateTimeAxis *m_memoryAxisX     = nullptr;
    QValueAxis    *m_memoryAxisY     = nullptr;

    // Network chart
    QChartView    *m_networkChartView = nullptr;
    QChart        *m_networkChart     = nullptr;
    QLineSeries   *m_inboundSeries    = nullptr;   // Download
    QLineSeries   *m_outboundSeries   = nullptr;   // Upload
    QDateTimeAxis *m_networkAxisX     = nullptr;
    QValueAxis    *m_networkAxisY     = nullptr;

    QTimer         *m_timer  = nullptr;
    WebDavServer   *m_server = nullptr;

    qint64 m_lastBytesSent     = 0;
    qint64 m_lastBytesReceived = 0;

    bool m_darkMode = false;   // текущий режим

    // Данные для подсчёта CPU
#ifdef Q_OS_WIN
    ULARGE_INTEGER m_prevProcKernelTime{};
    ULARGE_INTEGER m_prevProcUserTime{};
    ULARGE_INTEGER m_prevWallTime{};
#else
    clock_t m_prevProcTicks  = 0;
    clock_t m_prevWallTicks  = 0;
#endif
    int m_coreCount = 0;
};