// test_shuffle.cpp — shuffle state: persistence, daily advance,
// reshuffle-on-wrap, stale-list rebuild.

#include <QtTest>

#include <set>

#include "shuffle.hpp"

using namespace johona::shuffle;

class TestShuffle : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loadSave_roundtrip();
    void load_missingOrCorrupt_empty();
    void testDayPassed();
    void advance_emptyRebuilds();
    void advance_staleRebuilds();
    void advance_step();
    void advance_wrapReshuffles();
    void advance_noThemes_noop();
    void fisherYates_permutation();

private:
    QString statePath() const { return m_tmp.path() + "/shuffle-list.json"; }

    QTemporaryDir m_tmp;
};

void TestShuffle::initTestCase() {
    QVERIFY(m_tmp.isValid());
}

void TestShuffle::loadSave_roundtrip() {
    ShuffleState s;
    s.shuffleList = {"/themes/a", "/themes/b", "/themes/c"};
    s.currentIndex = 2;
    s.lastUsedDate = "2026-08-27";

    QVERIFY(saveShuffleState(s, statePath()));
    const ShuffleState d = loadShuffleState(statePath());
    QCOMPARE(d.shuffleList, s.shuffleList);
    QCOMPARE(d.currentIndex, 2);
    QCOMPARE(d.lastUsedDate, QString("2026-08-27"));
    QVERIFY(d.valid());
    QCOMPARE(d.currentTheme(), QString("/themes/c"));
}

void TestShuffle::load_missingOrCorrupt_empty() {
    const ShuffleState missing = loadShuffleState(m_tmp.path() + "/nope.json");
    QVERIFY(missing.empty());
    QVERIFY(!missing.valid());

    QFile f(statePath());
    f.open(QIODevice::WriteOnly);
    f.write("{{{ not json");
    f.close();
    QVERIFY(loadShuffleState(statePath()).empty());
}

void TestShuffle::testDayPassed() {
    ShuffleState s;
    s.shuffleList = {"/a"};
    s.lastUsedDate = "2026-08-27";
    QVERIFY(!dayPassed(s, "2026-08-27"));
    QVERIFY(dayPassed(s, "2026-08-28"));

    s.lastUsedDate.clear();  // never used → advance is due
    QVERIFY(dayPassed(s, "2026-08-27"));
}

void TestShuffle::advance_emptyRebuilds() {
    QRandomGenerator rng(42);
    const std::vector<QString> themes{"/a", "/b", "/c", "/d"};
    const ShuffleState out = advanceShuffle({}, themes, "2026-08-27", &rng);
    QCOMPARE(out.shuffleList.size(), themes.size());
    QCOMPARE(out.currentIndex, 0);
    QCOMPARE(out.lastUsedDate, QString("2026-08-27"));
    // Same set of themes (order may be shuffled).
    QCOMPARE((std::set<QString>(out.shuffleList.begin(), out.shuffleList.end())),
             (std::set<QString>(themes.begin(), themes.end())));
}

void TestShuffle::advance_staleRebuilds() {
    QRandomGenerator rng(7);
    ShuffleState s;
    s.shuffleList = {"/a", "/b"};  // /c was added since
    s.currentIndex = 1;
    s.lastUsedDate = "2026-08-26";

    const std::vector<QString> themes{"/a", "/b", "/c"};
    const ShuffleState out = advanceShuffle(s, themes, "2026-08-27", &rng);
    QCOMPARE(out.shuffleList.size(), static_cast<size_t>(3));
    QCOMPARE(out.currentIndex, 0);
    QCOMPARE(out.lastUsedDate, QString("2026-08-27"));
}

void TestShuffle::advance_step() {
    QRandomGenerator rng(1);
    ShuffleState s;
    s.shuffleList = {"/a", "/b", "/c"};
    s.currentIndex = 0;
    s.lastUsedDate = "2026-08-27";

    const std::vector<QString> themes{"/a", "/b", "/c"};  // same set
    const ShuffleState out = advanceShuffle(s, themes, "2026-08-27", &rng);
    QCOMPARE(out.currentIndex, 1);
    // No wrap → order preserved.
    QCOMPARE(out.shuffleList, s.shuffleList);
}

void TestShuffle::advance_wrapReshuffles() {
    QRandomGenerator rng(1);
    ShuffleState s;
    s.shuffleList = {"/a", "/b", "/c"};
    s.currentIndex = 2;  // next step wraps to 0
    s.lastUsedDate = "2026-08-27";

    const std::vector<QString> themes{"/a", "/b", "/c"};
    const ShuffleState out = advanceShuffle(s, themes, "2026-08-28", &rng);
    QCOMPARE(out.currentIndex, 0);
    QCOMPARE(out.lastUsedDate, QString("2026-08-28"));
    // Set preserved (order may change due to the reshuffle).
    QCOMPARE((std::set<QString>(out.shuffleList.begin(), out.shuffleList.end())),
             (std::set<QString>{"/a", "/b", "/c"}));
}

void TestShuffle::advance_noThemes_noop() {
    ShuffleState s;
    s.shuffleList = {"/a"};
    s.currentIndex = 0;
    s.lastUsedDate = "2026-08-27";
    const ShuffleState out = advanceShuffle(s, {}, "2026-08-28");
    // Unchanged (no themes to shuffle into).
    QCOMPARE(out.shuffleList, s.shuffleList);
    QCOMPARE(out.currentIndex, s.currentIndex);
    QCOMPARE(out.lastUsedDate, s.lastUsedDate);
}

void TestShuffle::fisherYates_permutation() {
    QRandomGenerator rng(99);
    std::vector<QString> v;
    for (int i = 1; i <= 20; i++)
        v.push_back(QStringLiteral("t%1").arg(i));
    fisherYates(v, &rng);
    QCOMPARE(v.size(), static_cast<size_t>(20));
    QCOMPARE((std::set<QString>(v.begin(), v.end())),
             (std::set<QString>{
                 "t1",  "t2",  "t3",  "t4",  "t5",  "t6",  "t7",  "t8",  "t9",
                 "t10", "t11", "t12", "t13", "t14", "t15", "t16", "t17",
                 "t18", "t19", "t20"}));
}

QTEST_GUILESS_MAIN(TestShuffle)
#include "test_shuffle.moc"
