#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QToolButton>

class WebDavServer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onBrowseButtonClicked();
    void onStartStopButtonClicked();
    void onExitActionTriggered();
    void onServerStateChanged(bool isRunning);
    void logMessage(const QString &message);

private:
    void setupUi();

    QLineEdit *m_pathLineEdit;
    QPushButton *m_browseButton;
    QPushButton *m_startStopButton;
    QToolButton *m_menuButton;
    QTextEdit *m_logTextEdit;

    WebDavServer *m_server;
};

#endif // MAINWINDOW_H