// schedulertab.cpp — see schedulertab.hpp (redesign mockup Scheduler page).

#include "schedulertab.hpp"

#include <QButtonGroup>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "solar.hpp"
#include "style.hpp"
#include "themes.hpp"
#include "widgets.hpp"

namespace johona::gui {

// ── hero card (mockup .hero) ────────────────────────────────────────────
// (Forward-declared in schedulertab.hpp; defined here, outside the
// anonymous namespace, so the member pointer type matches.)

class HeroCard : public QWidget {
public:
    explicit HeroCard(QWidget* parent = nullptr) : QWidget(parent) {
        setAutoFillBackground(false);
    }

    void setRunning(bool running) {
        if (m_running != running) {
            m_running = running;
            update();
        }
    }

    /// Force a repaint after a theme-mode change (the border/gradient
    /// colors come from the current style tokens).
    void refreshColors() { update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& tok = style::current();
        const QRectF outer = QRectF(0.5, 0.5, width() - 1, height() - 1);
        QPainterPath path;
        path.addRoundedRect(outer, 10, 10);
        p.fillPath(path, QColor(tok.base));
        if (m_running) {
            // Green-tinted gradient from the left (mockup .hero.running).
            QLinearGradient g(0, 0, width(), 0);
            g.setColorAt(0.0, QColor(39, 174, 96, 23));   // rgba(…, .09)
            g.setColorAt(0.55, QColor(39, 174, 96, 0));
            p.fillPath(path, g);
            p.setPen(QPen(QColor(39, 174, 96, 115), 1));  // rgba(…, .45)
        } else {
            p.setPen(QPen(QColor(tok.midlight), 1));
        }
        p.drawPath(path);
    }

private:
    bool m_running = false;
};

namespace {

// ── event log delegate (mockup .log-line) ───────────────────────────────

class LogDelegate : public QStyledItemDelegate {
public:
    explicit LogDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(12);  // 11.5 px mockup
        m_font = mono;
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index)
        const override {
        Q_UNUSED(opt);
        Q_UNUSED(index);
        return QSize(400, 22);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override {
        const QString ts = index.data(Qt::UserRole + 1).toString();
        const QString msg = index.data(Qt::DisplayRole).toString();
        const int cat = index.data(Qt::UserRole).toInt();
        const auto& tok = style::current();

        p->setFont(m_font);
        const QFontMetrics fm(m_font);
        const int tsW = fm.horizontalAdvance(ts);
        p->setPen(QColor(tok.placeholder));
        p->drawText(opt.rect.x(), opt.rect.y(), tsW, opt.rect.height(),
                    Qt::AlignVCenter | Qt::AlignLeft, ts);

        QColor mc(tok.windowText);
        if (cat == 2)
            mc = QColor(tok.red);
        else if (cat == 3)
            mc = QColor(tok.green);
        p->setPen(mc);
        p->drawText(opt.rect.x() + tsW + 12, opt.rect.y(),
                    opt.rect.width() - tsW - 12, opt.rect.height(),
                    Qt::AlignVCenter | Qt::AlignLeft, msg);
    }

private:
    QFont m_font;
};

/// Log-line category: 0 info, 1 apply, 2 error, 3 ok (mockup data-cat).
int classifyLog(const QString& message) {
    const QString m = message.toLower();
    if (m.contains("fail") || m.contains("error"))
        return 2;
    if (m.contains("succeeded"))
        return 3;
    if (m.contains("apply") || m.contains("retry") ||
        m.contains("boundary") || m.contains("shuffle"))
        return 1;
    return 0;
}

}  // namespace

SchedulerTab::SchedulerTab(Engine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── header ──────────────────────────────────────────────────────────
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(20, 14, 20, 10);
    hl->setSpacing(12);
    auto* titleBox = new QVBoxLayout();
    titleBox->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Scheduler"), head);
    {
        QFont f;
        f.setPixelSize(17);
        f.setWeight(QFont::Bold);
        title->setFont(f);
    }
    auto* subtitle = new QLabel(
        QStringLiteral("Event-driven — one-shot boundary timer + safety "
                       "tick"),
        head);
    subtitle->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(12);
        subtitle->setFont(f);
    }
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    hl->addLayout(titleBox);
    hl->addStretch(1);

    m_nextBtn = new QPushButton(
        colorIcon(kRefreshSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Next wallpaper"), head);
    m_nextBtn->setToolTip(QStringLiteral(
        "Advance the shuffle list and apply the next theme"));
    connect(m_nextBtn, &QPushButton::clicked, this,
            &SchedulerTab::nextRequested);
    hl->addWidget(m_nextBtn);

    m_toggleBtn = new QPushButton(head);
    m_toggleBtn->setProperty("cssClass", "primary");
    connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
        if (m_toggleBtn->text() == QStringLiteral("Start"))
            emit startRequested();
        else
            emit stopRequested();
    });
    hl->addWidget(m_toggleBtn);
    root->addWidget(head);

    // ── hero card ───────────────────────────────────────────────────────
    m_hero = new HeroCard(this);
    auto* heroLay = new QHBoxLayout(m_hero);
    heroLay->setContentsMargins(20, 16, 20, 16);
    heroLay->setSpacing(22);

    auto* stateBox = new QVBoxLayout();
    stateBox->setSpacing(0);
    auto* heroLabel = new QLabel(QStringLiteral("STATUS"), m_hero);
    heroLabel->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(11);  // 10.5 px mockup
        f.setWeight(QFont::Bold);
        heroLabel->setFont(f);
    }
    stateBox->addWidget(heroLabel);

    auto* stateRow = new QHBoxLayout();
    stateRow->setSpacing(9);
    m_dot = new StatusDot(10, m_hero);
    stateRow->addWidget(m_dot);
    m_stateLabel = new QLabel(QStringLiteral("Stopped"), m_hero);
    {
        QFont f;
        f.setPixelSize(19);
        f.setWeight(QFont::Bold);
        m_stateLabel->setFont(f);
    }
    stateRow->addWidget(m_stateLabel);
    stateRow->addStretch(1);
    stateBox->addLayout(stateRow);

    m_subLabel = new QLabel(QString(), m_hero);
    m_subLabel->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(12);  // 11.5 px mockup
        m_subLabel->setFont(f);
    }
    stateBox->addWidget(m_subLabel);
    heroLay->addLayout(stateBox);
    heroLay->addStretch(1);

    // Stat tiles (mockup .hero-stats), separated by 1 px dividers.
    auto makeStat = [&](const QString& label, QLabel*& value, QLabel*& sub) {
        auto* box = new QVBoxLayout();
        box->setSpacing(0);
        auto* l = new QLabel(label.toUpper(), m_hero);
        l->setProperty("cssClass", "muted");
        {
            QFont f;
            f.setPixelSize(11);
            f.setWeight(QFont::Bold);
            l->setFont(f);
        }
        value = new QLabel(QStringLiteral("—"), m_hero);
        {
            QFont f;
            f.setPixelSize(15);  // 14.5 px mockup
            f.setWeight(QFont::Bold);
            value->setFont(f);
        }
        sub = new QLabel(QString(), m_hero);
        sub->setProperty("cssClass", "muted");
        {
            QFont f;
            f.setPixelSize(11);
            sub->setFont(f);
        }
        box->addWidget(l);
        box->addWidget(value);
        box->addWidget(sub);
        auto* w = new QWidget(m_hero);
        w->setLayout(box);
        return w;
    };
    auto addDivider = [this]() {
        auto* d = new QFrame(m_hero);
        d->setProperty("cssClass", "vline");
        d->setFixedWidth(1);
        return d;
    };
    heroLay->addWidget(makeStat(QStringLiteral("Next change"), m_nextValue,
                                m_nextSub));
    heroLay->addWidget(addDivider());
    heroLay->addWidget(makeStat(QStringLiteral("Current"), m_curValue,
                                m_curSub));
    heroLay->addWidget(addDivider());
    heroLay->addWidget(makeStat(QStringLiteral("Active theme"),
                                m_themeValue, m_themeSub));
    root->addWidget(m_hero);

    // ── event log card ──────────────────────────────────────────────────
    auto* logCard = new QFrame(this);
    logCard->setProperty("cssClass", "card");
    auto* logLay = new QVBoxLayout(logCard);
    logLay->setContentsMargins(0, 0, 0, 0);
    logLay->setSpacing(0);

    auto* logHead = new QWidget(logCard);
    logHead->setProperty("cssClass", "log-head");
    auto* logHeadLay = new QHBoxLayout(logHead);
    logHeadLay->setContentsMargins(12, 9, 12, 9);
    logHeadLay->setSpacing(10);
    auto* logTitle = new QLabel(QStringLiteral("Event log"), logHead);
    {
        QFont f;
        f.setPixelSize(13);  // 12.5 px mockup
        f.setWeight(QFont::Bold);
        logTitle->setFont(f);
    }
    logHeadLay->addWidget(logTitle);

    auto* chipGroup = new QButtonGroup(this);
    chipGroup->setExclusive(true);
    auto addChip = [&](const QString& text, QToolButton*& out) {
        out = new QToolButton(logHead);
        out->setText(text);
        out->setCheckable(true);
        out->setProperty("cssClass", "chip");
        chipGroup->addButton(out);
        logHeadLay->addWidget(out);
        return out;
    };
    m_filterAll = addChip(QStringLiteral("All"), m_filterAll);
    m_filterApply = addChip(QStringLiteral("Apply"), m_filterApply);
    m_filterError = addChip(QStringLiteral("Errors"), m_filterError);
    m_filterAll->setChecked(true);
    connect(chipGroup, &QButtonGroup::idClicked, this,
            [this](int) { applyLogFilter(); });

    logHeadLay->addStretch(1);
    auto* clearBtn = new QPushButton(QStringLiteral("Clear"), logHead);
    clearBtn->setProperty("cssClass", "small");
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_log->clear();
        m_logLines = 0;
        emit statusMessage(QStringLiteral("Event log cleared"));
    });
    logHeadLay->addWidget(clearBtn);
    logLay->addWidget(logHead);

    m_log = new QListWidget(logCard);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setSelectionMode(QAbstractItemView::NoSelection);
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_log->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_log->setItemDelegate(new LogDelegate(m_log));
    m_log->setProperty("cssClass", "log-body");
    auto* sb = m_log->verticalScrollBar();
    connect(sb, &QScrollBar::rangeChanged, this, [this, sb](int lo, int hi) {
        Q_UNUSED(lo);
        // Distinguish "user scrolled" (max unchanged) from "new line"
        // (max grew) so auto-scroll keeps working after appending.
        if (hi == m_lastMax)
            m_atBottom = hi <= sb->value() + 2;
        m_lastMax = hi;
    });
    logLay->addWidget(m_log, 1);
    root->addWidget(logCard, 1);

    // ── periodic hero refresh ───────────────────────────────────────────
    m_statsTimer.setInterval(60000);
    connect(&m_statsTimer, &QTimer::timeout, this,
            &SchedulerTab::refreshStats);
    m_statsTimer.start();

    setRunning(false);
    refreshStats();
}

void SchedulerTab::setRunning(bool running) {
    m_running = running;
    if (running)
        m_startedAt = QTime::currentTime();
    m_hero->setRunning(running);
    m_dot->setOn(running);
    m_stateLabel->setText(running ? QStringLiteral("Running")
                                  : QStringLiteral("Stopped"));
    QPalette spal = m_stateLabel->palette();
    spal.setColor(
        QPalette::WindowText,
        running ? QColor("#27ae60") : QColor(style::current().disabled));
    m_stateLabel->setPalette(spal);
    m_subLabel->setText(
        running
            ? QStringLiteral("Applying wallpapers with the sun since %1")
                  .arg(m_startedAt.toString("HH:mm"))
            : QStringLiteral("Scheduler is idle — no boundaries armed"));
    if (running)
        m_toggleBtn->setIcon(
            colorIcon(kStopFilledSvg, Qt::white, 24).pixmap(15, 15));
    else
        m_toggleBtn->setIcon(
            colorIcon(kPlayFilledSvg, Qt::white, 24).pixmap(15, 15));
    m_toggleBtn->setText(running ? QStringLiteral("Stop")
                                 : QStringLiteral("Start"));
    refreshStats();
}

void SchedulerTab::refreshThemeColors() {
    // Re-apply the state label color (green when running, disabled gray
    // otherwise) from the *current* token set, and repaint the hero card
    // (its border/gradient depend on the tokens).  Deliberately does not
    // touch m_startedAt or call setRunning().
    QPalette pal = m_stateLabel->palette();
    pal.setColor(QPalette::WindowText,
                 m_running ? QColor("#27ae60")
                           : QColor(style::current().disabled));
    m_stateLabel->setPalette(pal);
    m_hero->refreshColors();
}

void SchedulerTab::refreshStats() {
    auto fut = bridge::call<HeroStats>(m_engine, [this]() {
        HeroStats s;
        s.running = m_engine->isRunning();
        const config::Config cfg = m_engine->config();
        s.themeName = cfg.lastApplied.isEmpty() ? QStringLiteral("—")
                                                : cfg.lastApplied;
        const QString backend = m_engine->activeBackendName();
        s.backend = backend.isEmpty() || backend == "none"
                        ? QStringLiteral("no backend")
                        : QStringLiteral("%1 backend").arg(backend);
        if (!s.running) {
            s.nextTime = QStringLiteral("—");
            s.nextSub = QStringLiteral("not armed");
            s.curValue = QStringLiteral("—");
            s.curSub = QStringLiteral("—");
            return s;
        }
        const auto [when, label] = m_engine->nextChange();
        if (when.isValid()) {
            s.nextTime = when.time().toString("HH:mm");
            s.nextSub = label == QStringLiteral("next change")
                            ? label
                            : QStringLiteral("%1 segment").arg(label);
        } else {
            s.nextTime = QStringLiteral("—");
            s.nextSub = QStringLiteral("unknown");
        }
        // Current segment + image position (from the applied theme).
        if (!cfg.lastApplied.isEmpty()) {
            const QString themeDir =
                m_engine->paths().themesDir + QStringLiteral("/") +
                cfg.lastApplied;
            if (auto data = themes::loadThemeData(themeDir)) {
                const solar::ThemeImageLists lists{data->sunriseImageList,
                                                   data->dayImageList,
                                                   data->sunsetImageList,
                                                   data->nightImageList};
                const QTimeZone tz(cfg.timezone.toUtf8().constData());
                if (tz.isValid() && !lists.empty()) {
                    const double now = QDateTime::currentMSecsSinceEpoch();
                    const auto seg = solar::segmentsForNow(
                        now, QDateTime::fromMSecsSinceEpoch(now, tz).date(),
                        tz, cfg.latitude, cfg.longitude, lists);
                    if (auto sel = solar::imageAt(now, seg, lists)) {
                        const int n = sel->index + 1;
                        int m = 0;
                        const auto& list =
                            sel->category == solar::Category::Sunrise
                                ? lists.sunrise
                                : sel->category == solar::Category::Day
                                  ? lists.day
                                  : sel->category == solar::Category::Sunset
                                      ? lists.sunset
                                      : lists.night;
                        m = static_cast<int>(list.size());
                        s.curValue =
                            QStringLiteral("%1 · %2 of %3")
                                .arg(solar::categoryName(sel->category),
                                     QString::number(n),
                                     QString::number(m));
                        // End of this image's display window.
                        for (const auto& win :
                             solar::effectiveWindows(seg, lists)) {
                            if (win.category != sel->category)
                                continue;
                            const double dur =
                                (win.end - win.start) /
                                static_cast<double>(m);
                            const double until =
                                win.start + dur * static_cast<double>(n);
                            s.curSub = QStringLiteral("until %1")
                                           .arg(QDateTime::fromMSecsSinceEpoch(
                                                   until, tz)
                                               .time()
                                               .toString("HH:mm"));
                            break;
                        }
                    }
                }
            }
        }
        return s;
    });
    fut.then(this, [this](HeroStats s) {
        m_nextValue->setText(s.nextTime);
        m_nextSub->setText(s.nextSub);
        m_curValue->setText(s.curValue);
        m_curSub->setText(s.curSub);
        m_themeValue->setText(s.themeName);
        m_themeSub->setText(s.backend);
    });
}

bool SchedulerTab::visibleForFilter(int cat) const {
    if (m_filterApply->isChecked())
        return cat != 0;  // apply/retry/error lines (non-info)
    if (m_filterError->isChecked())
        return cat == 2;
    return true;  // All
}

void SchedulerTab::applyLogFilter() {
    for (int i = 0; i < m_log->count(); i++) {
        auto* item = m_log->item(i);
        item->setHidden(!visibleForFilter(item->data(Qt::UserRole).toInt()));
    }
}

int SchedulerTab::classify(const QString& message) {
    return classifyLog(message);
}

void SchedulerTab::appendLog(const QString& message) {
    const bool atBottom = m_atBottom;
    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const int cat = classify(message);
    auto* item = new QListWidgetItem(message);
    item->setData(Qt::UserRole, cat);
    item->setData(Qt::UserRole + 1, ts);
    item->setHidden(!visibleForFilter(cat));
    m_log->addItem(item);
    m_logLines++;
    if (m_logLines > kMaxLogLines) {
        // Drop the oldest ~10% of lines (kWallpaper parity).
        const int drop = qMax(1, m_log->count() / 10);
        for (int i = 0; i < drop && m_log->count() > 1; i++)
            delete m_log->takeItem(0);
        m_logLines -= drop;
    }
    if (atBottom)
        m_log->scrollToBottom();
}

}  // namespace johona::gui
