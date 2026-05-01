#include "mainwindow.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QScrollBar>
#include <QSizePolicy>

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
    setupLogArea();
    setupBottomToolbar();
    applyStyles();

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
    m_portSpinBox->setValue(80);                         // default port
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
    m_btnMenu->setText("...");
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
void MainWindow::setupLogArea()
{
    m_logEdit = new QTextEdit(this);
    m_logEdit->setObjectName("logEdit");
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_logEdit->document()->setMaximumBlockCount(5000);

    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    m_logEdit->setFont(f);

    setCentralWidget(m_logEdit);
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
    background-color: #1e1e1e;
}

QToolBar#topToolbar {
    background-color: #252526;
    border-bottom: 1px solid #3c3c3c;
    padding: 4px 6px;
    spacing: 4px;
}

QToolBar#bottomToolbar {
    background-color: #007acc;
    border-top: none;
    padding: 0px;
    spacing: 0px;
}

QToolBar::separator {
    background-color: #3c3c3c;
    width: 1px;
    margin: 4px 6px;
}

QLabel#toolLabel {
    color: #9d9d9d;
    font-size: 12px;
}

QLineEdit#pathEdit {
    background-color: #3c3c3c;
    color: #d4d4d4;
    border: 1px solid #555;
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 12px;
    selection-background-color: #264f78;
}
QLineEdit#pathEdit:focus {
    border: 1px solid #007acc;
}

QSpinBox#portSpinBox {
    background-color: #3c3c3c;
    color: #d4d4d4;
    border: 1px solid #555;
    border-radius: 4px;
    padding: 2px 4px;
    font-size: 12px;
}
QSpinBox#portSpinBox::up-button,
QSpinBox#portSpinBox::down-button {
    background-color: #4a4a4a;
    border: none;
    width: 16px;
}
QSpinBox#portSpinBox::up-button:hover,
QSpinBox#portSpinBox::down-button:hover {
    background-color: #5a5a5a;
}

QPushButton#btnBrowse {
    background-color: #3c3c3c;
    color: #d4d4d4;
    border: 1px solid #555;
    border-radius: 4px;
    padding: 2px 12px;
    font-size: 12px;
}
QPushButton#btnBrowse:hover {
    background-color: #4a4a4a;
    border-color: #777;
}
QPushButton#btnBrowse:pressed {
    background-color: #2a2a2a;
}

QPushButton#btnStart {
    background-color: #388e3c;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 2px 16px;
    font-size: 12px;
    font-weight: bold;
}
QPushButton#btnStart:hover  { background-color: #43a047; }
QPushButton#btnStart:pressed { background-color: #2e7d32; }
QPushButton#btnStart:disabled {
    background-color: #2a4a2a;
    color: #666;
}

QPushButton#btnStop {
    background-color: #c62828;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 2px 16px;
    font-size: 12px;
    font-weight: bold;
}
QPushButton#btnStop:hover  { background-color: #d32f2f; }
QPushButton#btnStop:pressed { background-color: #b71c1c; }
QPushButton#btnStop:disabled {
    background-color: #4a1f1f;
    color: #666;
}

QToolButton#btnMenu {
    background-color: transparent;
    color: #9d9d9d;
    border: 1px solid transparent;
    border-radius: 4px;
    font-size: 16px;
    font-weight: bold;
}
QToolButton#btnMenu:hover {
    background-color: #3c3c3c;
    color: #d4d4d4;
    border-color: #555;
}
QToolButton#btnMenu:pressed  { background-color: #2a2a2a; }
QToolButton#btnMenu::menu-indicator { image: none; }

QMenu#mainMenu {
    background-color: #252526;
    color: #d4d4d4;
    border: 1px solid #3c3c3c;
    padding: 4px 0;
}
QMenu#mainMenu::item {
    padding: 6px 24px 6px 16px;
    font-size: 12px;
}
QMenu#mainMenu::item:selected {
    background-color: #094771;
    color: #ffffff;
}
QMenu#mainMenu::separator {
    height: 1px;
    background: #3c3c3c;
    margin: 3px 0;
}

QTextEdit#logEdit {
    background-color: #0d0d0d;
    color: #d4d4d4;
    border: none;
    border-top: 1px solid #3c3c3c;
    padding: 6px;
    font-family: Consolas, "Courier New", monospace;
    font-size: 11px;
}

QScrollBar:vertical {
    background-color: #1e1e1e;
    width: 10px;
    border: none;
}
QScrollBar::handle:vertical {
    background-color: #424242;
    border-radius: 5px;
    min-height: 20px;
}
QScrollBar::handle:vertical:hover { background-color: #555; }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical { height: 0px; }

QScrollBar:horizontal {
    background-color: #1e1e1e;
    height: 10px;
    border: none;
}
QScrollBar::handle:horizontal {
    background-color: #424242;
    border-radius: 5px;
    min-width: 20px;
}
QScrollBar::handle:horizontal:hover { background-color: #555; }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal { width: 0px; }

QLabel#statusLabel {
    color: #ffffff;
    font-size: 11px;
    font-weight: bold;
}
QLabel#statusRight {
    color: rgba(255,255,255,0.6);
    font-size: 11px;
}

    )");
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

// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onStop()
{
    m_server->stop();
}

// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onLogMessage(const QString &msg, const QString &level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");

    QString color, badge;
    if      (level == "ERROR") { color = "#f44747"; badge = "ERRO"; }
    else if (level == "WARN" ) { color = "#ffcc02"; badge = "WARN"; }
    else if (level == "REQ"  ) { color = "#4ec9b0"; badge = "REQ "; }
    else                       { color = "#9cdcfe"; badge = "INFO"; }

    QString line = QString(
                       "<span style='color:#555555;'>%1</span> "
                       "<span style='color:%2;font-weight:bold;'>[%3]</span> "
                       "<span style='color:#d4d4d4;'>%4</span>")
                       .arg(timestamp, color, badge, msg.toHtmlEscaped());

    m_logEdit->append(line);

    QScrollBar *sb = m_logEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ─────────────────────────────────────────────────────────────────────────────
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
        "QToolBar#bottomToolbar { background-color: #1b5e20; }");
}

// ─────────────────────────────────────────────────────────────────────────────
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
        "QToolBar#bottomToolbar { background-color: #7f1d1d; }");

    if (!reason.isEmpty())
        onLogMessage(reason, "ERROR");
}

// ─────────────────────────────────────────────────────────────────────────────
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
        "QToolBar#bottomToolbar { background-color: #007acc; }");
}