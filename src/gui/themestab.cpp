// themestab.cpp — see themestab.hpp (redesign mockup Themes page).

#include "themestab.hpp"

#include <algorithm>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QListWidgetItem>
#include <QPair>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "imageworkers.hpp"
#include "previewwidget.hpp"
#include "schedulepreview.hpp"
#include "style.hpp"
#include "themes.hpp"

namespace johona::gui {

namespace {

// ── theme card delegate (mockup .theme-card) ────────────────────────────

class ThemeCardDelegate : public QStyledItemDelegate {
public:
    ThemeCardDelegate(QListWidget* list, QHash<QString, QPixmap>* thumbs,
                      QObject* parent)
        : QStyledItemDelegate(parent), m_list(list), m_thumbs(thumbs) {}

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index)
        const override {
        Q_UNUSED(opt);
        Q_UNUSED(index);
        const int w = qMax(120, m_list->viewport()->width());
        const int thumbH = qRound(w * 9.0 / 16.0);
        return QSize(w, thumbH + 31);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override {
        const QRect r = opt.rect;
        if (r.width() < 20 || r.height() < 40)
            return;
        const auto& tok = style::current();
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;

        p->setRenderHint(QPainter::Antialiasing);
        const int radius = 8;
        const QRectF cardR(r.x() + 0.5, r.y() + 0.5, r.width() - 1,
                           r.height() - 1);
        QPainterPath card;
        card.addRoundedRect(cardR, radius, radius);

        // Selected: 2 px highlight border + soft blue glow (mockup
        // .theme-card.selected).  Hover: shadow lift (.theme-card:hover).
        if (selected) {
            const QColor hl(tok.highlight);
            for (int i = 3; i >= 1; i--) {
                p->setPen(QPen(QColor(hl.red(), hl.green(), hl.blue(), 12 * i),
                               1.0));
                p->drawRoundedRect(cardR.adjusted(-i, -i + 1, i, i - 1),
                                   radius + i, radius + i);
            }
        } else if (hovered) {
            p->setPen(QPen(QColor(0, 0, 0, 33), 1.0));
            p->drawRoundedRect(cardR.adjusted(-1, 0, 1, 2), radius + 1,
                               radius + 1);
        }

        p->fillPath(card, QColor(tok.base));
        p->setPen(QPen(QColor(tok.frameOutline), selected ? 2.0 : 1.0));
        p->drawPath(card);

        // ── 16:9 thumbnail (cover-cropped, dark letterbox) ──────────────
        const int thumbH = qRound(r.width() * 9.0 / 16.0);
        const QRectF thumbR(r.x() + 1, r.y() + 1, r.width() - 2, thumbH - 1);
        QPainterPath clip;
        clip.moveTo(thumbR.x(), thumbR.bottom());
        clip.lineTo(thumbR.x(), thumbR.y() + radius - 1);
        clip.quadTo(thumbR.x(), thumbR.y(), thumbR.x() + radius - 1,
                    thumbR.y());
        clip.lineTo(thumbR.right() - radius + 1, thumbR.y());
        clip.quadTo(thumbR.right(), thumbR.y(), thumbR.right(),
                    thumbR.y() + radius - 1);
        clip.lineTo(thumbR.right(), thumbR.bottom());
        clip.closeSubpath();
        p->save();
        p->setClipPath(clip);
        p->fillRect(thumbR, QColor("#0a0d14"));
        const QString path = index.data(Qt::UserRole).toString();
        auto it = m_thumbs->constFind(path);
        if (it != m_thumbs->constEnd() && !it.value().isNull()) {
            const QPixmap scaled = it.value().scaled(
                thumbR.width(), thumbR.height(),
                Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const int ox = thumbR.x() + (thumbR.width() - scaled.width()) / 2;
            const int oy = thumbR.y() + (thumbR.height() - scaled.height()) / 2;
            p->drawPixmap(ox, oy, scaled);
        }
        p->restore();

        // ── ACTIVE badge (mockup .active-badge) ─────────────────────────
        if (index.data(Qt::UserRole + 2).toBool()) {
            QFont bf;
            bf.setPixelSize(10);
            bf.setWeight(QFont::Bold);
            p->setFont(bf);
            const QFontMetrics bfm(bf);
            const int badgeH = 16;
            const int badgeW = 6 + 10 + 4 + bfm.horizontalAdvance("ACTIVE") + 8;
            const QRectF badge(r.x() + 7, r.y() + 7, badgeW, badgeH);
            QPainterPath bp;
            bp.addRoundedRect(badge, 8, 8);
            p->fillPath(bp, QColor(61, 174, 233, 240));  // rgba(61,174,233,.94)
            p->setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap,
                           Qt::RoundJoin));
            const double cx0 = badge.x() + 6;
            const double cy0 = badge.center().y();
            p->drawLine(QPointF(cx0, cy0), QPointF(cx0 + 3.5, cy0 + 3));
            p->drawLine(QPointF(cx0 + 3.5, cy0 + 3), QPointF(cx0 + 9, cy0 - 4));
            p->setPen(Qt::white);
            p->drawText(QRectF(badge.x() + 20, badge.y(), badgeW - 20,
                               badgeH),
                        Qt::AlignVCenter | Qt::AlignLeft, "ACTIVE");
        }

        // ── info row: name (left) + image icon + count (right) ──────────
        const int infoY = r.y() + thumbH;
        QFont nf;
        nf.setPixelSize(13);  // 12.5 px mockup
        nf.setWeight(QFont::DemiBold);
        p->setFont(nf);
        const QFontMetrics nfm(nf);
        const QString name = index.data(Qt::DisplayRole).toString();
        p->setPen(QColor(tok.windowText));
        p->drawText(QRect(r.x() + 10, infoY, r.width() - 70, 31),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    nfm.elidedText(name, Qt::ElideRight, r.width() - 70));

        const int count = index.data(Qt::UserRole + 1).toInt();
        QFont cf;
        cf.setPixelSize(11);  // 10.5 px mockup
        p->setFont(cf);
        const QFontMetrics cfm(cf);
        const QString countText = QString::number(count);
        const int iconSize = 11;
        const int rightX = r.right() - 10;
        p->setPen(QColor(tok.placeholder));
        p->drawText(rightX - cfm.horizontalAdvance(countText), infoY,
                    cfm.horizontalAdvance(countText), 31,
                    Qt::AlignVCenter | Qt::AlignRight, countText);
        const QPixmap icon = countIcon(tok.placeholder);
        p->drawPixmap(rightX - cfm.horizontalAdvance(countText) - 4 - iconSize,
                      infoY + (31 - iconSize) / 2, icon);
    }

private:
    static QPixmap countIcon(const QString& color) {
        static QHash<QString, QPixmap> cache;
        auto it = cache.constFind(color);
        if (it == cache.constEnd())
            it = cache.insert(
                color, colorIcon(kNavThemesSvg, QColor(color), 22).pixmap(11, 11));
        return it.value();
    }

    QListWidget* m_list;
    QHash<QString, QPixmap>* m_thumbs;
};

/// Decode a theme card thumbnail with long edge ≤ `target`.  The
/// downscale happens inside the decoder (setScaledSize), so no
/// full-resolution buffer is ever allocated.  Worker.
/// autoTransform is skipped: EXIF rotation is not needed for a small
/// card preview and it forces a full-res decode + rotate pass.
QImage decodeCardThumb(const QString& path, int target) {
    QImageReader reader(path);
    reader.setAutoTransform(false);
    const QSize sz = reader.size();
    if (sz.isValid()) {
        if (sz.width() >= sz.height())
            reader.setScaledSize(QSize(target, -1));
        else
            reader.setScaledSize(QSize(-1, target));
    }
    return reader.read();
}

}  // namespace

ThemesTab::ThemesTab(Engine* engine, const config::Paths& paths,
                     QWidget* parent)
    : QWidget(parent), m_engine(engine), m_paths(paths) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── header: title + count, pill search, Import ─────────────────────
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(20, 14, 20, 10);
    hl->setSpacing(12);

    auto* titleBox = new QVBoxLayout();
    titleBox->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Themes"), head);
    {
        QFont f;
        f.setPixelSize(17);
        f.setWeight(QFont::Bold);
        title->setFont(f);
    }
    m_countLabel = new QLabel(QStringLiteral("0 installed"), head);
    m_countLabel->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(12);
        m_countLabel->setFont(f);
    }
    titleBox->addWidget(title);
    titleBox->addWidget(m_countLabel);
    hl->addLayout(titleBox);

    m_search = new QLineEdit(head);
    m_search->setProperty("cssClass", "search");
    m_search->setPlaceholderText(QStringLiteral("Search themes…"));
    m_search->setFixedWidth(210);
    m_search->addAction(
        colorIcon(kSearchSvg, QColor("#80848a"), 24).pixmap(14, 14),
        QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this,
            &ThemesTab::onSearchTextChanged);
    hl->addWidget(m_search);

    hl->addStretch(1);
    m_importBtn = new QPushButton(
        colorIcon(kImportSvg, Qt::white, 24).pixmap(15, 15),
        QStringLiteral("Import Theme…"), head);
    m_importBtn->setProperty("cssClass", "primary");
    m_importBtn->setToolTip(
        QStringLiteral("Import a .ddw or .zip theme file"));
    connect(m_importBtn, &QPushButton::clicked, this, &ThemesTab::onImport);
    hl->addWidget(m_importBtn);
    root->addWidget(head);

    // ── body: card list + preview panel ────────────────────────────────
    auto* layout = new QWidget(this);
    auto* ll = new QHBoxLayout(layout);
    ll->setContentsMargins(20, 0, 20, 14);
    ll->setSpacing(14);

    m_list = new QListWidget(layout);
    m_list->setFixedWidth(250);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setSpacing(10);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);
    m_list->setStyleSheet(QStringLiteral("background: transparent;"));
    m_list->setItemDelegate(new ThemeCardDelegate(m_list, &m_thumbs, m_list));
    connect(m_list, &QListWidget::currentItemChanged, this,
            &ThemesTab::onSelectionChanged);
    ll->addWidget(m_list);

    m_emptyLabel =
        new QLabel(QString(), m_list->viewport());
    m_emptyLabel->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(12);
        m_emptyLabel->setFont(f);
    }
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    m_list->viewport()->installEventFilter(this);

    // Right panel
    auto* panel = new QWidget(layout);
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);

    auto* phead = new QHBoxLayout();
    phead->setContentsMargins(2, 2, 2, 8);
    phead->setSpacing(10);
    auto* nameBox = new QVBoxLayout();
    nameBox->setSpacing(2);
    m_nameLabel = new QLabel(QString(), panel);
    {
        QFont f;
        f.setPixelSize(14);
        f.setWeight(QFont::Bold);
        m_nameLabel->setFont(f);
    }
    m_metaLabel = new QLabel(QString(), panel);
    m_metaLabel->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(12);  // 11.5 px mockup
        m_metaLabel->setFont(f);
    }
    nameBox->addWidget(m_nameLabel);
    nameBox->addWidget(m_metaLabel);
    phead->addLayout(nameBox);
    phead->addStretch(1);

    m_deleteWarning =
        new QLabel(QStringLiteral("Stop the scheduler to delete themes"),
                   panel);
    {
        QFont f;
        f.setPixelSize(11);
        m_deleteWarning->setFont(f);
    }
    m_deleteWarning->setStyleSheet(QStringLiteral("color: #da4453;"));
    m_deleteWarning->hide();
    phead->addWidget(m_deleteWarning);

    m_deleteBtn = new QPushButton(panel);
    m_deleteBtn->setIcon(
        colorIcon(kTrashSvg, QColor("#da4453"), 24).pixmap(15, 15));
    m_deleteBtn->setProperty("cssClass", "danger-ghost");
    m_deleteBtn->setFixedSize(30, 30);
    m_deleteBtn->setToolTip(QStringLiteral("Delete theme"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &ThemesTab::onDelete);
    phead->addWidget(m_deleteBtn);

    m_applyBtn = new QPushButton(
        colorIcon(kCheckSvg, Qt::white, 24).pixmap(15, 15),
        QStringLiteral("Apply"), panel);
    m_applyBtn->setProperty("cssClass", "primary");
    m_applyBtn->setToolTip(
        QStringLiteral("Set the selected theme as your wallpaper"));
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &ThemesTab::onApply);
    phead->addWidget(m_applyBtn);
    pv->addLayout(phead);

    m_preview = new PreviewWidget(panel);
    pv->addWidget(m_preview, 1);

    pv->addSpacing(10);
    auto* schedCard = new QFrame(panel);
    schedCard->setProperty("cssClass", "card");
    auto* scl = new QVBoxLayout(schedCard);
    scl->setContentsMargins(12, 9, 12, 7);
    scl->setSpacing(0);
    m_schedule = new SchedulePreview(schedCard);
    scl->addWidget(m_schedule);
    pv->addWidget(schedCard, 0);

    ll->addWidget(panel, 1);
    root->addWidget(layout, 1);
}

void ThemesTab::refresh() {
    const QString prevSelected = selectedThemePath();
    m_list->clear();
    m_imageCache.clear();
    m_thumbs.clear();
    m_thumbLoading.clear();
    ++m_thumbToken;  // cancel in-flight decodes from the old list

    const auto themes = themes::discoverThemes(m_paths.themesDir);
    std::vector<themes::ThemeInfo> sorted(themes.begin(), themes.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const themes::ThemeInfo& a, const themes::ThemeInfo& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    const QString active = m_engine->config().lastApplied;
    for (const auto& t : sorted) {
        auto* item = new QListWidgetItem(t.displayName);
        item->setData(Qt::UserRole, t.path);
        item->setData(Qt::UserRole + 1, t.imageCount);
        item->setData(Qt::UserRole + 2, t.name == active);
        m_list->addItem(item);
    }
    // Restore the previous selection (import/delete), else the first card.
    for (int i = 0; i < m_list->count(); i++)
        if (m_list->item(i)->data(Qt::UserRole).toString() == prevSelected) {
            m_list->setCurrentRow(i);
            break;
        }
    if (!m_list->currentItem())
        m_list->setCurrentRow(0);
    applyFilter();
    requestThumbs();
}

QString ThemesTab::selectedThemePath() const {
    const auto* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QStringList ThemesTab::imagesFor(const QString& themePath) const {
    auto it = m_imageCache.constFind(themePath);
    if (it != m_imageCache.constEnd())
        return it.value();
    QStringList result;
    if (auto data = themes::loadThemeData(themePath))
        for (const QString& f : themes::imageFilesFor(themePath, *data))
            result << f;
    m_imageCache.insert(themePath, result);
    return result;
}

void ThemesTab::onSearchTextChanged(const QString& text) {
    Q_UNUSED(text);
    applyFilter();
}

void ThemesTab::applyFilter() {
    const QString q = m_search->text().trimmed().toLower();
    int shown = 0;
    for (int i = 0; i < m_list->count(); i++) {
        auto* item = m_list->item(i);
        const bool ok = q.isEmpty() || item->text().toLower().contains(q);
        item->setHidden(!ok);
        if (ok)
            shown++;
    }
    m_countLabel->setText(QStringLiteral("%1 installed").arg(shown));
    updateEmptyState();
}

void ThemesTab::updateEmptyState() {
    int visible = 0;
    for (int i = 0; i < m_list->count(); i++)
        if (!m_list->item(i)->isHidden())
            visible++;
    if (visible > 0) {
        m_emptyLabel->hide();
        return;
    }
    m_emptyLabel->setText(m_list->count() == 0
                              ? QStringLiteral(
                                    "No themes installed yet. Import a "
                                    ".ddw or .zip pack.")
                              : QStringLiteral("No themes match your "
                                               "search."));
    m_emptyLabel->setGeometry(0, 0, m_list->viewport()->width(),
                              m_list->viewport()->height());
    m_emptyLabel->show();
}

bool ThemesTab::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_list->viewport() && event->type() == QEvent::Resize) {
        // Card sizes depend on the viewport width (scrollbar on/off).
        m_list->doItemsLayout();
        if (m_emptyLabel->isVisible())
            m_emptyLabel->setGeometry(
                0, 0, m_list->viewport()->width(),
                m_list->viewport()->height());
    }
    return QWidget::eventFilter(obj, event);
}

void ThemesTab::requestThumbs() {
    const int token = m_thumbToken;
    // Adaptive card-thumb long edge: cards render at the list viewport
    // width (250 logical px list, minus scrollbar).  Scale by the device
    // pixel ratio; floor 250 so 1x displays keep the old 250-px minimum,
    // cap 500 for 2x HiDPI headroom.
    double dpr = devicePixelRatioF();
    if (dpr <= 0.0)
        dpr = 1.0;
    const int thumbTarget =
        qMax(250, qMin(500, static_cast<int>(m_list->viewport()->width() * dpr)));
    QPointer<ThemesTab> guard = this;
    for (int i = 0; i < m_list->count(); i++) {
        auto* item = m_list->item(i);
        const QString path = item->data(Qt::UserRole).toString();
        if (m_thumbs.contains(path) || m_thumbLoading.contains(path))
            continue;
        const QStringList imgs = imagesFor(path);
        if (imgs.isEmpty()) {
            m_thumbs.insert(path, QPixmap());  // no image → letterbox only
            continue;
        }
        m_thumbLoading.insert(path);
        const QString img = imgs.first();
        // One task per theme so all pool threads stay busy in parallel.
        imageDecodePool()->start(
            [guard, token, img, path, thumbTarget]() {
                const QPixmap thumb =
                    QPixmap::fromImage(decodeCardThumb(img, thumbTarget));
                if (!guard)
                    return;
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, token, path, thumb = std::move(thumb)]() mutable {
                        if (guard)
                            guard->onThumbsReady(token, path, std::move(thumb));
                    },
                    Qt::QueuedConnection);
            });
    }
}

void ThemesTab::onThumbsReady(int token, const QString& path,
                              QPixmap thumb) {
    if (token != m_thumbToken)
        return;  // superseded (list refreshed)
    m_thumbLoading.remove(path);
    m_thumbs.insert(path, std::move(thumb));
    m_list->viewport()->update();
}

void ThemesTab::onSelectionChanged() {
    const QString path = selectedThemePath();
    if (path.isEmpty()) {
        m_applyBtn->setEnabled(false);
        m_nameLabel->clear();
        m_metaLabel->clear();
        m_preview->setThemeName(QString());
        m_preview->setImages({});
        m_schedule->clear();
        return;
    }
    m_applyBtn->setEnabled(true);
    const auto* item = m_list->currentItem();
    const QString name = item->text();
    const int count = item->data(Qt::UserRole + 1).toInt();
    m_nameLabel->setText(name);
    m_metaLabel->setText(
        QStringLiteral("%1 images · 4 segments").arg(count));
    const QStringList imgs = imagesFor(path);
    m_preview->setThemeName(name);
    m_preview->setImages(imgs);
    m_preview->start();
    refreshSchedule();
}

void ThemesTab::refreshSchedule() {
    const QString path = selectedThemePath();
    if (path.isEmpty()) {
        m_schedule->clear();
        return;
    }
    m_schedule->refresh(m_engine->config(), path);
}

void ThemesTab::rebuildPreview() {
    onSelectionChanged();
}

void ThemesTab::setSchedulerRunning(bool running) {
    if (running) {
        m_deleteBtn->setEnabled(false);
        m_deleteWarning->setVisible(true);
    } else {
        m_deleteBtn->setEnabled(true);
        m_deleteWarning->setVisible(false);
    }
}

void ThemesTab::setTabVisible(bool visible) {
    if (visible) {
        const QString path = selectedThemePath();
        if (!path.isEmpty())
            m_preview->setImages(imagesFor(path));
        m_preview->start();
        // The schedule marker may be stale after being hidden.
        m_schedule->refreshNow();
    } else {
        m_preview->stop();
    }
}

void ThemesTab::setBusy(QPushButton* btn, bool busy) {
    btn->setEnabled(!busy);
    if (btn == m_applyBtn)
        btn->setText(busy ? QStringLiteral("Working…")
                          : QStringLiteral("Apply"));
    else if (btn == m_importBtn)
        btn->setText(busy ? QStringLiteral("Working…")
                          : QStringLiteral("Import Theme…"));
}

void ThemesTab::importTheme() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Import Theme"), QString(),
        QStringLiteral("Theme Files (*.ddw *.zip);;All Files (*)"));
    if (files.isEmpty())
        return;
    setBusy(m_importBtn, true);

    const QString themesDir = m_paths.themesDir;
    auto fut = bridge::call<themes::ImportResult>(
        m_engine, [files, themesDir]() {
            // Aggregate per-file results (kWallpaper parity: report all).
            themes::ImportResult last;
            int imported = 0, failed = 0;
            QStringList errors;
            for (const QString& f : files) {
                const themes::ImportResult r =
                    themes::importTheme(f, themesDir);
                if (r.success) {
                    ++imported;
                } else {
                    ++failed;
                    errors << QStringLiteral("%1: %2")
                               .arg(QFileInfo(f).fileName(), r.message);
                }
                last = r;
            }
            themes::ImportResult agg;
            agg.success = (failed == 0);
            agg.themePath = last.themePath;
            agg.displayName = last.displayName;
            agg.missingImages = last.missingImages;
            QString msg =
                QStringLiteral("%1 theme(s) imported successfully")
                    .arg(imported);
            if (failed > 0)
                msg += QStringLiteral("; %1 failed").arg(failed);
            agg.message = msg;
            if (!errors.isEmpty())
                agg.message += QStringLiteral("\n") + errors.join("\n");
            return agg;
        });
    fut.then(this, [this](themes::ImportResult result) {
        setBusy(m_importBtn, false);
        refresh();
        emit statusMessage(result.message);
        if (!result.success)
            QMessageBox::warning(this, QStringLiteral("Import Failed"),
                                 result.message);
    });
}

void ThemesTab::onApply() {
    const QString path = selectedThemePath();
    if (path.isEmpty())
        return;
    const QString folder = QFileInfo(path).fileName();

    // Daily shuffle enabled → confirm (kWallpaper parity).
    if (m_engine->config().dailyShuffleEnabled) {
        const auto reply = QMessageBox::question(
            this, QStringLiteral("Confirm Theme Apply"),
            QStringLiteral("Daily shuffle is enabled. If you apply this "
                           "theme, a new shuffle list will be created. "
                           "Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
    }

    setBusy(m_applyBtn, true);
    auto future = bridge::call<ApplyOutcome>(
        m_engine, [this, folder] { return m_engine->applyTheme(folder); });
    future.then(this, [this](ApplyOutcome out) {
        setBusy(m_applyBtn, false);
        if (!out.success) {
            QMessageBox::warning(this, QStringLiteral("Apply Failed"),
                                 out.message);
            return;
        }
        // Confirm success (the wallpaper may not change visibly if the
        // same image was already set).
        QMessageBox::information(
            this, QStringLiteral("Wallpaper Applied"),
            QStringLiteral("Applied: %1").arg(out.themeName));
        refresh();  // move the ACTIVE badge to the applied theme
    });
}

void ThemesTab::onDelete() {
    const auto* item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->text();
    const QString path = item->data(Qt::UserRole).toString();

    const auto reply = QMessageBox::question(
        this, QStringLiteral("Delete Theme"),
        QStringLiteral("Delete theme '%1'? This cannot be undone.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    setBusy(m_deleteBtn, true);
    const QString themesDir = m_paths.themesDir;
    auto fut = bridge::call<themes::DeleteResult>(
        m_engine, [path, themesDir] {
            return themes::deleteTheme(path, themesDir);
        });
    fut.then(this, [this, name](themes::DeleteResult result) {
        setBusy(m_deleteBtn, false);
        refresh();
        if (!result.success) {
            QMessageBox::warning(this, QStringLiteral("Delete Failed"),
                                 result.message);
            return;
        }
        emit statusMessage(
            QStringLiteral("Theme '%1' deleted successfully").arg(name));
    });
}

}  // namespace johona::gui
