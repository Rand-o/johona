// themestab.cpp — see themestab.hpp (kWallpaper ThemesPage parity).

#include "themestab.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "previewwidget.hpp"
#include "schedulepreview.hpp"
#include "themes.hpp"

namespace johona::gui {

ThemesTab::ThemesTab(Engine* engine, const config::Paths& paths,
                     QWidget* parent)
    : QWidget(parent), m_engine(engine), m_paths(paths) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setChildrenCollapsible(false);
    root->addWidget(m_split);

    // Left: theme list + buttons
    auto* left = new QWidget(m_split);
    left->setMinimumWidth(200);  // smaller initial width for the list
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(6);

    m_list = new QListWidget(left);
    m_list->setAlternatingRowColors(true);
    connect(m_list, &QListWidget::currentItemChanged, this,
            &ThemesTab::onSelectionChanged);
    lv->addWidget(m_list);

    auto* brow = new QHBoxLayout();
    brow->setSpacing(6);
    m_importBtn = new QPushButton(
        themeIcon(QStringLiteral("document-import"), kFallbackImportSvg),
        QStringLiteral("Import…"), left);
    m_importBtn->setToolTip(
        QStringLiteral("Import a .ddw or .zip theme file"));
    connect(m_importBtn, &QPushButton::clicked, this, &ThemesTab::onImport);
    brow->addWidget(m_importBtn);

    m_applyBtn = new QPushButton(
        themeIcon(QStringLiteral("dialog-ok-apply"), kFallbackApplySvg),
        QStringLiteral("Apply"), left);
    m_applyBtn->setToolTip(
        QStringLiteral("Set the selected theme as your wallpaper"));
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &ThemesTab::onApply);
    brow->addWidget(m_applyBtn);

    m_deleteBtn = new QPushButton(
        themeIcon(QStringLiteral("user-trash"), kFallbackTrashSvg),
        QStringLiteral("Delete"), left);
    m_deleteBtn->setToolTip(QStringLiteral("Delete the selected theme"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &ThemesTab::onDelete);
    brow->addWidget(m_deleteBtn);

    const int btnSize = 75;  // compact buttons (kWallpaper parity)
    m_importBtn->setMaximumWidth(btnSize);
    m_applyBtn->setMaximumWidth(btnSize);
    m_deleteBtn->setMaximumWidth(btnSize);

    lv->addLayout(brow);

    m_deleteWarning =
        new QLabel(QStringLiteral("Scheduler must be stopped to delete "
                                  "themes"),
                   left);
    m_deleteWarning->setStyleSheet(QStringLiteral("color: red;"));
    m_deleteWarning->setVisible(false);
    lv->addWidget(m_deleteWarning);

    m_split->addWidget(left);

    // Right: cross-fade preview + info + schedule
    auto* right = new QWidget(m_split);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(4);

    m_preview = new PreviewWidget(right);
    rv->addWidget(m_preview, 1);

    m_previewInfo = new QLabel(QString(), right);
    m_previewInfo->setAlignment(Qt::AlignCenter);
    rv->addWidget(m_previewInfo);

    m_schedule = new SchedulePreview(right);
    rv->addWidget(m_schedule);

    m_split->addWidget(right);
    m_split->setSizes({150, 850});
}

void ThemesTab::refresh() {
    m_list->clear();
    m_imageCache.clear();

    const auto themes = themes::discoverThemes(m_paths.themesDir);
    std::vector<themes::ThemeInfo> sorted(themes.begin(), themes.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const themes::ThemeInfo& a, const themes::ThemeInfo& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    for (const auto& t : sorted) {
        auto* item = new QListWidgetItem(t.displayName);
        item->setData(Qt::UserRole, t.path);
        m_list->addItem(item);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
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

void ThemesTab::onSelectionChanged() {
    const QString path = selectedThemePath();
    if (path.isEmpty()) {
        m_applyBtn->setEnabled(false);
        m_preview->setImages({});
        m_previewInfo->clear();
        m_schedule->clear();
        return;
    }
    m_applyBtn->setEnabled(true);
    const QStringList imgs = imagesFor(path);
    m_preview->setImages(imgs);
    m_preview->start();
    m_previewInfo->setText(imgs.isEmpty()
                               ? QString()
                               : QStringLiteral("%1 images").arg(imgs.size()));
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
    if (busy) {
        btn->setText(QStringLiteral("Working…"));
    } else if (btn == m_applyBtn) {
        btn->setText(QStringLiteral("Apply"));
    } else if (btn == m_importBtn) {
        btn->setText(QStringLiteral("Import…"));
    } else {
        btn->setText(QStringLiteral("Delete"));
    }
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
    const QString name = m_list->currentItem()->text();
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
