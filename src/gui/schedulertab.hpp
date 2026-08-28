// schedulertab.hpp — Scheduler tab (spec §11.3): start/stop, status block
// (running state, next change, active backend, last applied), and the live
// event log (capped at ~1000 lines).

#pragma once

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "engine.hpp"

namespace johona::gui {

class SchedulerTab : public QWidget {
    Q_OBJECT
public:
    explicit SchedulerTab(Engine* engine, QWidget* parent = nullptr);

signals:
    void statusMessage(const QString& message);

private slots:
    void onToggle();
    void refreshStatus();
    void appendLog(const QString& message);

private:
    Engine* m_engine;

    QPushButton* m_toggleBtn;
    QLabel* m_runningLabel;
    QLabel* m_nextChangeLabel;
    QLabel* m_backendLabel;
    QLabel* m_lastAppliedLabel;
    QPlainTextEdit* m_log;
    QTimer m_statusTimer;  // periodic status refresh (15 s)
    int m_logLines = 0;
    static constexpr int kMaxLogLines = 1000;
};

}  // namespace johona::gui
