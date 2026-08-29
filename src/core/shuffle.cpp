// shuffle.cpp — daily theme shuffle state implementation.

#include "shuffle.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <set>

namespace johona::shuffle {

ShuffleState loadShuffleState(const QString& path) {
    ShuffleState state;
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return state;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return state;  // corrupt → empty state (rebuilt on next advance)
    const QJsonObject obj = doc.object();
    for (const auto& v : obj.value("shuffle_list").toArray())
        state.shuffleList.push_back(v.toString());
    state.currentIndex = obj.value("current_index").toInt(0);
    state.lastUsedDate = obj.value("last_used_date").toString();
    if (state.currentIndex < 0 ||
        state.currentIndex >= static_cast<int>(state.shuffleList.size()))
        state.currentIndex = 0;
    return state;
}

bool saveShuffleState(const ShuffleState& state, const QString& path) {
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;
    QJsonObject obj;
    QJsonArray arr;
    for (const QString& p : state.shuffleList)
        arr.append(p);
    obj["shuffle_list"] = arr;
    obj["current_index"] = state.currentIndex;
    obj["last_used_date"] = state.lastUsedDate;

    // Atomic write: temp file in the same directory + rename.
    const QString tmp = path + QStringLiteral(".tmp");
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        if (!f.flush())
            return false;
    }
    if (QFile::exists(path) && !QFile::remove(path))
        return false;
    if (!QFile::rename(tmp, path)) {
        QFile::remove(tmp);
        return false;
    }
    return true;
}

bool dayPassed(const ShuffleState& state, const QString& today) {
    return state.lastUsedDate != today;
}

void fisherYates(std::vector<QString>& v, QRandomGenerator* rng) {
    QRandomGenerator* g = rng ? rng : QRandomGenerator::global();
    for (std::size_t i = v.size(); i > 1; i--) {
        // QRandomGenerator::bounded overloads are 32/64-bit unsigned; size_t
        // is neither on LP64, so cast explicitly.
        const std::size_t j = static_cast<std::size_t>(g->bounded(static_cast<int>(i)));
        std::swap(v[i - 1], v[j]);
    }
}

bool sameSet(const std::vector<QString>& a, const std::vector<QString>& b) {
    return std::set<QString>(a.begin(), a.end()) == std::set<QString>(b.begin(), b.end());
}

ShuffleState advanceShuffle(ShuffleState state, const std::vector<QString>& themes,
                            const QString& today, QRandomGenerator* rng) {
    if (themes.empty())
        return state;  // nothing to shuffle into

    // Rebuild when the list is empty or stale (theme set changed).
    if (state.empty() || !sameSet(state.shuffleList, themes)) {
        state.shuffleList = themes;
        fisherYates(state.shuffleList, rng);
        state.currentIndex = 0;
    } else {
        const int n = static_cast<int>(state.shuffleList.size());
        state.currentIndex = (state.currentIndex + 1) % n;
        if (state.currentIndex == 0)
            fisherYates(state.shuffleList, rng);  // wrap → reshuffle
    }
    state.lastUsedDate = today;
    return state;
}

}  // namespace johona::shuffle
