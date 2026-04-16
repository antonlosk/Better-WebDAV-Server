#include "mainwindow.h"
#include "webdavserver.h"

#include <QFileDialog>
#include <QDateTime>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_server(nullptr)
{
    setupUi();

    // Настройка меню "..." в правом верхнем углу
    QMenu *menu = new QMenu(this);
    QAction *exitAction = menu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExitActionTriggered);
    m_menuButton->setMenu(menu);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);

    // Начальные значения
    m_pathLineEdit->setText("C:/");
    m_startStopButton->setText("Start");

    // Подключаем сигналы
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseButtonClicked);
    connect(m_startStopButton, &QPushButton::clicked, this, &MainWindow::onStartStopButtonClicked);

    // Создаём сервер
    m_server = new WebDavServer(this);
    connect(m_server, &WebDavServer::stateChanged, this, &MainWindow::onServerStateChanged);
    connect(m_server, &WebDavServer::logMessage, this, &MainWindow::logMessage);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    // Центральный виджет и основной layout
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // Верхняя панель (тулбар)
    QHBoxLayout *topLayout = new QHBoxLayout();

    QLabel *pathLabel = new QLabel("Корневая папка:");
    topLayout->addWidget(pathLabel);

    m_pathLineEdit = new QLineEdit();
    topLayout->addWidget(m_pathLineEdit);

    m_browseButton = new QPushButton("Обзор...");
    topLayout->addWidget(m_browseButton);

    m_startStopButton = new QPushButton("Start");
    topLayout->addWidget(m_startStopButton);

    topLayout->addStretch();

    m_menuButton = new QToolButton();
    m_menuButton->setText("...");
    topLayout->addWidget(m_menuButton);

    mainLayout->addLayout(topLayout);

    // Область логов
    m_logTextEdit = new QTextEdit();
    m_logTextEdit->setReadOnly(true);
    mainLayout->addWidget(m_logTextEdit);

    setCentralWidget(central);

    // Окно
    setWindowTitle("Better WebDAV Server");
    resize(800, 600);

    // Меню и статусбар (пустые, но могут пригодиться)
    menuBar();
    statusBar();
}

void MainWindow::onBrowseButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Выбор корневой папки", m_pathLineEdit->text());
    if (!dir.isEmpty()) {
        m_pathLineEdit->setText(dir);
    }
}

void MainWindow::onStartStopButtonClicked()
{
    if (m_server->isRunning()) {
        m_server->stop();
    } else {
        m_server->start(m_pathLineEdit->text());
    }
}

void MainWindow::onServerStateChanged(bool isRunning)
{
    m_startStopButton->setText(isRunning ? "Stop" : "Start");
    m_pathLineEdit->setEnabled(!isRunning);
    m_browseButton->setEnabled(!isRunning);

    QString state = isRunning ? "запущен" : "остановлен";
    logMessage(QString("Сервер %1").arg(state));
}

void MainWindow::logMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logTextEdit->append(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::onExitActionTriggered()
{
    QApplication::quit();
}