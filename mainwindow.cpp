#include "mainwindow.h"
#include "monitor.h"
#include "settings.h"
#include "webdavserver.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QScrollBar>
#include <QSizePolicy>
#include <QDir>
#include <QFont>
#include <QMenu>
#include <QScrollArea>

// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Better WebDAV Server");
    setMinimumSize(820, 560);
    resize(960, 640);

    m_server = new WebDavServer(this);

    connect(m_server, &WebDavServer::logMessage,
            this,     &MainWindow::onLogMessage);
    connect(m_server, &WebDavServer::serverStarted,
            this,     &MainWindow::onServerStarted);
    connect(m_server, &WebDavServer::serverStartFailed,
            this,     &MainWindow::onServerStartFailed);
    connect(m_server, &WebDavServer::serverStopped,
            this,     &MainWindow::onServerStopped);

    setupTopToolbar();
    setupLogPage();

    m_monitor = new Monitor(this);
    m_monitor->setServer(m_server);
    m_monitor->setMinimumHeight(800);   // чтобы графики не сжимались

    // Обернём монитор в QScrollArea
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(m_monitor);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_settings = new Settings(this);
    m_settings->setServer(m_server);

    setupBottomToolbar();
    applyStyles();

    QFont appFont = QApplication::font();
    appFont.setFamily("Segoe UI");
    appFont.setPointSize(9);
    QApplication::setFont(appFont);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(scrollArea);   // index 0 – Monitor
    m_stack->addWidget(m_logEdit);    // index 1 – Logs
    m_stack->addWidget(m_settings);   // index 2 – Settings
    setCentralWidget(m_stack);
    m_stack->setCurrentIndex(0);      // открываем Monitor по умолчанию

    onLogMessage("Better WebDAV Server is ready.", "INFO");
    onLogMessage("Set a root directory and click Start.", "INFO");
}

MainWindow::~MainWindow() {}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupTopToolbar()
{
    m_topToolbar = new QToolBar(this);
    m_topToolbar->setObjectName("topToolbar");
    m_topToolbar->setMovable(false);
    m_topToolbar->setFloatable(false);
    m_topToolbar->setIconSize(QSize(18, 18));
    addToolBar(Qt::TopToolBarArea, m_topToolbar);

    // ── Burger menu button (☰) ────────────────────────────────────────────────
    m_burgerButton = new QToolButton(this);
    m_burgerButton->setObjectName("burgerButton");
    m_burgerButton->setText("\u2630"); // ☰
    m_burgerButton->setToolTip("Menu");
    m_burgerButton->setFixedSize(36, 30);
    m_burgerButton->setCursor(Qt::PointingHandCursor);
    m_burgerButton->setPopupMode(QToolButton::InstantPopup);

    QMenu *burgerMenu = new QMenu(m_burgerButton);
    burgerMenu->setObjectName("burgerMenu");

    QAction *monitorAct  = burgerMenu->addAction("Monitor");
    QAction *logAct      = burgerMenu->addAction("Logs");
    QAction *settingsAct = burgerMenu->addAction("Settings");

    connect(monitorAct,  &QAction::triggered, this, &MainWindow::showMonitor);
    connect(logAct,      &QAction::triggered, this, &MainWindow::showLogs);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::showSettings);
    m_burgerButton->setMenu(burgerMenu);

    // ── Path ─────────────────────────────────────────────────────────────────
    QLabel *pathIcon = new QLabel("  Path:", this);
    pathIcon->setObjectName("toolLabel");

    m_pathEdit = new QLineEdit("C:/", this);
    m_pathEdit->setObjectName("pathEdit");
    m_pathEdit->setPlaceholderText("Server root directory...");
    m_pathEdit->setMinimumWidth(260);
    m_pathEdit->setMaximumWidth(400);
    m_pathEdit->setToolTip("WebDAV server root directory");

    m_btnBrowse = new QPushButton("Browse", this);
    m_btnBrowse->setObjectName("btnBrowse");
    m_btnBrowse->setToolTip("Select directory");
    m_btnBrowse->setCursor(Qt::PointingHandCursor);
    m_btnBrowse->setFixedHeight(30);

    // ── Port ─────────────────────────────────────────────────────────────────
    QLabel *portLabel = new QLabel("  Port:", this);
    portLabel->setObjectName("toolLabel");

    m_portSpinBox = new QSpinBox(this);
    m_portSpinBox->setObjectName("portSpinBox");
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(80);
    m_portSpinBox->setFixedWidth(75);
    m_portSpinBox->setFixedHeight(30);
    m_portSpinBox->setToolTip("WebDAV server port (default 80)");

    // ── Start / stop buttons ─────────────────────────────────────────────────
    m_btnStart = new QPushButton("  Start", this);
    m_btnStart->setObjectName("btnStart");
    m_btnStart->setCursor(Qt::PointingHandCursor);
    m_btnStart->setFixedHeight(30);
    m_btnStart->setToolTip("Start WebDAV server");

    m_btnStop = new QPushButton("  Stop", this);
    m_btnStop->setObjectName("btnStop");
    m_btnStop->setCursor(Qt::PointingHandCursor);
    m_btnStop->setFixedHeight(30);
    m_btnStop->setEnabled(false);
    m_btnStop->setToolTip("Stop WebDAV server");

    // ── Spacer ────────────────────────────────────────────────────────────────
    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // ── "..." menu ────────────────────────────────────────────────────────────
    m_btnMenu = new QToolButton(this);
    m_btnMenu->setObjectName("btnMenu");
    m_btnMenu->setText("…");
    m_btnMenu->setToolTip("Menu");
    m_btnMenu->setFixedSize(36, 30);
    m_btnMenu->setCursor(Qt::PointingHandCursor);
    m_btnMenu->setPopupMode(QToolButton::InstantPopup);

    QMenu *menu = new QMenu(m_btnMenu);
    menu->setObjectName("mainMenu");

    QAction *actAbout = menu->addAction("About");
    menu->addSeparator();
    QAction *actQuit = menu->addAction("Quit");

    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this,
                           "About",
                           "<b>Better WebDAV Server</b><br>"
                           "Version 1.0.0<br><br>"
                           "Simple WebDAV server built with Qt.<br>"
                           "Supports methods: GET, PUT, DELETE,<br>"
                           "MKCOL, PROPFIND, MOVE, COPY, OPTIONS.");
    });
    connect(actQuit, &QAction::triggered, this, &MainWindow::onQuit);
    m_btnMenu->setMenu(menu);

    // ── Add widgets to toolbar ───────────────────────────────────────────────
    m_topToolbar->addWidget(m_burgerButton);
    m_topToolbar->addWidget(pathIcon);
    m_topToolbar->addWidget(m_pathEdit);
    m_topToolbar->addWidget(m_btnBrowse);
    m_topToolbar->addSeparator();
    m_topToolbar->addWidget(portLabel);
    m_topToolbar->addWidget(m_portSpinBox);
    m_topToolbar->addSeparator();
    m_topToolbar->addWidget(m_btnStart);
    m_topToolbar->addWidget(m_btnStop);
    m_topToolbar->addWidget(spacer);
    m_topToolbar->addWidget(m_btnMenu);
    m_topToolbar->addWidget(new QLabel("  ", this));

    connect(m_btnBrowse, &QPushButton::clicked, this, &MainWindow::onBrowse);
    connect(m_btnStart,  &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_btnStop,   &QPushButton::clicked, this, &MainWindow::onStop);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupLogPage()
{
    m_logEdit = new QTextEdit(this);
    m_logEdit->setObjectName("logEdit");
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_logEdit->document()->setMaximumBlockCount(5000);

    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    m_logEdit->setFont(f);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupBottomToolbar()
{
    m_bottomToolbar = new QToolBar(this);
    m_bottomToolbar->setObjectName("bottomToolbar");
    m_bottomToolbar->setMovable(false);
    m_bottomToolbar->setFloatable(false);
    m_bottomToolbar->setFixedHeight(28);
    addToolBar(Qt::BottomToolBarArea, m_bottomToolbar);

    QWidget *statusWidget = new QWidget(this);
    QHBoxLayout *sl = new QHBoxLayout(statusWidget);
    sl->setContentsMargins(8, 0, 8, 0);
    sl->setSpacing(6);

    m_statusLabel = new QLabel("  Server stopped", statusWidget);
    m_statusLabel->setObjectName("statusLabel");

    sl->addWidget(m_statusLabel);
    sl->addStretch();

    QLabel *rightInfo = new QLabel("Better WebDAV Server v1.0", statusWidget);
    rightInfo->setObjectName("statusRight");
    sl->addWidget(rightInfo);

    statusWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_bottomToolbar->addWidget(statusWidget);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::applyStyles()
{
    setStyleSheet(R"(
QMainWindow {
    background-color: #F3F3F3;
}

QToolBar#topToolbar {
    background-color: #FFFFFF;
    border-bottom: 1px solid #D1D1D1;
    padding: 6px 8px;
    spacing: 6px;
}

QToolBar#bottomToolbar {
    background-color: #0078D4;
    border-top: none;
    padding: 0px;
    spacing: 0px;
}

QToolBar::separator {
    background-color: #D1D1D1;
    width: 1px;
    margin: 4px 8px;
}

QLabel#toolLabel {
    color: #4D4D4D;
    font-size: 9pt;
    font-family: "Segoe UI";
}

QLineEdit#pathEdit {
    background-color: #FFFFFF;
    color: #000000;
    border: 1px solid #CCCCCC;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 9pt;
    font-family: "Segoe UI";
    selection-background-color: #0078D4;
}
QLineEdit#pathEdit:focus {
    border: 2px solid #0078D4;
}

QSpinBox#portSpinBox {
    background-color: #FFFFFF;
    color: #000000;
    border: 1px solid #CCCCCC;
    border-radius: 4px;
    padding: 2px 4px;
    font-size: 9pt;
    font-family: "Segoe UI";
}
QSpinBox#portSpinBox::up-button,
QSpinBox#portSpinBox::down-button {
    background-color: #E1E1E1;
    border: none;
    width: 16px;
}
QSpinBox#portSpinBox::up-button:hover,
QSpinBox#portSpinBox::down-button:hover {
    background-color: #D1D1D1;
}

QPushButton {
    background-color: #E1E1E1;
    color: #000000;
    border: 1px solid #CCCCCC;
    border-radius: 4px;
    padding: 6px 14px;
    font-size: 9pt;
    font-family: "Segoe UI";
}
QPushButton:hover {
    background-color: #D1D1D1;
    border-color: #999999;
}
QPushButton:pressed {
    background-color: #C8C8C8;
}
QPushButton:disabled {
    background-color: #F3F3F3;
    color: #A0A0A0;
    border-color: #E0E0E0;
}

QPushButton#btnStart {
    background-color: #0078D4;
    color: #FFFFFF;
    border: none;
    border-radius: 4px;
    padding: 6px 20px;
    font-weight: bold;
}
QPushButton#btnStart:hover {
    background-color: #106EBE;
}
QPushButton#btnStart:pressed {
    background-color: #005A9E;
}
QPushButton#btnStart:disabled {
    background-color: #B3D6F0;
    color: #6E6E6E;
}

QPushButton#btnStop {
    background-color: #E81123;
    color: #FFFFFF;
    border: none;
    border-radius: 4px;
    padding: 6px 20px;
    font-weight: bold;
}
QPushButton#btnStop:hover {
    background-color: #F1707A;
}
QPushButton#btnStop:pressed {
    background-color: #BF0A1A;
}
QPushButton#btnStop:disabled {
    background-color: #F4B9C0;
    color: #6E6E6E;
}

QToolButton#burgerButton {
    background-color: transparent;
    color: #4D4D4D;
    border: 1px solid transparent;
    border-radius: 4px;
    font-size: 18pt;
    font-weight: normal;
}
QToolButton#burgerButton:hover {
    background-color: #E1E1E1;
    color: #000000;
    border-color: #CCCCCC;
}
QToolButton#burgerButton:pressed {
    background-color: #D1D1D1;
}
QToolButton#burgerButton::menu-indicator { image: none; }

QToolButton#btnMenu {
    background-color: transparent;
    color: #4D4D4D;
    border: 1px solid transparent;
    border-radius: 4px;
    font-size: 14pt;
    font-weight: bold;
}
QToolButton#btnMenu:hover {
    background-color: #E1E1E1;
    color: #000000;
    border-color: #CCCCCC;
}
QToolButton#btnMenu:pressed {
    background-color: #D1D1D1;
}
QToolButton#btnMenu::menu-indicator { image: none; }

QMenu#mainMenu, QMenu#burgerMenu {
    background-color: #FFFFFF;
    color: #000000;
    border: 1px solid #CCCCCC;
    padding: 4px 0;
}
QMenu#mainMenu::item, QMenu#burgerMenu::item {
    padding: 6px 24px 6px 16px;
    font-size: 9pt;
    font-family: "Segoe UI";
}
QMenu#mainMenu::item:selected, QMenu#burgerMenu::item:selected {
    background-color: #0078D4;
    color: #FFFFFF;
}
QMenu#mainMenu::separator, QMenu#burgerMenu::separator {
    height: 1px;
    background: #E1E1E1;
    margin: 3px 0;
}

QTextEdit#logEdit {
    background-color: #FFFFFF;
    color: #1E1E1E;
    border: none;
    border-top: 1px solid #D1D1D1;
    padding: 6px;
    font-family: "Consolas", "Courier New", monospace;
    font-size: 10pt;
}

QScrollBar:vertical {
    background-color: #F3F3F3;
    width: 10px;
    border: none;
}
QScrollBar::handle:vertical {
    background-color: #C1C1C1;
    border-radius: 5px;
    min-height: 20px;
}
QScrollBar::handle:vertical:hover {
    background-color: #A1A1A1;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical { height: 0px; }

QScrollBar:horizontal {
    background-color: #F3F3F3;
    height: 10px;
    border: none;
}
QScrollBar::handle:horizontal {
    background-color: #C1C1C1;
    border-radius: 5px;
    min-width: 20px;
}
QScrollBar::handle:horizontal:hover {
    background-color: #A1A1A1;
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal { width: 0px; }

QLabel#statusLabel {
    color: #FFFFFF;
    font-size: 9pt;
    font-weight: bold;
    font-family: "Segoe UI";
}
QLabel#statusRight {
    color: rgba(255, 255, 255, 0.8);
    font-size: 9pt;
    font-family: "Segoe UI";
}
    )");
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::showMonitor()
{
    if (m_stack) m_stack->setCurrentIndex(0);
}

void MainWindow::showLogs()
{
    if (m_stack) m_stack->setCurrentIndex(1);
}

void MainWindow::showSettings()
{
    if (m_stack) m_stack->setCurrentIndex(2);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        "Select root directory",
        m_pathEdit->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        m_pathEdit->setText(QDir::toNativeSeparators(dir));
        onLogMessage(QString("Path changed to: %1").arg(dir), "INFO");
    }
}

void MainWindow::onStart()
{
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        onLogMessage("Please specify a root directory.", "WARN");
        m_pathEdit->setFocus();
        return;
    }
    quint16 port = static_cast<quint16>(m_portSpinBox->value());
    m_server->start(path, port);
}

void MainWindow::onStop()
{
    m_server->stop();
}

void MainWindow::onQuit()
{
    if (m_server->isRunning()) {
        auto res = QMessageBox::question(
            this,
            "Quit",
            "Server is still running.\nStop server and quit?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (res != QMessageBox::Yes) return;
        m_server->stop();
    }
    QApplication::quit();
}

void MainWindow::onLogMessage(const QString &msg, const QString &level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");

    QString color, badge;
    if      (level == "ERROR") { color = "#E81123"; badge = "ERRO"; }
    else if (level == "WARN" ) { color = "#FF8C00"; badge = "WARN"; }
    else if (level == "REQ"  ) { color = "#107C10"; badge = "REQ "; }
    else                       { color = "#0078D4"; badge = "INFO"; }

    QString line = QString(
                       "<span style='color:#888888;'>%1</span> "
                       "<span style='color:%2;font-weight:bold;'>[%3]</span> "
                       "<span style='color:#1E1E1E;'>%4</span>")
                       .arg(timestamp, color, badge, msg.toHtmlEscaped());

    m_logEdit->append(line);

    QScrollBar *sb = m_logEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::onServerStarted(quint16 port)
{
    m_serverRunning = true;
    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_pathEdit->setEnabled(false);
    m_portSpinBox->setEnabled(false);
    m_btnBrowse->setEnabled(false);

    m_statusLabel->setText(
        QString("  Server running  |  http://localhost:%1  |  %2")
            .arg(port)
            .arg(m_pathEdit->text()));

    m_bottomToolbar->setStyleSheet(
        "QToolBar#bottomToolbar { background-color: #107C10; }");

    m_monitor->startUpdates();
}

void MainWindow::onServerStartFailed(const QString &reason)
{
    m_serverRunning = false;
    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_pathEdit->setEnabled(true);
    m_portSpinBox->setEnabled(true);
    m_btnBrowse->setEnabled(true);

    m_statusLabel->setText("  Server start failed");
    m_bottomToolbar->setStyleSheet(
        "QToolBar#bottomToolbar { background-color: #E81123; }");

    m_monitor->stopUpdates();

    if (!reason.isEmpty())
        onLogMessage(reason, "ERROR");
}

void MainWindow::onServerStopped()
{
    m_serverRunning = false;
    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_pathEdit->setEnabled(true);
    m_portSpinBox->setEnabled(true);
    m_btnBrowse->setEnabled(true);

    m_statusLabel->setText("  Server stopped");
    m_bottomToolbar->setStyleSheet(
        "QToolBar#bottomToolbar { background-color: #0078D4; }");

    m_monitor->stopUpdates();
}