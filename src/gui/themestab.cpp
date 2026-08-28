// themestab.cpp — see themestab.hpp.

#include "themestab.hpp"

#include <QDateTime>
#include <QFileDialog>
#include <QFuture>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "enginebridge.hpp"
#include "previewwidget.hpp"
#include "schedulepreview.hpp"
#include "solar.hpp"
#include "themes.hpp"

namespace johona::gui {

namespace {

int categorySlot(solar::Category c) {
    switch (c) {
    case solar::Category::Sunrise:
        return 0;
    case solar::Category::Day:
        return 1;
    case solar::Category::Sunset:
        return 2;
    case solar::Category::Night:
        return 3;
    }
    return -1;
}

solar::ThemeImageLists listsOf(const themes::ThemeData& d) {
    solar::ThemeImageLists l;
    l.sunrise = d.sunriseImageList;
    l.day = d.dayImageList;
    l.sunset = d.sunsetImageList;
    l.night = d.nightImageList;
    return l;
}

/// The image path shown in each category at `nowMs` (fallback: the
/// category's first image).
QStringList perCategoryImages(const QString& themeDir, const themes::ThemeData& data,
                              const solar::Segments& seg, double nowMs,
                              QString* currentCategory) {
    const auto lists = listsOf(data);
    const auto windows = solar::effectiveWindows(seg, lists);
    QStringList per(4, QString());
    // First image of each list as fallback.
    const auto first = [&](int slot, const std::vector<int>& list) {
        if (!list.empty())
            per[slot] = themes::imageFileFor(themeDir, data, list.front());
    };
    first(0, lists.sunrise);
    first(1, lists.day);
    first(2, lists.sunset);
    first(3, lists.night);

    int position[4] = {0, 0, 0, 0};
    for (const auto& w : windows) {
        const int slot = categorySlot(w.category);
        if (slot < 0)
            continue;
        const std::vector<int>& list =
            slot == 0 ? lists.sunrise : slot == 1 ? lists.day :
            slot == 2 ? lists.sunset : lists.night;
        if (position[slot] < static_cast<int>(list.size()))
            per[slot] = themes::imageFileFor(themeDir, data, list[position[slot]]);
        position[slot]++;
        if (nowMs >= w.start && nowMs < w.end && currentCategory)
            *currentCategory = solar::categoryName(w.category);
    }
    return per;
}

}  // namespace

ThemesTab::ThemesTab(Engine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine) {
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumWidth(260);

    auto* buttons = new QVBoxLayout;
    m_importBtn = new QPushButton(tr("Import…"), this);
    m_applyBtn = new QPushButton(tr("Apply"), this);
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    m_nextBtn = new QPushButton(tr("Next wallpaper"), this);
    for (auto* b : {m_importBtn, m_applyBtn, m_deleteBtn, m_nextBtn}) {
        b->setEnabled(false);
        buttons->addWidget(b);
    }
    buttons->addStretch(1);

    m_preview = new PreviewWidget(this);
    m_schedule = new SchedulePreview(this);
    auto* right = new QVBoxLayout;
    right->addWidget(m_preview, 3);
    right->addWidget(m_schedule, 1);

    auto* left = new QHBoxLayout;
    left->addWidget(m_list, 3);
    left->addLayout(buttons, 1);

    auto* root = new QHBoxLayout(this);
    root->addLayout(left, 2);
    root->addLayout(right, 3);

    connect(m_importBtn, &QPushButton::clicked, this, &ThemesTab::onImport);
    connect(m_applyBtn, &QPushButton::clicked, this, &ThemesTab::onApply);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ThemesTab::onDelete);
    connect(m_nextBtn, &QPushButton::clicked, this, &ThemesTab::onNext);
    connect(m_list, &QListWidget::currentItemChanged, this,
            &ThemesTab::onSelectionChanged);
    connect(m_engine, &Engine::applied, this, &ThemesTab::onApplied);
    connect(m_preview, &PreviewWidget::categoryChanged, this,
            [this](const QString& cat) {
                if (!cat.isEmpty())
                    emit statusMessage(tr("Now showing: %1").arg(cat));
            });

    refresh();
}

void ThemesTab::refresh() {
    const QString dir = m_engine->paths().themesDir;
    const auto themes = themes::discoverThemes(dir);
    m_list->clear();
    for (const auto& t : themes) {
        auto* item = new QListWidgetItem(
            tr("%1  (%2 images)").arg(t.displayName.isEmpty() ? t.name : t.displayName)
                .append(QString::fromUtf8("  ·  "))
                .arg(t.imageCount),
            m_list);
        item->setData(Qt::UserRole, t.path);
        item->setToolTip(t.path);
    }
    m_applyBtn->setEnabled(!themes.empty());
    m_deleteBtn->setEnabled(!themes.empty());
    m_nextBtn->setEnabled(!themes.empty() && m_engine->config().dailyShuffleEnabled);
    if (!themes.empty() && !m_list->currentItem())
        m_list->setCurrentRow(0);
    else if (!themes.empty())
        onSelectionChanged();
}

QString ThemesTab::selectedThemePath() const {
    const auto* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ThemesTab::onSelectionChanged() {
    updatePreview();
    rebuildSchedule();
}

void ThemesTab::updatePreview() {
    const QString dir = selectedThemePath();
    if (dir.isEmpty()) {
        m_preview->clear();
        return;
    }
    auto data = themes::loadThemeData(dir);
    if (!data) {
        m_preview->clear();
        return;
    }
    const auto cfg = m_engine->config();
    const QTimeZone tz(cfg.timezone.toUtf8());
    const double now = QDateTime::currentMSecsSinceEpoch();
    const auto seg = solar::segmentsForNow(now, QDateTime::currentDateTime().date(), tz,
                                           cfg.latitude, cfg.longitude, listsOf(*data));
    QString currentCategory;
    const auto per = perCategoryImages(dir, *data, seg, now, &currentCategory);
    m_preview->setImages(per, currentCategory);
}

void ThemesTab::rebuildSchedule() {
    const QString dir = selectedThemePath();
    if (dir.isEmpty()) {
        m_schedule->setSchedule({});
        return;
    }
    auto data = themes::loadThemeData(dir);
    if (!data) {
        m_schedule->setSchedule({});
        return;
    }
    const auto cfg = m_engine->config();
    const QTimeZone tz(cfg.timezone.toUtf8());
    const QDate today =
        QDateTime::currentDateTime().toTimeZone(tz).date();

    SchedulePreview::ScheduleData sd;
    sd.day = today;
    sd.dayStartMs =
        QDateTime(today, QTime(0, 0), tz).toMSecsSinceEpoch();
    sd.dayEndMs =
        QDateTime(today.addDays(1), QTime(0, 0), tz).toMSecsSinceEpoch();

    const auto lists = listsOf(*data);
    const auto seg = solar::segmentsForDay(today, tz, cfg.latitude, cfg.longitude);
    const auto windows = solar::effectiveWindows(seg, lists);
    int position[4] = {0, 0, 0, 0};
    for (const auto& w : windows) {
        const int slot = categorySlot(w.category);
        if (slot < 0)
            continue;
        const std::vector<int>& list =
            slot == 0 ? lists.sunrise : slot == 1 ? lists.day :
            slot == 2 ? lists.sunset : lists.night;
        SchedulePreview::Entry e;
        e.window = w;
        if (position[slot] < static_cast<int>(list.size()))
            e.imagePath = themes::imageFileFor(dir, *data, list[position[slot]]);
        position[slot]++;
        sd.entries.push_back(e);
    }
    m_schedule->setSchedule(sd);
    m_schedule->setNow(QDateTime::currentMSecsSinceEpoch());
}

void ThemesTab::rebuildPreview() {
    updatePreview();
    rebuildSchedule();
}

void ThemesTab::setBusy(bool busy) {
    m_importBtn->setEnabled(!busy);
    m_applyBtn->setEnabled(!busy && m_list->count() > 0);
    m_deleteBtn->setEnabled(!busy && m_list->count() > 0);
    m_nextBtn->setEnabled(!busy && m_list->count() > 0 &&
                          m_engine->config().dailyShuffleEnabled);
    m_list->setEnabled(!busy);
}

void ThemesTab::onImport() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import theme"), QString(),
        tr("Johona themes (*.ddw *.zip);;All files (*)"));
    if (file.isEmpty())
        return;
    setBusy(true);
    emit statusMessage(tr("Importing %1…").arg(QFileInfo(file).fileName()));
    const QString themesDir = m_engine->paths().themesDir;
    QFuture<themes::ImportResult> fut =
        QtConcurrent::run([file, themesDir]() { return themes::importTheme(file, themesDir); });
    fut.then(this, [this, file](themes::ImportResult result) {
        setBusy(false);
        if (result.success) {
            emit statusMessage(tr("Imported theme “%1”").arg(result.displayName));
    refresh();
            return;
        }
        // List every missing image (spec §11.1).
        QString text;
        if (result.missingImages.empty()) {
            text = result.message;
        } else {
            QStringList lines;
            for (const QString& m : result.missingImages)
                lines << m;
            text = tr("Missing images:\n%1").arg(lines.join(QLatin1Char('\n')));
        }
        QMessageBox::critical(this, tr("Import failed"), text);
    });
}

void ThemesTab::onApply() {
    const QString path = selectedThemePath();
    if (path.isEmpty())
        return;
    setBusy(true);
    auto future = bridge::call<ApplyOutcome>(m_engine, [this, path]() {
        return m_engine->applyTheme(QFileInfo(path).fileName());
    });
    future.then(this, [this](ApplyOutcome out) {
        setBusy(false);
        if (out.success) {
            emit themeApplied(out.themeName);
            emit statusMessage(out.skipped
                                   ? tr("Wallpaper already set to %1").arg(out.themeName)
                                   : tr("Applied %1 (%2)").arg(out.themeName, out.category));
            rebuildPreview();
        } else {
            emit statusMessage(tr("Apply failed: %1").arg(out.message));
        }
    });
}

void ThemesTab::onDelete() {
    const QString path = selectedThemePath();
    if (path.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("Delete theme"),
        tr("Delete theme “%1” and all of its images?").arg(QFileInfo(path).fileName()));
    if (answer != QMessageBox::Yes)
        return;
    setBusy(true);
    const QString themesDir = m_engine->paths().themesDir;
    QFuture<themes::DeleteResult> fut = QtConcurrent::run(
        [path, themesDir]() { return themes::deleteTheme(path, themesDir); });
    fut.then(this, [this](themes::DeleteResult result) {
        setBusy(false);
        if (result.success) {
            emit statusMessage(tr("Theme deleted"));
    refresh();
        } else {
            QMessageBox::warning(this, tr("Delete failed"), result.message);
        }
    });
}

void ThemesTab::onNext() {
    setBusy(true);
    auto future = bridge::call<ApplyOutcome>(m_engine, [this]() {
        return m_engine->advanceShuffle();
    });
    future.then(this, [this](ApplyOutcome out) {
        setBusy(false);
        if (out.success) {
            emit themeApplied(out.themeName);
            emit statusMessage(tr("Next wallpaper: %1").arg(out.themeName));
            rebuildPreview();
        } else {
            emit statusMessage(tr("Advance failed: %1").arg(out.message));
        }
    });
}

void ThemesTab::onApplied(const QString& themeName, const QString& imagePath,
                          const QString& category) {
    Q_UNUSED(imagePath);
    // Select the applied theme in the list and refresh the preview.
    for (int i = 0; i < m_list->count(); i++) {
        auto* item = m_list->item(i);
        const QString name = QFileInfo(item->data(Qt::UserRole).toString()).fileName();
        if (name == themeName) {
            m_list->setCurrentItem(item);
            break;
        }
    }
    rebuildPreview();
    Q_UNUSED(category);
}

}  // namespace johona::gui
