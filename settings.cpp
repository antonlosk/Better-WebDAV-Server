#include "settings.h"
#include "webdavserver.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QSettings>

Settings::Settings(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel("Keep‑Alive Connection Settings");
    titleLabel->setStyleSheet("font-size: 14pt; font-weight: bold; color: #0078D4;");
    mainLayout->addWidget(titleLabel);

    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    m_timeoutSpinBox = new QSpinBox();
    m_timeoutSpinBox->setRange(5, 3600);          // от 5 секунд до 1 часа
    m_timeoutSpinBox->setValue(60);               // по умолчанию 60 секунд
    m_timeoutSpinBox->setSuffix(" sec");
    m_timeoutSpinBox->setToolTip("Idle timeout before closing the connection");
    formLayout->addRow("Idle Timeout:", m_timeoutSpinBox);

    m_intervalSpinBox = new QSpinBox();
    m_intervalSpinBox->setRange(1, 600);          // от 1 до 600 секунд
    m_intervalSpinBox->setValue(10);               // по умолчанию 10 секунд
    m_intervalSpinBox->setSuffix(" sec");
    m_intervalSpinBox->setToolTip("How often to check for idle connections");
    formLayout->addRow("Check Interval:", m_intervalSpinBox);

    mainLayout->addLayout(formLayout);

    QPushButton *applyButton = new QPushButton("Apply");
    applyButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #0078D4; color: white; border: none; border-radius: 4px;"
        "  padding: 8px 24px; font-size: 10pt; font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #106EBE; }"
        "QPushButton:pressed { background-color: #005A9E; }"
        );
    connect(applyButton, &QPushButton::clicked, this, &Settings::applySettings);
    mainLayout->addWidget(applyButton, 0, Qt::AlignLeft);

    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet("color: #107C10; font-style: italic;");
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    // Загружаем сохранённые настройки при создании
    loadSettings();
}

void Settings::setServer(WebDavServer *server)
{
    m_server = server;
}

void Settings::loadSettings()
{
    QSettings s;
    int timeout  = s.value("idle/timeout", 60).toInt();
    int interval = s.value("idle/interval", 10).toInt();
    m_timeoutSpinBox->setValue(timeout);
    m_intervalSpinBox->setValue(interval);
}

void Settings::saveSettings()
{
    QSettings s;
    s.setValue("idle/timeout",  m_timeoutSpinBox->value());
    s.setValue("idle/interval", m_intervalSpinBox->value());
}

void Settings::applySettings()
{
    if (!m_server) {
        m_statusLabel->setText("Server is not running.");
        return;
    }

    int timeoutSec  = m_timeoutSpinBox->value();
    int intervalSec = m_intervalSpinBox->value();

    // Сохраняем в постоянное хранилище
    saveSettings();

    // Передаём работающему серверу
    m_server->setIdleSettings(timeoutSec * 1000, intervalSec * 1000);

    m_statusLabel->setText(QString("Settings applied: timeout = %1 sec, check every %2 sec.")
                               .arg(timeoutSec).arg(intervalSec));
}