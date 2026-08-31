// style.hpp — Breeze 6.7 design tokens (mockup/redesign.html) + the
// application stylesheet.
//
// The mockup's CSS variables are the single source of truth for the look.
// The QPalette (mainwindow.cpp) carries the core Breeze colors; the tokens
// here add the extra surfaces the mockup defines (sidebar, frame-bg,
// button borders, …) and are injected into widget stylesheets so both
// light and dark modes work.

#pragma once

#include <QColor>
#include <QMap>
#include <QString>

namespace johona::gui::style {

/// The Breeze 6.7 token set for one color mode.
struct Tokens {
    QString window;        // --window
    QString windowText;    // --window-text
    QString base;          // --base (cards, inputs)
    QString altBase;       // --alt-base
    QString button;        // --button
    QString buttonText;    // --button-text
    QString highlight;     // --highlight (Breeze blue #3daee9)
    QString highlightText; // --highlight-text
    QString red;           // --red
    QString green;         // --green
    QString placeholder;   // --placeholder (muted text)
    QString disabled;      // --disabled
    QString mid;           // --mid
    QString midlight;      // --midlight
    QString frameOutline;  // --frame-outline (card borders)
    QString frameBg;       // --frame-bg (log body, timeline bg)
    QString btnBorder;     // --btn-border
    QString btnHover;      // --btn-hover
    QString btnPressed;    // --btn-pressed
    QString sidebar;       // --sidebar
    QString menuBg;        // --menu-bg
    QString menuHover;     // --menu-hover (rgba)
    QString closeHover;    // --close-hover
    QString closePressed;  // --close-pressed
    QString cardShadow;    // --card-shadow (CSS; reference only)
    QString hoverShadow;   // --hover-shadow (CSS; reference only)

    /// CSS variable map ("--window" → value), for stylesheet building.
    QMap<QString, QString> css() const {
        return {
            {"--window", window},        {"--window-text", windowText},
            {"--base", base},            {"--alt-base", altBase},
            {"--button", button},        {"--button-text", buttonText},
            {"--highlight", highlight},  {"--highlight-text", highlightText},
            {"--red", red},              {"--green", green},
            {"--placeholder", placeholder}, {"--disabled", disabled},
            {"--mid", mid},              {"--midlight", midlight},
            {"--frame-outline", frameOutline},
            {"--frame-bg", frameBg},     {"--btn-border", btnBorder},
            {"--btn-hover", btnHover},   {"--btn-pressed", btnPressed},
            {"--sidebar", sidebar},      {"--menu-bg", menuBg},
            {"--menu-hover", menuHover}, {"--close-hover", closeHover},
            {"--close-pressed", closePressed},
        };
    }
};

/// Breeze 6.7 light tokens (mockup `body[data-theme="light"]`).
inline Tokens light() {
    return {
        "#eff0f1", "#232629", "#ffffff", "#f7f7f7", "#fcfcfc", "#232629",
        "#3daee9", "#ffffff", "#da4453", "#27ae60", "#80848a", "#a0a1a3",
        "#b8babd", "#c8cbce", "#9d9fa1", "#f3f4f5", "#b0b2b4", "#e5f2fb",
        "#c9e6f7", "#e3e5e7", "#f3f4f5", "rgba(61,174,233,.28)", "#ff5264",
        "#b63945", "0 1px 2px rgba(0,0,0,.05)", "0 4px 14px rgba(0,0,0,.13)",
    };
}

/// Breeze 6.7 dark tokens (mockup `body[data-theme="dark"]`).
inline Tokens dark() {
    return {
        "#202326", "#fcfcfc", "#141618", "#1d1f22", "#292c30", "#fcfcfc",
        "#3daee9", "#fcfcfc", "#da4453", "#27ae60", "#8e9297", "#6e7174",
        "#4d5154", "#54585c", "#787a7b", "#1c1f22", "#8b8d8f", "#33414d",
        "#3a5266", "#272c31", "#1c1f22", "rgba(61,174,233,.3)", "#da4453",
        "#b63945", "0 1px 2px rgba(0,0,0,.35)", "0 4px 14px rgba(0,0,0,.5)",
    };
}

/// The token set of the active color mode.  MainWindow updates it in
/// applyAppearance() (light/dark modes, or the mode implied by the system
/// palette's luminance in "system" mode); custom-painted widgets read it
/// for the extra surfaces the QPalette does not carry (frame-outline,
/// frame-bg, …).
inline Tokens& current() {
    static Tokens t = light();
    return t;
}

inline void setTokens(const Tokens& t) { current() = t; }

/// Light or dark tokens, by the luminance of a window background color
/// (used for "system" mode, where the system palette decides).
inline Tokens tokensFor(const QColor& windowColor) {
    return windowColor.lightness() > 128 ? light() : dark();
}

/// Build the application stylesheet from a token set.
///
/// Covers: buttons (default/primary/danger-ghost/small), the pill search
/// field, QMenu (hamburger), thin Breeze scrollbars, and the status-bar
/// separators.  Widgets with bespoke painting (nav items, theme cards,
/// switch, hero, log, cards) style themselves or paint directly.
inline QString buildStyleSheet(const Tokens& t) {
    const auto c = t.css();
    auto v = [&](const char* name) {
        return c.value(QString::fromLatin1(name));
    };

    QString s;
    s.reserve(6000);

    // ── buttons ─────────────────────────────────────────────────────────
    // .btn — default; .btn.primary; .btn.danger-ghost; .btn.small
    s += QStringLiteral(
        "QPushButton { height: 30px; padding: 0 14px; border-radius: 5px;\n"
        "  border: 1px solid %1; background: %2; color: %3;\n"
        "  font-size: 12.5px; }\n"
        "QPushButton:hover:enabled { background: %4; }\n"
        "QPushButton:pressed:enabled { background: %5; }\n"
        "QPushButton:disabled { color: %6; border-color: %7; }\n"
        "QPushButton[cssClass~=\"primary\"] {\n"
        "  background: %8; border-color: %8; color: %9; font-weight: 600; }\n"
        "QPushButton[cssClass~=\"primary\"]:hover:enabled { background: %8; }\n"
        "QPushButton[cssClass~=\"primary\"]:pressed:enabled { background: %8; }\n"
        "QPushButton[cssClass~=\"primary\"]:disabled { color: %9; border-color: %8; }\n"
        "QPushButton[cssClass~=\"danger-ghost\"] {\n"
        "  color: %10; background: transparent; border: none; padding: 0 9px; }\n"
        "QPushButton[cssClass~=\"danger-ghost\"]:hover:enabled {\n"
        "  background: rgba(218,68,83,.12); border: 1px solid rgba(218,68,83,.45); }\n"
        "QPushButton[cssClass~=\"small\"] { height: 26px; padding: 0 10px; font-size: 12px; }\n")
        .arg(v("--btn-border"), v("--button"), v("--button-text"),
             v("--btn-hover"), v("--btn-pressed"), v("--disabled"),
             v("--midlight"), v("--highlight"), v("--highlight-text"),
             v("--red"));

    // Icon-only buttons (title bar hamburger, delete, filter chips).
    s += QStringLiteral(
        "QToolButton { border: none; background: transparent; border-radius: 6px;\n"
        "  padding: 4px; }\n"
        "QToolButton:hover { background: %1; }\n"
        "QToolButton:pressed { background: %2; }\n"
        "QToolButton[cssClass~=\"chip\"] {\n"
        "  height: 22px; padding: 0 11px; border-radius: 11px; font-size: 11px;\n"
        "  border: 1px solid %3; background: %4; color: %5; }\n"
        "QToolButton[cssClass~=\"chip\"]:hover { background: %6; }\n"
        "QToolButton[cssClass~=\"chip\"]:checked {\n"
        "  background: %7; border-color: %7; color: %8; font-weight: 600; }\n")
        .arg(v("--btn-hover"), v("--btn-pressed"), v("--btn-border"),
             v("--button"), v("--window-text"), v("--btn-hover"),
             v("--highlight"), v("--highlight-text"));

    // ── pill search field ───────────────────────────────────────────────
    s += QStringLiteral(
        "QLineEdit[cssClass~=\"search\"] {\n"
        "  height: 30px; padding: 0 12px; border-radius: 15px;\n"
        "  border: 1px solid %1; background: %2; color: %3; font-size: 12.5px; }\n"
        "QLineEdit[cssClass~=\"search\"]:focus {\n"
        "  border: 1px solid %4;\n"
        "  outline: 1px solid %4; }\n")
        .arg(v("--btn-border"), v("--base"), v("--window-text"),
             v("--highlight"));

    // ── input fields (settings rows) ────────────────────────────────────
    s += QStringLiteral(
        "QLineEdit[cssClass~=\"field\"], QComboBox[cssClass~=\"field\"],\n"
        "QSpinBox[cssClass~=\"field\"], QDoubleSpinBox[cssClass~=\"field\"] {\n"
        "  min-height: 28px; padding: 0 9px; border-radius: 5px;\n"
        "  border: 1px solid %1; background: %2; color: %3; font-size: 12.5px; }\n"
        "QLineEdit[cssClass~=\"field\"]:focus, QComboBox[cssClass~=\"field\"]:focus,\n"
        "QSpinBox[cssClass~=\"field\"]:focus, QDoubleSpinBox[cssClass~=\"field\"]:focus {\n"
        "  border: 1px solid %4; outline: 1px solid %4; }\n"
        "QComboBox[cssClass~=\"field\"]::drop-down {\n"
        "  width: 22px; border: none; border-left: 1px solid %1; }\n"
        "QComboBox[cssClass~=\"field\"]::down-arrow {\n"
        "  image: url(:/icons/combobox-down.svg); width: 10px; height: 6px; }\n"
        "QSpinBox[cssClass~=\"field\"]::up-button, QDoubleSpinBox[cssClass~=\"field\"]::up-button,\n"
        "QSpinBox[cssClass~=\"field\"]::down-button, QDoubleSpinBox[cssClass~=\"field\"]::down-button {\n"
        "  width: 18px; border-left: 1px solid %1; background: %2; }\n"
        "QSpinBox[cssClass~=\"field\"]::up-arrow, QDoubleSpinBox[cssClass~=\"field\"]::up-arrow {\n"
        "  image: url(:/icons/spin-up.svg); width: 10px; height: 6px; }\n"
        "QSpinBox[cssClass~=\"field\"]::down-arrow, QDoubleSpinBox[cssClass~=\"field\"]::down-arrow {\n"
        "  image: url(:/icons/spin-down.svg); width: 10px; height: 6px; }\n")
        .arg(v("--btn-border"), v("--base"), v("--window-text"),
             v("--highlight"));

    // ── QMenu (hamburger) ───────────────────────────────────────────────
    s += QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 6px;\n"
        "  padding: 4px; }\n"
        "QMenu::item { padding: 5px 26px 5px 10px; border-radius: 4px;\n"
        "  font-size: 12.5px; background: transparent; }\n"
        "QMenu::item:selected { background: %3; }\n"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 6px; }\n"
        "QMenu::item:disabled { color: %5; }\n")
        .arg(v("--menu-bg"), v("--frame-outline"), v("--menu-hover"),
             v("--midlight"), v("--disabled"));

    // ── thin Breeze scrollbars ──────────────────────────────────────────
    s += QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 8px;\n"
        "  margin: 0; }\n"
        "QScrollBar::handle:vertical { background: %1; border-radius: 4px;\n"
        "  min-height: 24px; margin: 2px; }\n"
        "QScrollBar::handle:vertical:hover { background: %2; }\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {\n"
        "  height: 0; }\n"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {\n"
        "  background: transparent; }\n"
        "QScrollBar:horizontal { background: transparent; height: 8px;\n"
        "  margin: 0; }\n"
        "QScrollBar::handle:horizontal { background: %1; border-radius: 4px;\n"
        "  min-width: 24px; margin: 2px; }\n"
        "QScrollBar::handle:horizontal:hover { background: %2; }\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {\n"
        "  width: 0; }\n"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {\n"
        "  background: transparent; }\n")
        .arg(v("--mid"), v("--frame-outline"));

    // ── status bar ──────────────────────────────────────────────────────
    s += QStringLiteral(
        "QStatusBar { background: %1; border-top: 1px solid %2;\n"
        "  color: %3; font-size: 11.5px; }\n"
        "QStatusBar QLabel { color: %3; font-size: 11.5px; }\n"
        "QWidget[cssClass~=\"sb-sep\"] { background: %2; max-width: 1px;\n"
        "  min-width: 1px; }\n")
        .arg(v("--window"), v("--midlight"), v("--placeholder"));

    // ── labels ──────────────────────────────────────────────────────────
    // Many labels set a widget-specific palette (WindowText =
    // PlaceholderText) at construction for muted text.  When the app
    // palette changes (theme switch), those widget palettes are not
    // updated, leaving stale colors.  A global stylesheet rule overrides
    // the palette for ALL QLabels with the token's windowText; the muted
    // rule (more specific) then re-applies the placeholder color.  Both
    // are regenerated on every applyAppearance() call.
    s += QStringLiteral(
        "QLabel { color: %1; }\n"
        "QLabel[cssClass~=\"muted\"] { color: %2; }\n")
        .arg(v("--window-text"), v("--placeholder"));

    // ── title bar & sidebar ─────────────────────────────────────────────
    s += QStringLiteral(
        "QWidget[cssClass~=\"titlebar\"] { background: %1;\n"
        "  border-bottom: 1px solid %2; }\n"
        "QWidget[cssClass~=\"sidebar\"] { background: %1;\n"
        "  border-right: 1px solid %2; }\n"
        "QFrame[cssClass~=\"brand-icon\"] { background: %3;\n"
        "  border: 1px solid %4; border-radius: 9px; }\n")
        .arg(v("--sidebar"), v("--midlight"), v("--base"),
             v("--frame-outline"));

    // ── cards & log ─────────────────────────────────────────────────────
    s += QStringLiteral(
        "QWidget[cssClass~=\"card\"] { background: %1; border: 1px solid %2;\n"
        "  border-radius: 8px; }\n"
        "QWidget[cssClass~=\"log-head\"] { background: transparent;\n"
        "  border-bottom: 1px solid %3; }\n"
        "QListWidget[cssClass~=\"log-body\"] { background: %4;\n"
        "  border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }\n"
        "QWidget[cssClass~=\"vline\"] { background: %3; border: none; }\n"
        "QWidget[cssClass~=\"row\"] { background: transparent;\n"
        "  border-top: 1px solid %4; }\n")
        .arg(v("--base"), v("--frame-outline"), v("--midlight"),
             v("--frame-bg"));

    return s;
}

}  // namespace johona::gui::style
