// settstab.cpp — see settstab.hpp (redesign mockup Settings page).

#include "settstab.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCompleter>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "autostart.hpp"
#include "enginebridge.hpp"
#include "style.hpp"

namespace johona::gui {

// ── switch (mockup .switch: 38×21, 15 px knob, 150 ms slide) ───────────
// (Forward-declared in settstab.hpp; defined here, outside the anonymous
// namespace, so the member pointer types match.)

class ToggleSwitch : public QCheckBox {
    Q_OBJECT
    Q_PROPERTY(double knobPos READ knobPos WRITE setKnobPos)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QCheckBox(parent) {
        setFixedSize(38, 21);
        m_anim = new QPropertyAnimation(this, "knobPos", this);
        m_anim->setDuration(150);
        connect(this, &QCheckBox::toggled, this, [this](bool on) {
            m_anim->stop();
            m_anim->setStartValue(knobPos());
            m_anim->setEndValue(on ? 26.5 : 10.5);
            m_anim->start();
        });
    }

    double knobPos() const { return m_knobPos; }
    void setKnobPos(double v) {
        m_knobPos = v;
        update();
    }

    QSize sizeHint() const override { return QSize(38, 21); }
    QSize minimumSizeHint() const override { return QSize(38, 21); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& tok = style::current();
        const QRectF track(0.5, 0.5, 37, 20);
        QPainterPath tp;
        tp.addRoundedRect(track, 11, 11);
        if (isChecked()) {
            p.fillPath(tp, QColor(tok.highlight));
            p.setPen(QPen(QColor(tok.highlight), 1));
        } else {
            p.fillPath(tp, QColor(tok.mid));
            p.setPen(QPen(QColor(tok.btnBorder), 1));
        }
        p.drawPath(tp);
        // 15 px white knob with a soft shadow.
        const double cy = 10.5;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));  // rgba(0,0,0,.35)-ish
        p.drawEllipse(QPointF(m_knobPos, cy + 1), 7.5, 7.5);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(m_knobPos, cy), 7.5, 7.5);
    }

private:
    double m_knobPos = 10.5;
    QPropertyAnimation* m_anim;
};

// ── color-scheme mini-card (mockup .scheme) ────────────────────────────

class SchemeCard : public QAbstractButton {
public:
    enum Mode { System, Light, Dark };

    explicit SchemeCard(Mode mode, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_mode(mode) {
        setCheckable(true);
        setFixedSize(96, 80);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& tok = style::current();
        const QRectF outer(0.5, 0.5, 95, 79);
        QPainterPath path;
        path.addRoundedRect(outer, 6, 6);
        p.fillPath(path, isChecked() ? QColor(tok.btnHover)
                                     : QColor(tok.button));
        if (isChecked()) {
            // Blue border + 1 px ring (mockup .scheme.selected).
            p.setPen(QPen(QColor(tok.highlight), 1.0));
            p.drawPath(path);
            p.setPen(QPen(QColor(tok.highlight), 1.0));
            p.drawRoundedRect(outer.adjusted(-1.5, -1.5, 1.5, 1.5), 7, 7);
        } else {
            p.setPen(QPen(QColor(tok.btnBorder), 1.0));
            p.drawPath(path);
        }

        // Window swatch (mockup .swatch): 44 px, titlebar strip + bars.
        const QRectF sw(7, 7, 82, 44);
        QColor bg, strip, barFull, barShort;
        if (m_mode == System) {
            QLinearGradient g(sw.x(), sw.y(), sw.right(), sw.y());
            g.setColorAt(0.498, QColor("#eff0f1"));
            g.setColorAt(0.502, QColor("#202326"));
            p.fillPath(swPath(sw), g);
            strip = QColor("#272c31");
            barFull = QColor("#3daee9");
            barShort = QColor("#c8cbce");
        } else if (m_mode == Light) {
            p.fillPath(swPath(sw), QColor("#eff0f1"));
            strip = QColor("#e3e5e7");
            barFull = QColor("#3daee9");
            barShort = QColor("#c8cbce");
        } else {
            p.fillPath(swPath(sw), QColor("#202326"));
            strip = QColor("#272c31");
            barFull = QColor("#3daee9");
            barShort = QColor("#4d5154");
        }
        p.setPen(QPen(QColor(tok.midlight), 1));
        p.drawPath(swPath(sw));
        // titlebar strip
        p.setPen(Qt::NoPen);
        p.setBrush(strip);
        p.drawRect(QRectF(sw.x(), sw.y(), sw.width(), 9));
        // content bars
        p.setBrush(barFull);
        p.drawRoundedRect(QRectF(sw.x() + 5, sw.y() + 11, sw.width() - 10, 5),
                          2, 2);
        p.drawRoundedRect(QRectF(sw.x() + 5, sw.y() + 19, sw.width() * 0.45, 5),
                          2, 2);
        p.drawRoundedRect(QRectF(sw.x() + 5, sw.y() + 27, sw.width() - 10, 5),
                          2, 2);

        // caption
        QFont f;
        f.setPixelSize(12);  // 11.5 px mockup
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(tok.windowText));
        const QString cap = m_mode == System   ? QStringLiteral("System")
                         : m_mode == Light ? QStringLiteral("Light")
                                           : QStringLiteral("Dark");
        p.drawText(QRectF(0, 55, 96, 20), Qt::AlignCenter, cap);
    }

private:
    static QPainterPath swPath(const QRectF& r) {
        QPainterPath path;
        path.addRoundedRect(r, 4, 4);
        return path;
    }

    Mode m_mode;
};

namespace {

const char* kBackendIds[] = {"auto", "plasma", "portal", "gnome",
                             "xdg_settings"};

// ── card / row builders ─────────────────────────────────────────────────

struct Card {
    QFrame* frame;
    QVBoxLayout* lay;
};

Card makeCard(QWidget* parent, const char* iconSvg, const QString& title,
              const QString& desc) {
    auto* card = new QFrame(parent);
    card->setProperty("cssClass", "card");
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 14, 18, 16);
    v->setSpacing(0);

    auto* head = new QHBoxLayout();
    head->setSpacing(8);
    auto* icon = new QLabel(card);
    icon->setPixmap(
        colorIcon(iconSvg, QColor("#80848a"), 24).pixmap(15, 15));
    icon->setFixedSize(15, 15);
    auto* t = new QLabel(title, card);
    {
        QFont f;
        f.setPixelSize(13);
        f.setWeight(QFont::Bold);
        t->setFont(f);
    }
    head->addWidget(icon);
    head->addWidget(t);
    head->addStretch(1);
    v->addLayout(head);

    auto* d = new QLabel(desc, card);
    {
        QFont f;
        f.setPixelSize(12);  // 11.5 px mockup
        d->setFont(f);
        QPalette pal = d->palette();
        pal.setColor(QPalette::WindowText,
                     pal.color(QPalette::PlaceholderText));
        d->setPalette(pal);
    }
    d->setWordWrap(true);
    d->setContentsMargins(23, 3, 0, 12);
    v->addWidget(d);
    return {card, v};
}

/// One settings row: 190 px label block (title + description) on the left,
/// control(s) right-aligned.  `first` suppresses the top divider.
void addRow(Card& card, bool first, const QString& title,
            const QString& desc, QWidget* ctl) {
    auto* row = new QWidget(card.frame);
    if (!first)
        row->setProperty("cssClass", "row");
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 8, 0, 8);
    h->setSpacing(12);

    auto* lbl = new QWidget(row);
    lbl->setFixedWidth(190);
    auto* lv = new QVBoxLayout(lbl);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(1);
    auto* t = new QLabel(title, lbl);
    {
        QFont f;
        f.setPixelSize(13);  // 12.5 px mockup
        f.setWeight(QFont::DemiBold);
        t->setFont(f);
    }
    lv->addWidget(t);
    if (!desc.isEmpty()) {
        auto* d = new QLabel(desc, lbl);
        {
            QFont f;
            f.setPixelSize(11);
            d->setFont(f);
            QPalette pal = d->palette();
            pal.setColor(QPalette::WindowText,
                         pal.color(QPalette::PlaceholderText));
            d->setPalette(pal);
        }
        d->setWordWrap(true);
        lv->addWidget(d);
    }
    h->addWidget(lbl);
    h->addStretch(1);
    h->addWidget(ctl);
    card.lay->addWidget(row);
}

QLabel* mutedLabel(QWidget* parent, const QString& text, int px = 11) {
    auto* l = new QLabel(text, parent);
    QFont f;
    f.setPixelSize(px);
    l->setFont(f);
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    l->setPalette(pal);
    return l;
}

}  // namespace

SettingsTab::SettingsTab(Engine* engine,
                         location::LocationManager* locationManager,
                         QWidget* parent)
    : QWidget(parent), m_engine(engine), m_location(locationManager) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── header ──────────────────────────────────────────────────────────
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(20, 14, 20, 10);
    hl->setSpacing(12);
    auto* titleBox = new QVBoxLayout();
    titleBox->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Settings"), head);
    {
        QFont f;
        f.setPixelSize(17);
        f.setWeight(QFont::Bold);
        title->setFont(f);
    }
    auto* subtitle = new QLabel(
        QStringLiteral("Changes apply to the running engine immediately"),
        head);
    {
        QFont f;
        f.setPixelSize(12);
        subtitle->setFont(f);
        QPalette pal = subtitle->palette();
        pal.setColor(QPalette::WindowText,
                     pal.color(QPalette::PlaceholderText));
        subtitle->setPalette(pal);
    }
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    hl->addLayout(titleBox);
    hl->addStretch(1);

    auto* revertBtn = new QPushButton(QStringLiteral("Revert"), head);
    connect(revertBtn, &QPushButton::clicked, this,
            [this]() { reload(); });
    hl->addWidget(revertBtn);

    m_saveBtn = new QPushButton(
        colorIcon(kSaveSvg, Qt::white, 24).pixmap(15, 15),
        QStringLiteral("Save"), head);
    m_saveBtn->setProperty("cssClass", "primary");
    m_saveBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsTab::onSave);
    hl->addWidget(m_saveBtn);
    outer->addWidget(head);

    // ── scrollable 660 px column ────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto* body = new QWidget();
    auto* bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(20, 0, 20, 14);
    bodyLay->setSpacing(0);
    auto* col = new QWidget(body);
    col->setFixedWidth(660);
    auto* colLay = new QVBoxLayout(col);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(12);
    bodyLay->addWidget(col);
    bodyLay->addStretch(1);

    // ── Scheduler ───────────────────────────────────────────────────────
    {
        Card c = makeCard(
            col, kNavSchedulerSvg, QStringLiteral("Scheduler"),
            QStringLiteral("How often the scheduler double-checks the "
                           "current image."));
        m_interval = new QSpinBox(c.frame);
        m_interval->setRange(1, 3600);
        m_interval->setSuffix(QStringLiteral(" s"));
        m_interval->setProperty("cssClass", "field");
        m_interval->setFixedWidth(110);
        addRow(c, true, QStringLiteral("Safety interval"),
               QStringLiteral("No-op tick that catches clock jumps & "
                              "resume"),
               m_interval);

        m_runCycle = new ToggleSwitch(c.frame);
        addRow(c, false, QStringLiteral("Cycle task"),
               QStringLiteral("Re-apply the current image every interval"),
               m_runCycle);
        m_dailyShuffle = new ToggleSwitch(c.frame);
        addRow(c, false, QStringLiteral("Daily theme shuffle"),
               QStringLiteral("Rotate through installed themes at "
                              "midnight"),
               m_dailyShuffle);
        m_startOnLaunch = new ToggleSwitch(c.frame);
        addRow(c, false, QStringLiteral("Start on launch"),
               QStringLiteral("Start the scheduler when Johona starts"),
               m_startOnLaunch);
        m_autostart = new ToggleSwitch(c.frame);
        connect(m_autostart, &QCheckBox::toggled, this,
                &SettingsTab::onAutostartToggled);
        addRow(c, false, QStringLiteral("Start at login"),
               QStringLiteral("Launch Johona automatically (autostart "
                              "entry)"),
               m_autostart);
        colLay->addWidget(c.frame);
    }

    // ── Location ────────────────────────────────────────────────────────
    {
        Card c = makeCard(
            col, kLocationSvg, QStringLiteral("Location"),
            QStringLiteral("Drives the sunrise/sunset segment boundaries "
                           "(suncalc)."));
        m_timezone = new QLineEdit(c.frame);
        m_timezone->setProperty("cssClass", "field");
        m_timezone->setFixedWidth(210);
        {
            QStringList zones;
            for (const QByteArray& id : QTimeZone::availableTimeZoneIds())
                zones << QString::fromUtf8(id);
            m_timezone->setCompleter(new QCompleter(zones, m_timezone));
        }
        addRow(c, true, QStringLiteral("Timezone"),
               QStringLiteral("IANA identifier, e.g. America/Phoenix"),
               m_timezone);

        auto* coordRow = new QWidget(c.frame);
        auto* cr = new QHBoxLayout(coordRow);
        cr->setContentsMargins(0, 0, 0, 0);
        cr->setSpacing(8);
        m_lat = new QDoubleSpinBox(coordRow);
        m_lat->setRange(-90.0, 90.0);
        m_lat->setDecimals(4);
        m_lat->setProperty("cssClass", "field");
        m_lat->setFixedWidth(110);
        m_lon = new QDoubleSpinBox(coordRow);
        m_lon->setRange(-180.0, 180.0);
        m_lon->setDecimals(4);
        m_lon->setProperty("cssClass", "field");
        m_lon->setFixedWidth(110);
        cr->addWidget(m_lat);
        cr->addWidget(m_lon);
        m_autoDetectBtn = new QPushButton(
            colorIcon(kRefreshSvg, QColor("#80848a"), 24).pixmap(14, 14),
            QStringLiteral("Auto-detect"), coordRow);
        m_autoDetectBtn->setProperty("cssClass", "small");
        m_autoDetectBtn->setToolTip(
            QStringLiteral("Detect the current location (Geoclue2, then "
                           "timezone lookup)"));
        connect(m_autoDetectBtn, &QPushButton::clicked, this,
                &SettingsTab::onAutoDetect);
        cr->addWidget(m_autoDetectBtn);
        addRow(c, false, QStringLiteral("Coordinates"),
               QStringLiteral("Latitude / longitude in decimal degrees"),
               coordRow);
        colLay->addWidget(c.frame);
    }

    // ── Wallpaper backend ───────────────────────────────────────────────
    {
        Card c = makeCard(
            col, kNavThemesSvg, QStringLiteral("Wallpaper backend"),
            QStringLiteral("Which desktop interface Johona sets the "
                           "wallpaper through."));
        m_backend = new QComboBox(c.frame);
        m_backend->setProperty("cssClass", "field");
        m_backend->setFixedWidth(210);
        m_backend->addItem(QStringLiteral("Auto (detected)"), "auto");
        m_backend->addItem(QStringLiteral("Plasma (KDE)"), "plasma");
        m_backend->addItem(QStringLiteral("XDG Background portal"),
                           "portal");
        m_backend->addItem(QStringLiteral("GNOME (gsettings)"), "gnome");
        m_backend->addItem(QStringLiteral("xdg-settings"), "xdg_settings");
        addRow(c, true, QStringLiteral("Backend"),
               QStringLiteral("Plasma, XDG portal, GNOME, xdg-settings"),
               m_backend);
        m_backendHint = mutedLabel(c.frame, QString(), 11);
        m_backendHint->setContentsMargins(202, 7, 0, 0);
        m_backendHint->setWordWrap(true);
        c.lay->addWidget(m_backendHint);
        colLay->addWidget(c.frame);
    }

    // ── Appearance ──────────────────────────────────────────────────────
    {
        Card c = makeCard(
            col, kNavSettingsSvg, QStringLiteral("Appearance"),
            QStringLiteral("Johona's own look and system integration."));
        auto* schemeRow = new QWidget(c.frame);
        auto* sr = new QHBoxLayout(schemeRow);
        sr->setContentsMargins(0, 0, 0, 0);
        sr->setSpacing(10);
        auto* schemeGroup = new QButtonGroup(this);
        schemeGroup->setExclusive(true);
        m_schemeSys = new SchemeCard(SchemeCard::System, schemeRow);
        m_schemeLight = new SchemeCard(SchemeCard::Light, schemeRow);
        m_schemeDark = new SchemeCard(SchemeCard::Dark, schemeRow);
        schemeGroup->addButton(m_schemeSys, 0);
        schemeGroup->addButton(m_schemeLight, 1);
        schemeGroup->addButton(m_schemeDark, 2);
        sr->addWidget(m_schemeSys);
        sr->addWidget(m_schemeLight);
        sr->addWidget(m_schemeDark);
        connect(schemeGroup, &QButtonGroup::idClicked, this,
                &SettingsTab::onSchemeChanged);
        addRow(c, true, QStringLiteral("Color scheme"),
               QStringLiteral("Override or follow the system KDE theme"),
               schemeRow);
        colLay->addWidget(c.frame);
    }

    colLay->addStretch(1);
    scroll->setWidget(body);

    reload();
}

void SettingsTab::reload() {
    const config::Config c = m_engine->config();
    m_interval->setValue(c.safetyInterval);
    m_runCycle->setChecked(c.cycleEnabled);
    m_dailyShuffle->setChecked(c.dailyShuffleEnabled);
    m_startOnLaunch->setChecked(c.startSchedulerOnLaunch);
    m_timezone->setText(c.timezone);
    m_lat->setValue(c.latitude);
    m_lon->setValue(c.longitude);
    m_backend->blockSignals(true);
    int bi = 0;
    for (int i = 0; i < 5; i++)
        if (QString(kBackendIds[i]) == c.backendOverride)
            bi = i;
    m_backend->setCurrentIndex(bi);
    m_backend->blockSignals(false);
    m_schemeSys->blockSignals(true);
    m_schemeLight->blockSignals(true);
    m_schemeDark->blockSignals(true);
    const int mode = c.themeMode == "light"   ? 1
                     : c.themeMode == "dark" ? 2
                                             : 0;
    m_schemeSys->setChecked(mode == 0);
    m_schemeLight->setChecked(mode == 1);
    m_schemeDark->setChecked(mode == 2);
    m_schemeSys->blockSignals(false);
    m_schemeLight->blockSignals(false);
    m_schemeDark->blockSignals(false);
    m_autostart->blockSignals(true);
    m_autostart->setChecked(c.autostartEnabled);
    m_autostart->blockSignals(false);
    updateBackendHint();
}

config::Config SettingsTab::collect() const {
    config::Config c = m_engine->config();  // read-modify-write
    c.safetyInterval = m_interval->value();
    c.cycleEnabled = m_runCycle->isChecked();
    c.dailyShuffleEnabled = m_dailyShuffle->isChecked();
    c.startSchedulerOnLaunch = m_startOnLaunch->isChecked();
    c.timezone = m_timezone->text().trimmed();
    c.latitude = m_lat->value();
    c.longitude = m_lon->value();
    c.backendOverride = m_backend->currentData().toString();
    c.themeMode = m_schemeLight->isChecked()
                      ? QStringLiteral("light")
                      : m_schemeDark->isChecked() ? QStringLiteral("dark")
                                                  : QStringLiteral("system");
    c.autostartEnabled = m_autostart->isChecked();
    return c;
}

void SettingsTab::updateBackendHint() {
    const QString id = m_backend->currentData().toString();
    if (id != "auto") {
        m_backendHint->setText(
            QStringLiteral("Forces the <b>%1</b> backend even when another "
                           "desktop is available.")
                .arg(m_backend->currentText()));
        return;
    }
    // Auto: show the detected backend (engine thread).
    const int token = ++m_backendToken;
    auto fut = bridge::call<QString>(
        m_engine, [this] { return m_engine->activeBackendName(); });
    fut.then(this, [this, token](QString name) {
        if (token != m_backendToken)
            return;
        if (name.isEmpty() || name == "none")
            m_backendHint->setText(
                QStringLiteral("Auto uses the first available desktop "
                               "backend. Currently detected: <b>none</b>."));
        else
            m_backendHint->setText(
                QStringLiteral("Auto uses the first available desktop "
                               "backend. Currently detected: <b>%1</b>.")
                    .arg(name));
    });
}

void SettingsTab::onSave() {
    config::Config c = collect();
    if (!config::isValidTimezone(c.timezone)) {
        QMessageBox::warning(
            this, QStringLiteral("Invalid Timezone"),
            QStringLiteral("'%1' is not a valid IANA timezone id (e.g. "
                           "America/Phoenix).")
                .arg(c.timezone));
        return;
    }
    if (!config::save(c)) {
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Could not save settings."));
        return;
    }
    // Hot-reload the running engine (cycle interval, shuffle, backend…).
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
    emit settingsSaved();
    emit statusMessage(QStringLiteral("Settings saved"));
}

void SettingsTab::onAutoDetect() {
    m_autoDetectBtn->setEnabled(false);
    auto future = bridge::call<location::Location>(
        m_location, [this] {
            QString source;
            const location::Location loc =
                m_location->detect(m_engine->config(), &source);
            qInfo() << "Location detected via" << source << ":" << loc.latitude
                    << loc.longitude << loc.timezone;
            return loc;
        });
    future.then(this, [this](location::Location loc) {
        m_autoDetectBtn->setEnabled(true);
        m_lat->setValue(loc.latitude);
        m_lon->setValue(loc.longitude);
        if (!loc.timezone.isEmpty())
            m_timezone->setText(loc.timezone);
        emit statusMessage(QStringLiteral("Location detected: %1, %2 (%3)")
                               .arg(loc.latitude, 0, 'f', 4)
                               .arg(loc.longitude, 0, 'f', 4)
                               .arg(loc.timezone));
    });
}

void SettingsTab::onSchemeChanged(int index) {
    const QString name = index == 1
                             ? QStringLiteral("light")
                             : index == 2 ? QStringLiteral("dark")
                                          : QStringLiteral("system");
    // The palette itself is applied by the main window (it owns the
    // system-palette snapshot); persist the preference immediately.
    config::Config c = m_engine->config();
    c.themeMode = name;
    config::save(c);
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
    emit schemeChanged(name);
}

void SettingsTab::onAutostartToggled(bool enabled) {
    // Apply immediately (kWallpaper parity).
    autostart::setEnabled(enabled);
    config::Config c = m_engine->config();
    c.autostartEnabled = enabled;
    config::save(c);
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
}

}  // namespace johona::gui

// ToggleSwitch declares Q_OBJECT in this .cpp: AUTOMOC generates the
// meta-object code into settstab.moc, which must be included here.
#include "settstab.moc"
