#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>

class WebDavServer;

class Settings : public QWidget
{
    Q_OBJECT
public:
    explicit Settings(QWidget *parent = nullptr);

    void setServer(WebDavServer *server);
    void loadSettings();   // загрузить сохранённые значения в поля ввода
    void saveSettings();   // сохранить текущие значения

private slots:
    void applySettings();

private:
    QSpinBox *m_timeoutSpinBox;   // секунды
    QSpinBox *m_intervalSpinBox;  // секунды
    QLabel   *m_statusLabel;
    WebDavServer *m_server = nullptr;
};