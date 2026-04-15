#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include "webdavserver.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QToolButton;
class QToolBar;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void appendLog(const QString &message);

private slots:
    void startServer();
    void stopServer();
    void exitApplication();
    void onServerStarted(bool success);   // новый слот

private:
    void setupUI();
    QString getCurrentTimestamp() const;

    QPlainTextEdit *logArea;
    QLabel *pathLabel;
    QPushButton *startButton;
    QPushButton *stopButton;
    QToolButton *menuButton;
    QToolBar *topToolBar;
    QToolBar *bottomToolBar;

    WebDAVServer *webdavServer;
};

#endif // MAINWINDOW_H