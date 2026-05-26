#include "monitor.h"
#include "webdavserver.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#else
#  include <QFile>
#  include <QTextStream>
#  include <unistd.h>
#  include <sys/times.h>
#endif

#include <QVBoxLayout>
#include <QDateTime>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QPainter>
#include <QLegend>
#include <QThread>

// ---------------------------------------------------------------------------
Monitor::Monitor(QWidget *parent)
    : QWidget(parent)
{
    m_coreCount = QThread::idealThreadCount();
    if (m_coreCount < 1) m_coreCount = 1;
    setupUi();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Monitor::updateCharts);
}

Monitor::~Monitor() {}

void Monitor::setServer(WebDavServer *server) { m_server = server; }

void Monitor::startUpdates()
{
    if (!m_server) return;
    m_lastBytesSent     = m_server->bytesSent();
    m_lastBytesReceived = m_server->bytesReceived();

    // Save initial process times and wall‑time
#ifdef Q_OS_WIN
    FILETIME create, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) {
        m_prevProcKernelTime.LowPart  = kernel.dwLowDateTime;
        m_prevProcKernelTime.HighPart = kernel.dwHighDateTime;
        m_prevProcUserTime.LowPart    = user.dwLowDateTime;
        m_prevProcUserTime.HighPart   = user.dwHighDateTime;
    }
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    m_prevWallTime.LowPart  = ftNow.dwLowDateTime;
    m_prevWallTime.HighPart = ftNow.dwHighDateTime;
#else
    struct tms t;
    m_prevWallTicks = times(&t);
    m_prevProcTicks = t.tms_utime + t.tms_stime;
#endif

    m_timer->start(1000);
}

void Monitor::stopUpdates() { m_timer->stop(); }

// ---------------------------------------------------------------------------
void Monitor::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    // ── CPU chart ────────────────────────────────────────────────────────
    m_cpuChart = new QChart();
    m_cpuChart->setTitle("CPU Usage (process)");
    m_cpuChart->legend()->setVisible(true);
    m_cpuChart->legend()->setAlignment(Qt::AlignBottom);

    m_cpuSeries = new QLineSeries();
    m_cpuSeries->setName("CPU");
    m_cpuSeries->setColor(QColor(232, 17, 35));   // #E81123
    m_cpuSeries->setPen(QPen(QColor(232, 17, 35), 2));
    m_cpuChart->addSeries(m_cpuSeries);

    m_cpuAxisX = new QDateTimeAxis();
    m_cpuAxisX->setFormat("hh:mm:ss");
    m_cpuAxisX->setTitleText("Time");
    m_cpuChart->addAxis(m_cpuAxisX, Qt::AlignBottom);
    m_cpuSeries->attachAxis(m_cpuAxisX);

    m_cpuAxisY = new QValueAxis();
    m_cpuAxisY->setTitleText("% of all CPUs");
    m_cpuAxisY->setRange(0, 100);
    m_cpuAxisY->setLabelFormat("%.1f");
    m_cpuChart->addAxis(m_cpuAxisY, Qt::AlignLeft);
    m_cpuSeries->attachAxis(m_cpuAxisY);

    m_cpuChartView = new QChartView(m_cpuChart);
    m_cpuChartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(m_cpuChartView);

    // ── Memory chart ─────────────────────────────────────────────────────
    m_memoryChart = new QChart();
    m_memoryChart->setTitle("Memory Usage (MB)");
    m_memoryChart->legend()->setVisible(true);
    m_memoryChart->legend()->setAlignment(Qt::AlignBottom);

    m_memorySeries = new QLineSeries();
    m_memorySeries->setName("RAM");
    m_memorySeries->setColor(QColor(0, 120, 212));
    m_memorySeries->setPen(QPen(QColor(0, 120, 212), 2));
    m_memoryChart->addSeries(m_memorySeries);

    m_memoryAxisX = new QDateTimeAxis();
    m_memoryAxisX->setFormat("hh:mm:ss");
    m_memoryAxisX->setTitleText("Time");
    m_memoryChart->addAxis(m_memoryAxisX, Qt::AlignBottom);
    m_memorySeries->attachAxis(m_memoryAxisX);

    m_memoryAxisY = new QValueAxis();
    m_memoryAxisY->setTitleText("MB");
    m_memoryAxisY->setLabelFormat("%.1f");
    m_memoryChart->addAxis(m_memoryAxisY, Qt::AlignLeft);
    m_memorySeries->attachAxis(m_memoryAxisY);

    m_memoryChartView = new QChartView(m_memoryChart);
    m_memoryChartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(m_memoryChartView);

    // ── Network chart ────────────────────────────────────────────────────
    m_networkChart = new QChart();
    m_networkChart->setTitle("Network Activity (MB/s)");
    m_networkChart->legend()->setVisible(true);
    m_networkChart->legend()->setAlignment(Qt::AlignBottom);

    m_inboundSeries = new QLineSeries();
    m_inboundSeries->setName("Download");
    m_inboundSeries->setColor(QColor(0, 120, 212));
    m_inboundSeries->setPen(QPen(QColor(0, 120, 212), 2));
    m_networkChart->addSeries(m_inboundSeries);

    m_outboundSeries = new QLineSeries();
    m_outboundSeries->setName("Upload");
    m_outboundSeries->setColor(QColor(232, 17, 35));
    m_outboundSeries->setPen(QPen(QColor(232, 17, 35), 2));
    m_networkChart->addSeries(m_outboundSeries);

    m_networkAxisX = new QDateTimeAxis();
    m_networkAxisX->setFormat("hh:mm:ss");
    m_networkAxisX->setTitleText("Time");
    m_networkChart->addAxis(m_networkAxisX, Qt::AlignBottom);
    m_inboundSeries->attachAxis(m_networkAxisX);
    m_outboundSeries->attachAxis(m_networkAxisX);

    m_networkAxisY = new QValueAxis();
    m_networkAxisY->setTitleText("MB/s");
    m_networkAxisY->setLabelFormat("%.2f");
    m_networkChart->addAxis(m_networkAxisY, Qt::AlignLeft);
    m_inboundSeries->attachAxis(m_networkAxisY);
    m_outboundSeries->attachAxis(m_networkAxisY);

    m_networkChartView = new QChartView(m_networkChart);
    m_networkChartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(m_networkChartView);
}

// ---------------------------------------------------------------------------
double Monitor::getCpuUsagePercent()
{
#ifdef Q_OS_WIN
    FILETIME create, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user))
        return 0.0;

    ULARGE_INTEGER kern, usr;
    kern.LowPart  = kernel.dwLowDateTime;
    kern.HighPart = kernel.dwHighDateTime;
    usr.LowPart   = user.dwLowDateTime;
    usr.HighPart  = user.dwHighDateTime;

    ULONGLONG procDelta = (kern.QuadPart - m_prevProcKernelTime.QuadPart)
                          + (usr.QuadPart  - m_prevProcUserTime.QuadPart);

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULARGE_INTEGER wallNow;
    wallNow.LowPart  = ftNow.dwLowDateTime;
    wallNow.HighPart = ftNow.dwHighDateTime;
    ULONGLONG wallDelta = wallNow.QuadPart - m_prevWallTime.QuadPart;

    m_prevProcKernelTime = kern;
    m_prevProcUserTime   = usr;
    m_prevWallTime       = wallNow;

    if (wallDelta == 0) return 0.0;

    // Percent of a single core
    double percentSingleCore = 100.0 * procDelta / wallDelta;
    // Convert to percentage of total capacity of all cores (as in Task Manager)
    return percentSingleCore / m_coreCount;
#else
    struct tms t;
    clock_t wallNow = times(&t);
    clock_t procNow = t.tms_utime + t.tms_stime;

    clock_t wallDelta = wallNow - m_prevWallTicks;
    clock_t procDelta = procNow - m_prevProcTicks;
    m_prevWallTicks = wallNow;
    m_prevProcTicks = procNow;

    if (wallDelta <= 0) return 0.0;
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;
    double percentSingleCore = 100.0 * procDelta / wallDelta;
    return percentSingleCore / m_coreCount;
#endif
}

// ---------------------------------------------------------------------------
void Monitor::updateCharts()
{
    if (!m_server) return;

    QDateTime now = QDateTime::currentDateTime();
    qint64 cutoff = now.addSecs(-60).toMSecsSinceEpoch();

    // ── 1. CPU (process) ─────────────────────────────────────────────────
    double cpuPercent = getCpuUsagePercent();
    m_cpuSeries->append(now.toMSecsSinceEpoch(), cpuPercent);
    while (m_cpuSeries->count() > 0 && m_cpuSeries->at(0).x() < cutoff)
        m_cpuSeries->removePoints(0, 1);
    m_cpuAxisX->setRange(now.addSecs(-60), now);
    m_cpuAxisY->setRange(0, 100);
    m_cpuSeries->setName(QString("CPU: %1%").arg(cpuPercent, 0, 'f', 1));

    // ── 2. Memory ────────────────────────────────────────────────────────
    qreal memMB = getProcessMemoryMB();
    m_memorySeries->append(now.toMSecsSinceEpoch(), memMB);
    while (m_memorySeries->count() > 0 && m_memorySeries->at(0).x() < cutoff)
        m_memorySeries->removePoints(0, 1);
    m_memoryAxisX->setRange(now.addSecs(-60), now);
    qreal maxMem = memMB;
    for (int i = 0; i < m_memorySeries->count(); ++i)
        if (m_memorySeries->at(i).y() > maxMem) maxMem = m_memorySeries->at(i).y();
    m_memoryAxisY->setRange(0, qMax(maxMem + 10, 50.0));
    m_memorySeries->setName(QString("RAM: %1 MB").arg(memMB, 0, 'f', 1));

    // ── 3. Network ───────────────────────────────────────────────────────
    qint64 bytesSent     = m_server->bytesSent();
    qint64 bytesReceived = m_server->bytesReceived();
    qreal downloadRate = (bytesReceived - m_lastBytesReceived) / (1024.0 * 1024.0);
    qreal uploadRate   = (bytesSent     - m_lastBytesSent)     / (1024.0 * 1024.0);
    m_lastBytesReceived = bytesReceived;
    m_lastBytesSent     = bytesSent;

    m_inboundSeries->append(now.toMSecsSinceEpoch(), downloadRate);
    m_outboundSeries->append(now.toMSecsSinceEpoch(), uploadRate);
    while (m_inboundSeries->count() > 0 && m_inboundSeries->at(0).x() < cutoff)
        m_inboundSeries->removePoints(0, 1);
    while (m_outboundSeries->count() > 0 && m_outboundSeries->at(0).x() < cutoff)
        m_outboundSeries->removePoints(0, 1);
    m_networkAxisX->setRange(now.addSecs(-60), now);
    qreal maxNet = 0;
    for (int i = 0; i < m_inboundSeries->count(); ++i) {
        qreal v = m_inboundSeries->at(i).y();
        if (v > maxNet) maxNet = v;
    }
    for (int i = 0; i < m_outboundSeries->count(); ++i) {
        qreal v = m_outboundSeries->at(i).y();
        if (v > maxNet) maxNet = v;
    }
    m_networkAxisY->setRange(0, qMax(maxNet + 1.0, 5.0));
    m_inboundSeries->setName(QString("Download: %1 MB/s").arg(downloadRate, 0, 'f', 2));
    m_outboundSeries->setName(QString("Upload: %1 MB/s").arg(uploadRate, 0, 'f', 2));
}

// ---------------------------------------------------------------------------
qint64 Monitor::getProcessMemoryMB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             (PROCESS_MEMORY_COUNTERS*)&pmc,
                             sizeof(pmc)))
        return static_cast<qint64>(pmc.PrivateUsage / (1024 * 1024));
    return 0;
#else
    QFile file("/proc/self/status");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("VmRSS:")) {
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    bool ok;
                    qint64 kb = parts[1].toLongLong(&ok);
                    if (ok) return kb / 1024;
                }
            }
        }
    }
    return 0;
#endif
}