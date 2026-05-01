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

    // Dark color palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window,          QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText,       QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Base,             QColor(20, 20, 20));
    darkPalette.setColor(QPalette::AlternateBase,    QColor(40, 40, 40));
    darkPalette.setColor(QPalette::ToolTipBase,      QColor(50, 50, 50));
    darkPalette.setColor(QPalette::ToolTipText,      QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Text,             QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Button,           QColor(50, 50, 50));
    darkPalette.setColor(QPalette::ButtonText,       QColor(220, 220, 220));
    darkPalette.setColor(QPalette::BrightText,       Qt::red);
    darkPalette.setColor(QPalette::Link,             QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight,        QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText,  Qt::black);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));
    darkPalette.setColor(QPalette::Disabled, QPalette::Text,       QColor(100, 100, 100));

    app.setPalette(darkPalette);

    MainWindow w;
    w.show();

    return app.exec();
}