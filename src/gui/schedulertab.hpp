// schedulertab.hpp — Scheduler page (redesign mockup): header (title +
// subtitle, Next wallpaper + Start/Stop), status hero card (state + three
// stat tiles), and the live event log (monospace, filter chips
// All/Apply/Errors, Clear, capped).

#pragma once

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include "engine.hpp"

namespace johona::gui {

class SchedulerTab : public QWidget {
    Q_OBJECT
public:
    explicit SchedulerTab(Engine* engine, QWidget* parent = nullptr);

    /// Update the hero, buttons, and log lock state.
    void setRunning(bool running);

    /// Re-apply palette-dependent colors after a theme-mode change
    /// (called by MainWindow::applyAppearance).
    void refreshThemeColors();

signals:
    void startRequested();
    void stopRequested();
    void nextRequested();
    void statusMessage(const QString& message);

public slots:
    void appendLog(const QString& message);

private:
    struct HeroStats {
        bool running = false;
        QString nextTime;   // "18:47" | "—"
        QString nextSub;    // "Sunset segment" | "not armed"
        QString curValue;   // "Day · 4 of 6" | "—"
        QString curSub;     // "until 15:33" | "—"
        QString themeName;  // "Alpine" | "—"
        QString backend;    // "Plasma backend"
    };

    void refreshStats();
    void applyLogFilter();
    bool visibleForFilter(int cat) const;
    static int classify(const QString& message);

    Engine* m_engine;

    // Header
    QPushButton* m_nextBtn;
    QPushButton* m_toggleBtn;

    // Hero
    class HeroCard* m_hero;
    class StatusDot* m_dot;
    QLabel* m_stateLabel;
    QLabel* m_subLabel;
    QLabel* m_nextValue;
    QLabel* m_nextSub;
    QLabel* m_curValue;
    QLabel* m_curSub;
    QLabel* m_themeValue;
    QLabel* m_themeSub;

    // Log
    QListWidget* m_log;
    QToolButton* m_filterAll;
    QToolButton* m_filterApply;
    QToolButton* m_filterError;
    int m_logLines = 0;
    static constexpr int kMaxLogLines = 1000;

    QTimer m_statsTimer;  // 60 s: hero stats + status-bar next time
    QTime m_startedAt;    // wall clock when the scheduler started
    bool m_running = false;
    bool m_atBottom = true;
    int m_lastMax = 0;
};

}  // namespace johona::gui
