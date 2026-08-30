// widgets.hpp — small shared custom-painted widgets (redesign mockup):
// StatusDot (glowing green/gray dot), NavItem (sidebar nav pill), and
// StatusMessageLabel (fading status-bar message).

#pragma once

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace johona::gui {

/// 8–10 px status dot with a soft glow when on (mockup .dot).
class StatusDot : public QWidget {
public:
    explicit StatusDot(int size, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(size, size);
    }

    void setOn(bool on) {
        if (m_on != on) {
            m_on = on;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool m_on = false;
};

/// Sidebar navigation item: 34 px pill, placeholder icon + 13 px label;
/// active = Breeze blue pill with white icon/text (mockup .nav-item).
class NavItem : public QPushButton {
public:
    NavItem(const char* iconSvg, const QString& text, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const char* m_iconSvg;
};

/// Status-bar message label: fades out/in on change, auto-clears to
/// "Ready" after the timeout (mockup #sb-msg).
class StatusMessageLabel : public QLabel {
    Q_OBJECT
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity)
public:
    explicit StatusMessageLabel(QWidget* parent = nullptr);

    /// Show `text`, fading over 160 ms; auto-clear to "Ready" after
    /// `timeoutMs` (0 = no auto-clear).
    void showMessage(const QString& text, int timeoutMs = 5000);

    double opacity() const { return m_opacity; }
    void setOpacity(double v) {
        m_opacity = v;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void fadeTo(const QString& text);

    double m_opacity = 1.0;
    QString m_pending;
    bool m_fadingOut = false;
    QPropertyAnimation m_fade;
    QTimer m_auto;
};

}  // namespace johona::gui
