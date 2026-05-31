#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Better WebDAV Server");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("BetterWebDAV");

    // Use Fusion style for a modern look
    app.setStyle(QStyleFactory::create("Fusion"));

    MainWindow w;
    w.show();

    return app.exec();
}