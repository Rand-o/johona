// schedulertab.cpp — see schedulertab.hpp (kWallpaper SchedulerPage parity).

#include "schedulertab.hpp"

#include <QDateTime>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "appicons.hpp"

namespace johona::gui {

SchedulerTab::SchedulerTab(Engine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine) {
    auto* col = new QVBoxLayout(this);
    col->setSpacing(12);

    // ── Status ──────────────────────────────────────────────────────────
    auto* sg = new QGroupBox(QStringLiteral("Status"), this);
    auto* sv = new QVBoxLayout(sg);
    m_statusLabel = new QLabel(QStringLiteral("Stopped"), sg);
    QFont f = m_statusLabel->font();
    f.setPointSize(f.pointSize() + 2);
    m_statusLabel->setFont(f);  // deliberately not bold
    m_statusLabel->setAlignment(Qt::AlignCenter);
    sv->addWidget(m_statusLabel);
    col->addWidget(sg);

    // ── Controls ────────────────────────────────────────────────────────
    auto* row = new QHBoxLayout();
    m_startBtn = new QPushButton(
        themeIcon(QStringLiteral("media-playback-start"), kFallbackPlaySvg),
        QStringLiteral("Start"), this);
    connect(m_startBtn, &QPushButton::clicked, this,
            &SchedulerTab::startRequested);
    row->addWidget(m_startBtn);
    m_stopBtn = new QPushButton(
        themeIcon(QStringLiteral("media-playback-stop"), kFallbackStopSvg),
        QStringLiteral("Stop"), this);
    connect(m_stopBtn, &QPushButton::clicked, this,
            &SchedulerTab::stopRequested);
    m_stopBtn->setEnabled(false);
    row->addWidget(m_stopBtn);
    row->addStretch();
    col->addLayout(row);

    // ── Event Log ───────────────────────────────────────────────────────
    auto* lg = new QGroupBox(QStringLiteral("Event Log"), this);
    auto* lv = new QVBoxLayout(lg);
    m_log = new QTextEdit(lg);
    m_log->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(qMax(mono.pointSize(), 9));
    m_log->setFont(mono);
    m_log->setMinimumHeight(180);
    lv->addWidget(m_log);
    col->addWidget(lg, 1);
}

void SchedulerTab::setRunning(bool running) {
    m_statusLabel->setText(running ? QStringLiteral("Running")
                                   : QStringLiteral("Stopped"));
    if (running) {
        const QPalette pal = m_statusLabel->palette();
        m_statusLabel->setStyleSheet(
            QStringLiteral("color: %1;")
                .arg(pal.color(QPalette::Highlight).name()));
    } else {
        m_statusLabel->setStyleSheet(QString());
    }
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
}

void SchedulerTab::appendLog(const QString& message) {
    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss AP"));
    m_log->append(QStringLiteral("[%1]  %2").arg(ts, message));
    m_logLines++;
    if (m_logLines > kMaxLogLines) {
        // Drop the oldest ~10% of lines.
        const int drop = qMax(1, m_log->document()->blockCount() / 10);
        QTextCursor cur(m_log->document());
        for (int i = 0; i < drop && m_log->document()->blockCount() > 1; i++) {
            cur.movePosition(QTextCursor::Start);
            cur.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
        m_logLines -= drop;
    }
}

}  // namespace johona::gui
