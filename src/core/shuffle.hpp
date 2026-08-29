// shuffle.hpp — daily theme shuffle state (single writer, persist-after-success).
//
// State file (config dir, `shuffle-list.json`):
//
//   { "shuffle_list": ["<theme paths>"], "current_index": 0,
//     "last_used_date": "yyyy-MM-dd" }
//
// Semantics (spec §9.3/§9.5, kWallpaper parity plus the approved
// reshuffle-on-wrap improvement):
//  - The engine is the single writer; state is persisted only after the
//    wallpaper set succeeds (a failed change retries the same theme).
//  - Daily advance (date change, incl. missed-midnight recovery) and the
//    manual "Next wallpaper" advance share the same step:
//      current_index = (current_index + 1) mod len;
//      when the index wraps to 0 the list is reshuffled (Fisher–Yates).
//  - An empty or stale list (theme set changed) is rebuilt from the
//    currently discovered themes.
//  - Atomic writes.

#pragma once

#include <QRandomGenerator>
#include <QString>
#include <vector>

namespace johona::shuffle {

struct ShuffleState {
    std::vector<QString> shuffleList;  // theme directory paths
    int currentIndex = 0;
    QString lastUsedDate;              // "yyyy-MM-dd" ("" = never)

    bool empty() const { return shuffleList.empty(); }
    bool valid() const {
        return !shuffleList.empty() && currentIndex >= 0 &&
               currentIndex < static_cast<int>(shuffleList.size());
    }
    QString currentTheme() const { return valid() ? shuffleList[currentIndex] : QString{}; }
};

/// Load shuffle state (missing/corrupt file → empty state).
ShuffleState loadShuffleState(const QString& path);

/// Save shuffle state atomically (temp file + rename).
bool saveShuffleState(const ShuffleState& state, const QString& path);

/// True when a daily advance is due (last_used_date differs from `today`).
bool dayPassed(const ShuffleState& state, const QString& today);

/// One advance step (daily or manual).  Pure function — the caller persists
/// the result only after the wallpaper set succeeds.
///
///  - `themes`: the currently discovered theme paths (used to rebuild an
///    empty or stale list).
///  - `today`: "yyyy-MM-dd" in the configured timezone.
///  - `rng`: injectable RNG (tests); defaults to QRandomGenerator::global().
ShuffleState advanceShuffle(ShuffleState state, const std::vector<QString>& themes,
                            const QString& today, QRandomGenerator* rng = nullptr);

/// Fisher–Yates shuffle (exposed for tests).
void fisherYates(std::vector<QString>& v, QRandomGenerator* rng = nullptr);

/// True when two path lists contain exactly the same paths (any order).
bool sameSet(const std::vector<QString>& a, const std::vector<QString>& b);

}  // namespace johona::shuffle
