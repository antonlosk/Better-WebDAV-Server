#include "settings.h"
#include "webdavserver.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QSettings>
#include <QGroupBox>

Settings::Settings(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // ── Keep-Alive Settings ──────────────────────────────────────────────
    QGroupBox *keepAliveGroup = new QGroupBox("Keep‑Alive Connection Settings");
    keepAliveGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #CCCCCC; border-radius: 4px; margin-top: 8px; padding-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        );

    QFormLayout *keepAliveLayout = new QFormLayout(keepAliveGroup);
    keepAliveLayout->setSpacing(10);

    m_timeoutSpinBox = new QSpinBox();
    m_timeoutSpinBox->setRange(5, 3600);
    m_timeoutSpinBox->setValue(60);
    m_timeoutSpinBox->setSuffix(" sec");
    m_timeoutSpinBox->setToolTip("Idle timeout before closing the connection");
    keepAliveLayout->addRow("Idle Timeout:", m_timeoutSpinBox);

    m_intervalSpinBox = new QSpinBox();
    m_intervalSpinBox->setRange(1, 600);
    m_intervalSpinBox->setValue(10);
    m_intervalSpinBox->setSuffix(" sec");
    m_intervalSpinBox->setToolTip("How often to check for idle connections");
    keepAliveLayout->addRow("Check Interval:", m_intervalSpinBox);

    mainLayout->addWidget(keepAliveGroup);

    // ── Customization ─────────────────────────────────────────────────────
    QGroupBox *customizationGroup = new QGroupBox("Customization");
    customizationGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #CCCCCC; border-radius: 4px; margin-top: 8px; padding-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        );

    QFormLayout *customLayout = new QFormLayout(customizationGroup);
    customLayout->setSpacing(10);

    m_themeCombo = new QComboBox();
    m_themeCombo->addItem("System", "System");
    m_themeCombo->addItem("Dark",   "Dark");
    m_themeCombo->addItem("Light",  "Light");
    customLayout->addRow("Theme:", m_themeCombo);

    mainLayout->addWidget(customizationGroup);

    // ── Apply Button ──────────────────────────────────────────────────────
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

    // Загружаем сохранённые значения
    loadSettings();
}

void Settings::setServer(WebDavServer *server)
{
    m_server = server;
}

void Settings::loadSettings()
{
    QSettings s;
    m_timeoutSpinBox->setValue(s.value("idle/timeout", 60).toInt());
    m_intervalSpinBox->setValue(s.value("idle/interval", 10).toInt());

    QString theme = s.value("theme/name", "System").toString();
    int idx = m_themeCombo->findData(theme);
    if (idx >= 0)
        m_themeCombo->setCurrentIndex(idx);
}

void Settings::saveSettings()
{
    QSettings s;
    s.setValue("idle/timeout",  m_timeoutSpinBox->value());
    s.setValue("idle/interval", m_intervalSpinBox->value());
    s.setValue("theme/name",    m_themeCombo->currentData().toString());
}

void Settings::applySettings()
{
    // Сохраняем все значения в постоянное хранилище
    saveSettings();

    // Применяем тайм-ауты, только если сервер запущен
    if (m_server) {
        int timeoutSec  = m_timeoutSpinBox->value();
        int intervalSec = m_intervalSpinBox->value();
        m_server->setIdleSettings(timeoutSec * 1000, intervalSec * 1000);
    }

    // Применяем тему в любом случае (интерфейс не зависит от состояния сервера)
    QString theme = m_themeCombo->currentData().toString();
    emit themeChanged(theme);

    m_statusLabel->setText(QString("Settings applied: timeout = %1 sec, check every %2 sec, theme = %3.")
                               .arg(m_timeoutSpinBox->value())
                               .arg(m_intervalSpinBox->value())
                               .arg(theme));
}