// widgets.cpp — see widgets.hpp.

#include "widgets.hpp"

#include <QHash>
#include <QPainterPath>
#include <QPen>

#include "appicons.hpp"
#include "style.hpp"

namespace johona::gui {

// ── StatusDot ────────────────────────────────────────────────────────────

void StatusDot::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = rect().center();
    if (m_on) {
        // 7 px glow (mockup box-shadow 0 0 7px rgba(39,174,96,.9)).
        for (int i = 3; i >= 1; i--) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(39, 174, 96, 26 * (4 - i)));
            p.drawEllipse(c, width() / 2.0 + i * 2, width() / 2.0 + i * 2);
        }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(m_on ? QColor("#27ae60")
                    : QColor(style::current().disabled));
    p.drawEllipse(c, width() / 2.0 - 1, width() / 2.0 - 1);
}

// ── NavItem ──────────────────────────────────────────────────────────────

NavItem::NavItem(const char* iconSvg, const QString& text, QWidget* parent)
    : QPushButton(text, parent), m_iconSvg(iconSvg) {
    setCheckable(true);
    setFixedHeight(34);
    setCursor(Qt::PointingHandCursor);
}

void NavItem::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& tok = style::current();
    const QRectF r(0.5, 0.5, width() - 1, height() - 1);
    const bool active = isChecked();
    if (active) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(tok.highlight));
        p.drawRoundedRect(r, 6, 6);
    } else if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(tok.btnHover));
        p.drawRoundedRect(r, 6, 6);
    }
    const QColor ic =
        active ? QColor(tok.highlightText) : QColor(tok.placeholder);
    static QHash<QString, QPixmap> cache;
    const QString key = QString::fromUtf8(m_iconSvg) + ic.name();
    auto it = cache.constFind(key);
    if (it == cache.constEnd())
        it = cache.insert(key,
                          colorIcon(m_iconSvg, ic, 24).pixmap(17, 17));
    p.drawPixmap(10, (height() - 17) / 2, it.value());

    QFont f;
    f.setPixelSize(13);
    f.setWeight(active ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(active ? tok.highlightText : tok.windowText));
    p.drawText(QRect(10 + 17 + 10, 0, width() - 10 - 17 - 10 - 10, height()),
               Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ── StatusMessageLabel ───────────────────────────────────────────────────

StatusMessageLabel::StatusMessageLabel(QWidget* parent)
    : QLabel(QStringLiteral("Ready"), parent) {
    m_fade.setDuration(160);
    m_fade.setTargetObject(this);
    m_fade.setPropertyName("opacity");
    m_auto.setSingleShot(true);
    connect(&m_auto, &QTimer::timeout, this, [this]() {
        fadeTo(QStringLiteral("Ready"));
    });
    connect(&m_fade, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadingOut) {
            setText(m_pending);
            m_fadingOut = false;
            m_fade.setStartValue(0.0);
            m_fade.setEndValue(1.0);
            m_fade.start();
        }
    });
}

void StatusMessageLabel::showMessage(const QString& text, int timeoutMs) {
    m_auto.stop();
    if (timeoutMs > 0)
        m_auto.start(timeoutMs);
    fadeTo(text);
}

void StatusMessageLabel::fadeTo(const QString& text) {
    m_pending = text;
    if (text == this->text())
        return;
    m_fadingOut = true;
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(0.0);
    m_fade.start();
}

void StatusMessageLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, text());
}

}  // namespace johona::gui
