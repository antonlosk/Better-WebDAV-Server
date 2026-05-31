#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>

class WebDavServer;

class Settings : public QWidget
{
    Q_OBJECT
public:
    explicit Settings(QWidget *parent = nullptr);

    void setServer(WebDavServer *server);
    void loadSettings();
    void saveSettings();

signals:
    void themeChanged(const QString &theme);

private slots:
    void applySettings();
    void resetToDefaults();

private:
    QSpinBox   *m_timeoutSpinBox;
    QSpinBox   *m_intervalSpinBox;
    QLabel     *m_statusLabel;
    QComboBox  *m_themeCombo;
    WebDavServer *m_server = nullptr;
};