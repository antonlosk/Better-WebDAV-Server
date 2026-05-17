#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Better WebDAV Server");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("BetterWebDAV");

    // Используем Fusion как основу
    app.setStyle(QStyleFactory::create("Fusion"));

    // Светлая палитра в стиле Windows 10
    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window,          QColor(245, 245, 245));
    lightPalette.setColor(QPalette::WindowText,       Qt::black);
    lightPalette.setColor(QPalette::Base,             Qt::white);
    lightPalette.setColor(QPalette::AlternateBase,    QColor(245, 245, 245));
    lightPalette.setColor(QPalette::ToolTipBase,      Qt::white);
    lightPalette.setColor(QPalette::ToolTipText,      Qt::black);
    lightPalette.setColor(QPalette::Text,             Qt::black);
    lightPalette.setColor(QPalette::Button,           QColor(240, 240, 240));
    lightPalette.setColor(QPalette::ButtonText,       Qt::black);
    lightPalette.setColor(QPalette::BrightText,       Qt::red);
    lightPalette.setColor(QPalette::Link,             QColor(0, 120, 212));
    lightPalette.setColor(QPalette::Highlight,        QColor(0, 120, 212));
    lightPalette.setColor(QPalette::HighlightedText,  Qt::white);
    lightPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(160, 160, 160));
    lightPalette.setColor(QPalette::Disabled, QPalette::Text,       QColor(160, 160, 160));

    app.setPalette(lightPalette);

    MainWindow w;
    w.show();

    return app.exec();
}