// schedulertab.hpp — Scheduler tab (kWallpaper SchedulerPage parity):
// Status group (big centered label), Start/Stop buttons, and the live
// event log (monospace, "[hh:mm:ss AM/PM]" lines, capped).

#pragma once

#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QWidget>

#include "engine.hpp"

namespace johona::gui {

class SchedulerTab : public QWidget {
    Q_OBJECT
public:
    explicit SchedulerTab(Engine* engine, QWidget* parent = nullptr);

    /// Update the status label + button states.
    void setRunning(bool running);

signals:
    void startRequested();
    void stopRequested();
    void statusMessage(const QString& message);

public slots:
    void appendLog(const QString& message);

private:
    Engine* m_engine;

    QLabel* m_statusLabel;
    QPushButton* m_startBtn;
    QPushButton* m_stopBtn;
    QTextEdit* m_log;
    int m_logLines = 0;
    static constexpr int kMaxLogLines = 1000;
};

}  // namespace johona::gui
