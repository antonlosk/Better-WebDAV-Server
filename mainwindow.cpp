#include "mainwindow.h"
#include "webdavserver.h"

#include <QToolBar>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QDateTime>
#include <QVBoxLayout>
#include <QWidget>
#include <QApplication>
#include <QFileDialog>
#include <QSettings>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), webdavServer(nullptr), settings("MyCompany", "BetterWebDAV")
{
    loadSettings();
    setupUI();
    webdavServer = new WebDAVServer(this, rootPath, this);
    connect(webdavServer, &WebDAVServer::appendLog, this, &MainWindow::appendLog);
    connect(webdavServer, &WebDAVServer::serverStarted, this, &MainWindow::onServerStarted);
    appendLog("Приложение запущено. Добро пожаловать в Better WebDAV Server!");
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (webdavServer) {
        webdavServer->stopServer();
        delete webdavServer;
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("Better WebDAV Server");

    topToolBar = new QToolBar("Верхний тулбар", this);
    topToolBar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, topToolBar);

    pathLabel = new QLabel("Путь: " + rootPath, this);

    QPushButton *browseButton = new QPushButton("Обзор", this);
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::chooseRootFolder);

    startButton = new QPushButton("Start", this);
    stopButton = new QPushButton("Stop", this);
    stopButton->setEnabled(false);

    menuButton = new QToolButton(this);
    menuButton->setText("...");
    QMenu *menu = new QMenu(this);
    QAction *exitAction = new QAction("Exit", this);
    connect(exitAction, &QAction::triggered, this, &MainWindow::exitApplication);
    menu->addAction(exitAction);
    menuButton->setMenu(menu);
    menuButton->setPopupMode(QToolButton::InstantPopup);

    topToolBar->addWidget(pathLabel);
    topToolBar->addWidget(browseButton);
    topToolBar->addWidget(startButton);
    topToolBar->addWidget(stopButton);

    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topToolBar->addWidget(spacer);
    topToolBar->addWidget(menuButton);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startServer);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::stopServer);

    bottomToolBar = new QToolBar("Нижний тулбар", this);
    bottomToolBar->setMovable(false);
    addToolBar(Qt::BottomToolBarArea, bottomToolBar);
    QLabel *statusLabel = new QLabel("Готов", this);
    bottomToolBar->addWidget(statusLabel);

    logArea = new QPlainTextEdit(this);
    logArea->setReadOnly(true);
    logArea->setMaximumBlockCount(1000);
    setCentralWidget(logArea);

    resize(800, 600);
}

void MainWindow::startServer()
{
    if (!webdavServer) {
        appendLog("Ошибка: Сервер не инициализирован.");
        return;
    }
    webdavServer->startServer(8080);
}

void MainWindow::stopServer()
{
    if (webdavServer) {
        webdavServer->stopServer();
    }
    startButton->setEnabled(true);
    stopButton->setEnabled(false);
    appendLog("Сервер остановлен.");
}

void MainWindow::exitApplication()
{
    appendLog("Выход из приложения.");
    QApplication::quit();
}

void MainWindow::appendLog(const QString &message)
{
    QString logEntry = QString("[%1] %2").arg(getCurrentTimestamp(), message);
    logArea->appendPlainText(logEntry);
}

void MainWindow::onServerStarted(bool success)
{
    if (success) {
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        appendLog("Сервер успешно запущен.");
    } else {
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        appendLog("Ошибка запуска сервера (возможно, порт занят).");
    }
}

void MainWindow::chooseRootFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выберите корневую папку WebDAV",
                                                    rootPath,
                                                    QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        rootPath = QDir::toNativeSeparators(dir);
        if (!rootPath.endsWith(QDir::separator()))
            rootPath += QDir::separator();
        pathLabel->setText("Путь: " + rootPath);
        saveSettings();
        appendLog("Корневая папка изменена на: " + rootPath);
        if (webdavServer && stopButton->isEnabled()) {
            stopServer();
            startServer();
        }
    }
}

QString MainWindow::getCurrentTimestamp() const
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
}

void MainWindow::loadSettings()
{
    rootPath = settings.value("rootPath", "C:/").toString();
    if (!rootPath.endsWith('/') && !rootPath.endsWith('\\'))
        rootPath += '/';
}

void MainWindow::saveSettings()
{
    settings.setValue("rootPath", rootPath);
}