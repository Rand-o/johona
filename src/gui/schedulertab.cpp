// schedulertab.cpp — see schedulertab.hpp.

#include "schedulertab.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

#include "enginebridge.hpp"

namespace johona::gui {

namespace {
struct Status {
    bool running = false;
    QString nextChange;
    QString backend;
    QString lastApplied;
    QString lastAppliedImage;
};
}  // namespace

SchedulerTab::SchedulerTab(Engine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine) {
    m_toggleBtn = new QPushButton(tr("Start scheduler"), this);
    m_toggleBtn->setCheckable(true);

    auto* statusGroup = new QGroupBox(tr("Status"), this);
    auto* form = new QFormLayout(statusGroup);
    m_runningLabel = new QLabel(statusGroup);
    m_nextChangeLabel = new QLabel(statusGroup);
    m_backendLabel = new QLabel(statusGroup);
    m_lastAppliedLabel = new QLabel(statusGroup);
    form->addRow(tr("State:"), m_runningLabel);
    form->addRow(tr("Next change:"), m_nextChangeLabel);
    form->addRow(tr("Backend:"), m_backendLabel);
    form->addRow(tr("Last applied:"), m_lastAppliedLabel);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kMaxLogLines);
    m_log->setPlaceholderText(tr("Scheduler events will appear here…"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(m_toggleBtn);
    root->addWidget(statusGroup);
    root->addWidget(m_log, 1);

    connect(m_toggleBtn, &QPushButton::toggled, this, &SchedulerTab::onToggle);
    connect(m_engine, &Engine::logMessage, this, &SchedulerTab::appendLog);
    connect(m_engine, &Engine::applied, this, [this](const QString& theme,
                                                     const QString& image,
                                                     const QString& category) {
        Q_UNUSED(category);
        m_lastAppliedLabel->setText(
            theme + (image.isEmpty() ? QString() : tr(" — %1").arg(QFileInfo(image).fileName())));
    refreshStatus();
    });

    // Periodic status refresh (next-change countdown, backend).
    m_statusTimer.setInterval(15000);
    connect(&m_statusTimer, &QTimer::timeout, this, &SchedulerTab::refreshStatus);
    m_statusTimer.start();

    refreshStatus();
}

void SchedulerTab::onToggle() {
    const bool start = m_toggleBtn->isChecked();
    bridge::call(m_engine, [this, start]() {
        if (start)
            m_engine->start();
        else
            m_engine->stop();
    });
    m_runningLabel->setText(start ? tr("Running") : tr("Stopped"));
    emit statusMessage(start ? tr("Scheduler started") : tr("Scheduler stopped"));
    refreshStatus();
}

void SchedulerTab::refreshStatus() {
    auto future = bridge::call<Status>(m_engine, [this]() {
        Status s;
        s.running = m_engine->isRunning();
        const auto next = m_engine->nextChange();
        if (next.first.isValid())
            s.nextChange = next.first.toString(Qt::ISODateWithMs).replace('T', ' ') +
                           "  (" + next.second + ")";
        else
            s.nextChange = tr("unknown");
        s.backend = m_engine->activeBackendName();
        const auto cfg = m_engine->config();
        s.lastApplied = cfg.lastApplied;
        s.lastAppliedImage = cfg.lastAppliedImage;
        return s;
    });
    future.then(this, [this](Status s) {
        m_toggleBtn->blockSignals(true);
        m_toggleBtn->setChecked(s.running);
        m_toggleBtn->blockSignals(false);
        m_runningLabel->setText(s.running ? tr("Running") : tr("Stopped"));
        m_nextChangeLabel->setText(s.nextChange);
        m_backendLabel->setText(s.backend);
        m_lastAppliedLabel->setText(s.lastApplied.isEmpty()
                                        ? tr("—")
                                        : s.lastApplied +
                                              (s.lastAppliedImage.isEmpty()
                                                   ? QString()
                                                   : tr(" — %1").arg(s.lastAppliedImage)));
    });
}

void SchedulerTab::appendLog(const QString& message) {
    m_log->appendPlainText(
        QDateTime::currentDateTime().toString("hh:mm:ss") + "  " + message);
}

}  // namespace johona::gui
