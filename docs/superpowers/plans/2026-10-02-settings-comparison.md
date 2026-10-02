# Settings Comparison Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fourth Statistics report mode, «Настройки», showing one table that compares how the nights went on each set of device settings.

**Architecture:** A new UI-free module `SettingsComparison` merges the Statistics settings periods (`RXItem`) with equal settings into groups, picks the best values and renders the HTML table. `Statistics::GenerateSettingsComparison()` feeds it the periods and per-night figures (from `Day` and OSCAR's analysis rows). `GenerateHTML()` and printing use the result in mode `STAT_MODE_SETTINGS`. `analysisFigure()` gains a numeric twin, `analysisFigureValue()`.

**Tech Stack:** Qt 6.11 (Homebrew), qmake, C++17, QtTest via the project's `AutoTest.h` harness.

**Spec:** `docs/superpowers/specs/2026-10-02-settings-comparison-design.md`

## Global Constraints

- Work on `master` of the fork (`/Users/semyk/Downloads/Oscar_Project/oscar-sql`); push only to `origin`, and only when the user says so.
- Build dirs: `../build` (app), `../build-test` (tests, `CONFIG+=test`), `../build-nobt`. After adding files to `oscar/oscar.pro` or changing `.ts`, re-run qmake in each dir: `Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q"`, then `make -j10`.
- Tests: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Class…]` — expects 0 unexpected failures (currently 365 pass).
- Russian translation: every new string translated in `Translations/Russkiy.ru.ts`. Run `/opt/homebrew/bin/lupdate oscar/oscar.pro -ts Translations/Russkiy.ru.ts`, replace each `<translation type="unfinished">…</translation>` of the new strings with `<translation>…</translation>`. Then `grep -c 'type="unfinished"' Translations/Russkiy.ru.ts` → 0 and `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`.
- Running OSCAR: only `open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`. Never `~/Documents/OSCAR20_Data`, never two instances. Quit through the app menu «OSCAR20 → Quit OSCAR20».
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Fork-only docs (`docs/superpowers/…`, gitignored) are added with `git add -f` in a separate commit whose subject ends with `(fork-only, not for MR)`.
- Spec values: `kMinNights = 3`; mode constant `STAT_MODE_SETTINGS = 3`; «–» for no data; hours and AHI 2 decimals, percentages, leak and pressure 1 decimal; locale formatting (`QLocale().toString`).
- Code style: match the surrounding OSCAR code (4-space indent, `//!` doc comments in headers, file header comment block with `Copyright (c) 2026 The OSCAR Team`).

## Review Focus

1. Nights loaded from `RXChanges.cache` whose summaries were never opened — leak and pressure must still show real numbers, not «–» or 0 (Task 4 opens each day's summary; checked in the app).
2. Settings strings with HTML special characters (`<`, `&`) — must appear escaped, not break the table (Task 3 test `testHtmlEscapesSettings`).
3. OSCAR restarted with «Настройки» as the saved mode — the radio button and the hidden date pickers must match the mode (Task 4 switch branch; checked by restarting the app).
4. A system locale with a comma decimal separator (the user's Russian macOS) — numbers in the table use the locale; tests build their expected strings with `QLocale()` (Task 3).
5. A group with zero hours — «–», never `inf` or `nan` text (Task 2 test `testRowFigures`, Task 3 test `testHtmlShowsDashWithoutData`).

---

### Task 1: `analysisFigureValue()` — the analysis figures as numbers

**Files:**
- Modify: `oscar/statistics.cpp:127-170` (`analysisFigure`)
- Modify: `oscar/statistics.h:78`
- Test: `oscar/tests/analysispaneltests.cpp`, `oscar/tests/analysispaneltests.h`

**Interfaces:**
- Consumes: `struct AnalysisDailyData` (`oscar/database/analysis_daily_repository.h`).
- Produces: `double analysisFigureValue(const QString &key, const QList<AnalysisDailyData> &rows);` declared in `oscar/statistics.h` next to `analysisFigure`. Returns NaN when there is no data; the same number `analysisFigure()` prints otherwise (nadir included).

- [ ] **Step 1: Write the failing test**

In `oscar/tests/analysispaneltests.h` add the slot after `void testStatisticsFigures();`:

```cpp
    void testStatisticsFigureValues();
```

In `oscar/tests/analysispaneltests.cpp` add after `AnalysisPanelTests::testStatisticsFigures()`:

```cpp
void AnalysisPanelTests::testStatisticsFigureValues()
{
    // the numbers behind the Statistics figures, for tables that compare them
    AnalysisDailyData a, b;
    a.hasFlow = b.hasFlow = true;
    a.flowSeconds = 3600;
    b.flowSeconds = 7200;
    a.nObstructiveApnea = 5;
    b.nObstructiveApnea = 1;
    a.hasOximetry = true;
    a.oxiSeconds = 3600;
    a.nDesat3 = 6;
    a.spo2Nadir = 86;
    a.spo2Hist = QVector<int>(51, 0);
    a.spo2Hist[96 - 50] = 3240;
    a.spo2Hist[89 - 50] = 360;   // 10 % below 90
    const QList<AnalysisDailyData> rows { a, b };

    QCOMPARE(analysisFigureValue(QStringLiteral("ahi"), rows), 2.0);    // 6 events over 3 hours
    QCOMPARE(analysisFigureValue(QStringLiteral("odi3"), rows), 6.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("below:90"), rows), 10.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("nadir"), rows), 86.0);
    QVERIFY(std::isnan(analysisFigureValue(QStringLiteral("pri"), rows)));   // no pulse
    QVERIFY(std::isnan(analysisFigureValue(QStringLiteral("ahi"), {})));

    // the text figures are these numbers, formatted
    QCOMPARE(analysisFigure(QStringLiteral("ahi"), rows), QStringLiteral("2.00"));
    QCOMPARE(analysisFigure(QStringLiteral("nadir"), rows), QStringLiteral("86"));
    QCOMPARE(analysisFigure(QStringLiteral("pri"), rows), QStringLiteral("-"));
}
```

Add `#include <cmath>` to the includes of `oscar/tests/analysispaneltests.cpp` if it is not there.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:" | head -3`
Expected: compile error `use of undeclared identifier 'analysisFigureValue'`.

- [ ] **Step 3: Write the implementation**

In `oscar/statistics.h`, after line 78 (`QString analysisFigure(...)`), add:

```cpp
//! The number behind analysisFigure(): NaN when there is no data for \a key.
double analysisFigureValue(const QString &key, const QList<AnalysisDailyData> &rows);
```

In `oscar/statistics.cpp` rename the current `QString analysisFigure(const QString &key, const QList<AnalysisDailyData> &rows)` to `double analysisFigureValue(...)`, keep the loop unchanged and replace its first line and its ending:

```cpp
double analysisFigureValue(const QString &key, const QList<AnalysisDailyData> &rows)
{
    double num = 0, den = 0;
    double nadir = 101;
    // ... the existing for loop, unchanged ...
    if (den <= 0) return std::numeric_limits<double>::quiet_NaN();
    if (key == QLatin1String("nadir")) return nadir;
    return num / den;
}

QString analysisFigure(const QString &key, const QList<AnalysisDailyData> &rows)
{
    const double value = analysisFigureValue(key, rows);
    if (std::isnan(value)) return QStringLiteral("-");
    if (key == QLatin1String("nadir")) return QString::number(value, 'f', 0);
    return QString::number(value, 'f', 2);
}
```

(The removed lines are `const QString none = QStringLiteral("-");` and the old `if (den <= 0) return none; …return QString::number(num / den, 'f', 2);` tail.) Add `#include <limits>` next to `#include <cmath>` at the top of `oscar/statistics.cpp`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh AnalysisPanelTests`
Expected: no build errors; `AnalysisPanelTests: N PASS` and `0 unexpected`. `testStatisticsFigures` still passes (unchanged text output).

- [ ] **Step 5: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/statistics.cpp oscar/statistics.h oscar/tests/analysispaneltests.cpp oscar/tests/analysispaneltests.h
git commit -m "Statistics: analysis figures as numbers for comparison tables

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `SettingsComparison` — groups, rows, best values

**Files:**
- Create: `oscar/settingscomparison.h`, `oscar/settingscomparison.cpp`
- Create: `oscar/tests/settingscomparisontests.h`, `oscar/tests/settingscomparisontests.cpp`
- Modify: `oscar/oscar.pro` (four lists: sources, headers, test sources, test headers — next to `overviewpresets.*` / `tests/overviewpresetstests.*`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces (namespace `SettingsComparison`, header `oscar/settingscomparison.h`):
  - `constexpr int kMinNights = 3;`
  - `struct Period { QString mode, pressure, relief, deviceKey, deviceLabel; QList<QDate> dates; double hours = 0; double events = 0; };`
  - `struct Group { QString mode, pressure, relief, deviceKey, deviceLabel; QList<QDate> dates; double hours = 0; double events = 0; };`
  - `QList<Group> group(const QList<Period> &periods);`
  - `enum Column { Nights, Usage, DeviceAhi, AnalysisAhi, Leak, Pressure, FlowLimitation, Odi3, Below90, ColumnCount };`
  - `struct Row { Group group; QVector<double> values; int nights() const; };` — `values` has `ColumnCount` entries, NaN = no data.
  - `Row row(const Group &group);`
  - `bool reliable(const Row &row);`
  - `QSet<int> best(const QList<Row> &rows, Column column);`
  - `QString dateList(const QList<QDate> &dates);`
  - `int decimals(Column column);`

- [ ] **Step 1: Write the header and a stub implementation**

`oscar/settingscomparison.h`:

```cpp
/* Device Settings Comparison Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SETTINGSCOMPARISON_H
#define SETTINGSCOMPARISON_H

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>

//! Compares how the nights went on each set of device settings (Statistics, "Settings" mode).
namespace SettingsComparison {

//! Nights a row needs before its figures count as a result.
constexpr int kMinNights = 3;

//! One stretch of nights on unchanged settings, as Statistics::updateRXChanges() finds them.
struct Period {
    QString mode;
    QString pressure;
    QString relief;
    QString deviceKey;      //!< equal for nights on what counts as the same device
    QString deviceLabel;
    QList<QDate> dates;
    double hours = 0;
    double events = 0;      //!< the device's AHI (or RDI) events
};

//! All periods with the same settings on the same device, wherever they fall in the history.
struct Group {
    QString mode;
    QString pressure;
    QString relief;
    QString deviceKey;
    QString deviceLabel;
    QList<QDate> dates;     //!< ascending, no duplicates
    double hours = 0;
    double events = 0;
};

//! Merges \a periods with equal settings and device; the group used last comes first.
QList<Group> group(const QList<Period> &periods);

enum Column { Nights, Usage, DeviceAhi, AnalysisAhi, Leak, Pressure, FlowLimitation, Odi3, Below90, ColumnCount };

//! A table row: its group and one value per column, NaN where there is no data.
struct Row {
    Group group;
    QVector<double> values = QVector<double>(ColumnCount, std::numeric_limits<double>::quiet_NaN());
    int nights() const { return group.dates.size(); }
};

//! A row with the nights, usage and device AHI of \a group filled in.
Row row(const Group &group);
//! Whether \a row has enough nights to be compared.
bool reliable(const Row &row);
//! Rows holding the best value of \a column among the reliable ones; empty with fewer than two.
QSet<int> best(const QList<Row> &rows, Column column);
//! \a dates (ascending) as text, nights in a row shown as one range.
QString dateList(const QList<QDate> &dates);
//! Decimals \a column is shown with.
int decimals(Column column);

} // namespace SettingsComparison

#endif // SETTINGSCOMPARISON_H
```

`oscar/settingscomparison.cpp` (stub, so the tests fail on assertions):

```cpp
/* Device Settings Comparison
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparison.h"

namespace SettingsComparison {

QList<Group> group(const QList<Period> &) { return {}; }
Row row(const Group &) { return Row(); }
bool reliable(const Row &) { return false; }
QSet<int> best(const QList<Row> &, Column) { return {}; }
QString dateList(const QList<QDate> &) { return QString(); }
int decimals(Column) { return 0; }

} // namespace SettingsComparison
```

Register in `oscar/oscar.pro`: add `settingscomparison.cpp \` after `overviewpresets.cpp \`, `settingscomparison.h \` after `overviewpresets.h \`, `tests/settingscomparisontests.cpp \` after `tests/overviewpresetstests.cpp \`, `tests/settingscomparisontests.h \` after `tests/overviewpresetstests.h \`.

- [ ] **Step 2: Write the failing tests**

`oscar/tests/settingscomparisontests.h`:

```cpp
/* Device Settings Comparison Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SETTINGSCOMPARISONTESTS_H
#define SETTINGSCOMPARISONTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the Statistics table comparing device settings.
class SettingsComparisonTests : public QObject
{
    Q_OBJECT
private slots:
    void testGroupMergesSameSettings();
    void testGroupKeepsDifferentSettingsApart();
    void testRowFigures();
    void testBest();
    void testDateList();
};
DECLARE_TEST(SettingsComparisonTests)

#endif // SETTINGSCOMPARISONTESTS_H
```

`oscar/tests/settingscomparisontests.cpp`:

```cpp
/* Device Settings Comparison Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparisontests.h"

#include <QLocale>
#include <cmath>

#include "settingscomparison.h"

using namespace SettingsComparison;

namespace {

QList<QDate> nights(const QDate &first, int count)
{
    QList<QDate> dates;
    for (int i = 0; i < count; ++i) dates << first.addDays(i);
    return dates;
}

Period period(const QString &pressure, const QDate &first, int count, double hoursPerNight, double eventsPerHour,
              const QString &relief = QStringLiteral("SoftPAP: 1"), const QString &device = QStringLiteral("prisma"))
{
    Period p;
    p.mode = QStringLiteral("APAP");
    p.pressure = pressure;
    p.relief = relief;
    p.deviceKey = device;
    p.deviceLabel = device;
    p.dates = nights(first, count);
    p.hours = hoursPerNight * count;
    p.events = eventsPerHour * p.hours;
    return p;
}

Row rowWith(int nightCount, Column column, double value)
{
    Group g;
    g.dates = nights(QDate(2026, 9, 1), nightCount);
    Row r;
    r.group = g;
    r.values[column] = value;
    return r;
}

} // namespace

void SettingsComparisonTests::testGroupMergesSameSettings()
{
    // 9-16 on 20.09, 6-9 on 24-28.09, 9-16 again on 21-23.09: two groups, 9-16 has 4 nights
    const QList<Period> periods {
        period(QStringLiteral("9-16"), QDate(2026, 9, 20), 1, 7, 5),
        period(QStringLiteral("6-9"), QDate(2026, 9, 24), 5, 8, 2),
        period(QStringLiteral("9-16"), QDate(2026, 9, 21), 3, 7, 3),
    };
    const QList<Group> groups = group(periods);
    QCOMPARE(groups.size(), 2);

    // the group used last comes first
    QCOMPARE(groups[0].pressure, QStringLiteral("6-9"));
    QCOMPARE(groups[0].dates.size(), 5);

    const Group &g = groups[1];
    QCOMPARE(g.pressure, QStringLiteral("9-16"));
    QCOMPARE(g.dates, nights(QDate(2026, 9, 20), 4));    // merged and in date order
    QCOMPARE(g.hours, 28.0);
    QCOMPARE(g.events, 7 * 5 + 21 * 3.0);
}

void SettingsComparisonTests::testGroupKeepsDifferentSettingsApart()
{
    const QList<Period> periods {
        period(QStringLiteral("9-16"), QDate(2026, 9, 1), 3, 7, 5),
        period(QStringLiteral("9-16"), QDate(2026, 9, 4), 3, 7, 5, QStringLiteral("SoftPAP: 2")),
        period(QStringLiteral("9-16"), QDate(2026, 9, 7), 3, 7, 5, QStringLiteral("SoftPAP: 1"),
               QStringLiteral("airsense")),
    };
    QCOMPARE(group(periods).size(), 3);
}

void SettingsComparisonTests::testRowFigures()
{
    const Group g = group({ period(QStringLiteral("9-16"), QDate(2026, 9, 21), 4, 7, 3.5) }).first();
    const Row r = row(g);
    QCOMPARE(r.nights(), 4);
    QCOMPARE(r.values[Nights], 4.0);
    QCOMPARE(r.values[Usage], 7.0);
    QCOMPARE(r.values[DeviceAhi], 3.5);
    QVERIFY(std::isnan(r.values[AnalysisAhi]));       // filled in by Statistics
    QVERIFY(reliable(r));
    QVERIFY(!reliable(row(group({ period(QStringLiteral("6-9"), QDate(2026, 9, 1), 2, 7, 1) }).first())));

    // no hours: no usage and no index, never a division by zero
    Group empty = g;
    empty.hours = 0;
    empty.events = 0;
    const Row none = row(empty);
    QVERIFY(std::isnan(none.values[Usage]));
    QVERIFY(std::isnan(none.values[DeviceAhi]));
}

void SettingsComparisonTests::testBest()
{
    // lower is better for AHI; the 1-night row would win but does not count
    QList<Row> rows { rowWith(4, DeviceAhi, 3.0), rowWith(5, DeviceAhi, 5.0), rowWith(1, DeviceAhi, 1.0) };
    QCOMPARE(best(rows, DeviceAhi), QSet<int>({ 0 }));

    // higher is better for usage; equal values win together
    rows = { rowWith(4, Usage, 7.0), rowWith(3, Usage, 7.0), rowWith(6, Usage, 6.5) };
    QCOMPARE(best(rows, Usage), QSet<int>({ 0, 1 }));

    // nothing to compare against: no winner
    rows = { rowWith(4, DeviceAhi, 3.0), rowWith(2, DeviceAhi, 1.0) };
    QVERIFY(best(rows, DeviceAhi).isEmpty());

    // rows without the figure are skipped
    rows = { rowWith(4, Odi3, 6.0), rowWith(4, Odi3, std::nan("")), rowWith(5, Odi3, 9.0) };
    QCOMPARE(best(rows, Odi3), QSet<int>({ 0 }));

    // pressure and nights are not better or worse
    rows = { rowWith(4, Pressure, 9.0), rowWith(5, Pressure, 12.0) };
    QVERIFY(best(rows, Pressure).isEmpty());
    QVERIFY(best(rows, Nights).isEmpty());
}

void SettingsComparisonTests::testDateList()
{
    const QLocale locale;
    const QDate a(2026, 9, 20), c(2026, 9, 22), d(2026, 9, 25);
    const QString expected = locale.toString(a, QLocale::ShortFormat) + QStringLiteral(" – ")
                           + locale.toString(c, QLocale::ShortFormat) + QStringLiteral("; ")
                           + locale.toString(d, QLocale::ShortFormat);
    QCOMPARE(dateList({ a, a.addDays(1), c, d }), expected);
    QCOMPARE(dateList({ d }), locale.toString(d, QLocale::ShortFormat));
    QCOMPARE(dateList({}), QString());
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build-test && Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q" >/dev/null 2>&1; make -j10 2>&1 | grep -E "error:" | head -3; ./test > run.log 2>&1; grep -E "^(PASS|FAIL!).*SettingsComparisonTests" run.log
```
Expected: builds; `FAIL!` for all five `SettingsComparisonTests::test…` slots (stub returns nothing).

- [ ] **Step 4: Write the implementation**

Replace `oscar/settingscomparison.cpp` with:

```cpp
/* Device Settings Comparison
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "settingscomparison.h"

#include <QHash>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace SettingsComparison {

namespace {

QDate lastDate(const Group &g) { return g.dates.isEmpty() ? QDate() : g.dates.last(); }

// Values compare as they are shown, so 7.00 and 7.004 win together.
qint64 shown(double value, Column column) { return qRound64(value * std::pow(10.0, decimals(column))); }

} // namespace

QList<Group> group(const QList<Period> &periods)
{
    QList<Group> groups;
    QHash<QString, int> index;
    for (const Period &p : periods) {
        const QString key = QStringList { p.mode, p.pressure, p.relief, p.deviceKey }.join(QChar(0x1f));
        auto it = index.find(key);
        if (it == index.end()) {
            Group g;
            g.mode = p.mode;
            g.pressure = p.pressure;
            g.relief = p.relief;
            g.deviceKey = p.deviceKey;
            g.deviceLabel = p.deviceLabel;
            it = index.insert(key, groups.size());
            groups << g;
        }
        Group &g = groups[it.value()];
        g.dates << p.dates;
        g.hours += p.hours;
        g.events += p.events;
    }
    for (Group &g : groups) {
        std::sort(g.dates.begin(), g.dates.end());
        g.dates.erase(std::unique(g.dates.begin(), g.dates.end()), g.dates.end());
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group &a, const Group &b) { return lastDate(a) > lastDate(b); });
    return groups;
}

Row row(const Group &group)
{
    Row r;
    r.group = group;
    const int nights = group.dates.size();
    r.values[Nights] = nights;
    if (nights > 0 && group.hours > 0) r.values[Usage] = group.hours / nights;
    if (group.hours > 0) r.values[DeviceAhi] = group.events / group.hours;
    return r;
}

bool reliable(const Row &row)
{
    return row.nights() >= kMinNights;
}

QSet<int> best(const QList<Row> &rows, Column column)
{
    if (column == Nights || column == Pressure || column == ColumnCount) return {};
    const bool higherIsBetter = (column == Usage);

    QList<int> candidates;
    for (int i = 0; i < rows.size(); ++i) {
        if (reliable(rows[i]) && !std::isnan(rows[i].values[column])) candidates << i;
    }
    if (candidates.size() < 2) return {};

    qint64 top = shown(rows[candidates.first()].values[column], column);
    for (int i : candidates) {
        const qint64 v = shown(rows[i].values[column], column);
        if (higherIsBetter ? v > top : v < top) top = v;
    }
    QSet<int> winners;
    for (int i : candidates) {
        if (shown(rows[i].values[column], column) == top) winners.insert(i);
    }
    return winners;
}

QString dateList(const QList<QDate> &dates)
{
    const QLocale locale;
    QStringList parts;
    int i = 0;
    while (i < dates.size()) {
        int j = i;
        while (j + 1 < dates.size() && dates[j].daysTo(dates[j + 1]) == 1) ++j;
        QString part = locale.toString(dates[i], QLocale::ShortFormat);
        if (j > i) part += QStringLiteral(" – ") + locale.toString(dates[j], QLocale::ShortFormat);
        parts << part;
        i = j + 1;
    }
    return parts.join(QStringLiteral("; "));
}

int decimals(Column column)
{
    switch (column) {
    case Nights:
        return 0;
    case Usage:
    case DeviceAhi:
    case AnalysisAhi:
    case Odi3:
        return 2;
    case Leak:
    case Pressure:
    case FlowLimitation:
    case Below90:
    case ColumnCount:
        break;
    }
    return 1;
}

} // namespace SettingsComparison
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh SettingsComparisonTests`
Expected: `SettingsComparisonTests: 7 PASS` (5 tests + init/cleanup), `0 unexpected`.

- [ ] **Step 6: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/oscar.pro oscar/settingscomparison.h oscar/settingscomparison.cpp oscar/tests/settingscomparisontests.h oscar/tests/settingscomparisontests.cpp
git commit -m "Settings comparison: merge settings periods and pick the best values

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `SettingsComparison::html()` — the table

**Files:**
- Modify: `oscar/settingscomparison.h`, `oscar/settingscomparison.cpp`
- Modify: `oscar/tests/settingscomparisontests.h`, `oscar/tests/settingscomparisontests.cpp`
- Modify: `Translations/Russkiy.ru.ts`

**Interfaces:**
- Consumes: everything Task 2 produces (`Row`, `row()`, `best()`, `reliable()`, `dateList()`, `decimals()`, `Column`).
- Produces:
  - `struct Options { bool showDevice = false; QString ahiName = QStringLiteral("AHI"); double percentile = 95; QString headingColor = QStringLiteral("#ffffff"); QStringList rowColors; };`
  - `QString html(const QList<Row> &rows, const Options &options);`
  - constants in the header: `const QString kBestColor = QStringLiteral("#c8f0c8");`, `const QString kFewColor = QStringLiteral("#909090");`, `const QString kNoData = QStringLiteral("–");`

- [ ] **Step 1: Declare the API**

Append to `oscar/settingscomparison.h`, before `} // namespace SettingsComparison`:

```cpp
//! Background of the best value in a column.
const QString kBestColor = QStringLiteral("#c8f0c8");
//! Text colour of rows with too few nights.
const QString kFewColor = QStringLiteral("#909090");
//! Shown where a row has no data for a column.
const QString kNoData = QStringLiteral("–");

struct Options {
    bool showDevice = false;            //!< more than one device in the history
    QString ahiName = QStringLiteral("AHI");
    double percentile = 95;             //!< of the pressure column
    QString headingColor = QStringLiteral("#ffffff");
    QStringList rowColors;              //!< background per row, cycled; white when empty
};

//! The whole comparison table: title, column heads, one row per group and the note.
QString html(const QList<Row> &rows, const Options &options);
```

Add a stub to `oscar/settingscomparison.cpp` inside the namespace:

```cpp
QString html(const QList<Row> &, const Options &) { return QString(); }
```

- [ ] **Step 2: Write the failing tests**

In `oscar/tests/settingscomparisontests.h` add the slots after `void testDateList();`:

```cpp
    void testHtmlMarksBestAndFewNights();
    void testHtmlShowsDashWithoutData();
    void testHtmlDeviceColumn();
    void testHtmlEscapesSettings();
```

Append to `oscar/tests/settingscomparisontests.cpp`:

```cpp
namespace {

Row tableRow(const QString &pressure, int nightCount, double ahi)
{
    Group g;
    g.mode = QStringLiteral("APAP");
    g.pressure = pressure;
    g.relief = QStringLiteral("SoftPAP: 1");
    g.deviceKey = g.deviceLabel = QStringLiteral("Prisma 20A");
    g.dates = nights(QDate(2026, 9, 1), nightCount);
    g.hours = 7.0 * nightCount;
    g.events = ahi * g.hours;
    return row(g);
}

QString number(double value, int decimals) { return QLocale().toString(value, 'f', decimals); }

} // namespace

void SettingsComparisonTests::testHtmlMarksBestAndFewNights()
{
    const QList<Row> rows { tableRow(QStringLiteral("9-16"), 4, 3.0), tableRow(QStringLiteral("6-9"), 5, 5.0),
                            tableRow(QStringLiteral("7-10"), 1, 1.0) };
    const QString html = SettingsComparison::html(rows, Options());

    // the best reliable AHI is green, the 1-night row's lower AHI is not
    QVERIFY2(html.contains(QStringLiteral("<td align=right bgcolor='%1'>%2</td>").arg(kBestColor, number(3.0, 2))),
             qPrintable(html));
    QVERIFY(!html.contains(QStringLiteral("bgcolor='%1'><font color='%2'>%3").arg(kBestColor, kFewColor, number(1.0, 2))));

    // the 1-night row is grey and says why
    QVERIFY(html.contains(QStringLiteral("<font color='%1'>APAP · 7-10 · SoftPAP: 1</font>").arg(kFewColor)));
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "%1 (few nights)").arg(1)));

    // the dates of a row are in its tooltip
    QVERIFY(html.contains(QStringLiteral("title='%1'").arg(dateList(rows[0].group.dates))));
}

void SettingsComparisonTests::testHtmlShowsDashWithoutData()
{
    Row r = tableRow(QStringLiteral("9-16"), 4, 3.0);
    r.values[Usage] = std::nan("");
    const QString html = SettingsComparison::html({ r }, Options());
    QVERIFY(html.contains(QStringLiteral("<td align=right>%1</td>").arg(kNoData)));     // analysis not filled in
    QVERIFY(!html.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
    QVERIFY(!html.contains(QStringLiteral("inf"), Qt::CaseInsensitive));
}

void SettingsComparisonTests::testHtmlDeviceColumn()
{
    const QList<Row> rows { tableRow(QStringLiteral("9-16"), 4, 3.0) };
    const QString device = QCoreApplication::translate("SettingsComparison", "Device");
    Options options;
    QVERIFY(!SettingsComparison::html(rows, options).contains(QStringLiteral("<th align=left>%1</th>").arg(device)));
    options.showDevice = true;
    const QString html = SettingsComparison::html(rows, options);
    QVERIFY(html.contains(QStringLiteral("<th align=left>%1</th>").arg(device)));
    QVERIFY(html.contains(QStringLiteral("<td>Prisma 20A</td>")));
}

void SettingsComparisonTests::testHtmlEscapesSettings()
{
    Row r = tableRow(QStringLiteral("Min <4 & Max 7"), 4, 3.0);
    const QString html = SettingsComparison::html({ r }, Options());
    QVERIFY(html.contains(QStringLiteral("Min &lt;4 &amp; Max 7")));
    QVERIFY(!html.contains(QStringLiteral("Min <4")));
}
```

Add `#include <QCoreApplication>` to the includes of `oscar/tests/settingscomparisontests.cpp`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:" | head -3; ./test > run.log 2>&1; grep -E "^FAIL!.*SettingsComparisonTests" run.log`
Expected: `FAIL!` for the four `testHtml…` slots; the five Task 2 tests still pass.

- [ ] **Step 4: Write the implementation**

Replace the `html()` stub in `oscar/settingscomparison.cpp` with the code below, and add `#include <QCoreApplication>` to its includes. Every visible string goes through `QCoreApplication::translate("SettingsComparison", …)` with a literal so `lupdate` finds it.

```cpp
namespace {

const QList<Column> kTableColumns { Nights, Usage, DeviceAhi, AnalysisAhi, Leak, Pressure, FlowLimitation, Odi3, Below90 };

QString columnTitle(Column column, const Options &options)
{
    const QLocale locale;
    switch (column) {
    case Nights: return QCoreApplication::translate("SettingsComparison", "Nights");
    case Usage: return QCoreApplication::translate("SettingsComparison", "Usage, h");
    case DeviceAhi: return QCoreApplication::translate("SettingsComparison", "Device %1").arg(options.ahiName);
    case AnalysisAhi: return QCoreApplication::translate("SettingsComparison", "Analysis AHI");
    case Leak: return QCoreApplication::translate("SettingsComparison", "Leak");
    case Pressure:
        return QCoreApplication::translate("SettingsComparison", "Pressure %1%").arg(locale.toString(options.percentile));
    case FlowLimitation: return QCoreApplication::translate("SettingsComparison", "Flow limitation, %");
    case Odi3: return QCoreApplication::translate("SettingsComparison", "ODI 3%");
    case Below90: return QCoreApplication::translate("SettingsComparison", "SpO2 < 90%");
    case ColumnCount: break;
    }
    return QString();
}

QString formatValue(double value, Column column)
{
    if (std::isnan(value) || std::isinf(value)) return kNoData;
    if (column == Nights) return QString::number(qRound(value));
    return QLocale().toString(value, 'f', decimals(column));
}

QString settingsText(const Group &g)
{
    QStringList parts;
    for (const QString &part : { g.mode, g.pressure, g.relief }) {
        if (!part.trimmed().isEmpty()) parts << part.trimmed();
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

QString html(const QList<Row> &rows, const Options &options)
{
    const QLocale locale;
    QVector<QSet<int>> winners(ColumnCount);
    for (Column c : kTableColumns) winners[c] = best(rows, c);

    const int span = 1 + (options.showDevice ? 1 : 0) + kTableColumns.size();
    QString html = QStringLiteral("<table class=curved width='100%' cellpadding=2>");
    html += QStringLiteral("<tr bgcolor='%1'><th colspan=%2 align=center><font size='+2'>%3</font></th></tr>")
                .arg(options.headingColor).arg(span)
                .arg(QCoreApplication::translate("SettingsComparison", "Device Settings Compared").toHtmlEscaped());

    html += QStringLiteral("<tr><th align=left>%1</th>")
                .arg(QCoreApplication::translate("SettingsComparison", "Settings").toHtmlEscaped());
    if (options.showDevice) {
        html += QStringLiteral("<th align=left>%1</th>")
                    .arg(QCoreApplication::translate("SettingsComparison", "Device").toHtmlEscaped());
    }
    for (Column c : kTableColumns) {
        QString head = columnTitle(c, options).toHtmlEscaped();
        if (c == Pressure) {
            const QString tip = QCoreApplication::translate("SettingsComparison",
                                    "Average over the nights of each night's %1th percentile")
                                    .arg(locale.toString(options.percentile));
            head = QStringLiteral("<span title='%1'>%2</span>").arg(tip.toHtmlEscaped(), head);
        }
        html += QStringLiteral("<th align=right>%1</th>").arg(head);
    }
    html += QStringLiteral("</tr>");

    for (int i = 0; i < rows.size(); ++i) {
        const Row &r = rows[i];
        const bool few = !reliable(r);
        auto look = [few](const QString &text) {
            return few ? QStringLiteral("<font color='%1'>%2</font>").arg(kFewColor, text) : text;
        };
        const QString background = options.rowColors.isEmpty() ? QStringLiteral("#ffffff")
                                                               : options.rowColors[i % options.rowColors.size()];
        html += QStringLiteral("<tr bgcolor='%1'>").arg(background);
        html += QStringLiteral("<td><span title='%1'>%2</span></td>")
                    .arg(dateList(r.group.dates).toHtmlEscaped(), look(settingsText(r.group).toHtmlEscaped()));
        if (options.showDevice) html += QStringLiteral("<td>%1</td>").arg(look(r.group.deviceLabel.toHtmlEscaped()));
        for (Column c : kTableColumns) {
            QString text = formatValue(r.values[c], c);
            if (c == Nights && few) {
                text = QCoreApplication::translate("SettingsComparison", "%1 (few nights)").arg(r.nights());
            }
            const QString mark = winners[c].contains(i) ? QStringLiteral(" bgcolor='%1'").arg(kBestColor) : QString();
            html += QStringLiteral("<td align=right%1>%2</td>").arg(mark, look(text.toHtmlEscaped()));
        }
        html += QStringLiteral("</tr>");
    }

    const QString note = QCoreApplication::translate("SettingsComparison",
        "Green marks the best value among settings used for at least %1 nights. Grey rows have fewer "
        "nights, too few to judge. Pressure is the average over the nights.").arg(kMinNights);
    html += QStringLiteral("<tr><td colspan=%1 align=center><i>%2</i></td></tr>").arg(span).arg(note.toHtmlEscaped());
    html += QStringLiteral("</table>");
    return html;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh SettingsComparisonTests`
Expected: `SettingsComparisonTests: 11 PASS`, `0 unexpected`.

- [ ] **Step 6: Translate the new strings**

Run `cd /Users/semyk/Downloads/Oscar_Project/oscar-sql && /opt/homebrew/bin/lupdate oscar/oscar.pro -ts Translations/Russkiy.ru.ts`, then in context `SettingsComparison` set these translations (remove `type="unfinished"`):

| Source | Russian |
|---|---|
| `Nights` | `Ночей` |
| `Usage, h` | `Исп., ч` |
| `Device %1` | `%1 аппарата` |
| `Analysis AHI` | `AHI анализа` |
| `Leak` | `Утечка` |
| `Pressure %1%` | `Давление %1 %` |
| `Flow limitation, %` | `Огр. потока, %` |
| `ODI 3%` | `ODI 3 %` |
| `SpO2 &lt; 90%` | `SpO2 &lt; 90 %` |
| `Device Settings Compared` | `Сравнение настроек аппарата` |
| `Settings` | `Настройки` |
| `Device` | `Аппарат` |
| `Average over the nights of each night&apos;s %1th percentile` | `Среднее по ночам: %1-й процентиль каждой ночи` |
| `%1 (few nights)` | `%1 (мало ночей)` |
| `Green marks the best value among settings used for at least %1 nights. Grey rows have fewer nights, too few to judge. Pressure is the average over the nights.` | `Зелёным отмечено лучшее значение среди настроек, на которых было не меньше %1 ночей. Серые строки — ночей меньше, выводы по ним делать рано. Давление — среднее по ночам.` |

Check: `grep -c 'type="unfinished"' Translations/Russkiy.ru.ts` → `0`; `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`.

- [ ] **Step 7: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/settingscomparison.h oscar/settingscomparison.cpp oscar/tests/settingscomparisontests.h oscar/tests/settingscomparisontests.cpp Translations/Russkiy.ru.ts
git commit -m "Settings comparison: the HTML table with best values and few-night rows

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Statistics mode «Настройки»

**Files:**
- Modify: `oscar/SleepLib/profiles.h:447-450` (mode constants)
- Modify: `oscar/statistics.h` (declare `GenerateSettingsComparison`)
- Modify: `oscar/statistics.cpp` (`GenerateHTML`, new `GenerateSettingsComparison`, include)
- Modify: `oscar/mainwindow.ui` (radio button after `reportModeRange`)
- Modify: `oscar/mainwindow.h:391` (slot), `oscar/mainwindow.cpp` (`reset_reportModeUi()` switch ~3909, new slot after `on_reportModeRange_clicked()` ~3965)
- Modify: `Translations/Russkiy.ru.ts`

**Interfaces:**
- Consumes: `analysisFigureValue()` (Task 1); `SettingsComparison::Period`, `group()`, `row()`, `Row`, `Column`, `Options`, `html()` (Tasks 2–3); existing `analysisRows(start, end)` (anonymous namespace at the top of `statistics.cpp`), `updateRXChanges()`, `rxitems`, `formatRelief()` (`SleepLib/common.h`), `alternatingColor(int&)`, `heading_color`, `AnalysisPanel::disclaimer()`, `AnalysisService::outdatedCount()`.
- Produces: `const int STAT_MODE_SETTINGS = 3;` in `profiles.h`; `QString Statistics::GenerateSettingsComparison();`; `MainWindow::on_reportModeSettings_clicked()`.

There is no unit test for this glue: it needs a loaded profile. It is verified by the full suite staying green and by the app check in Steps 6–7.

- [ ] **Step 1: The mode constant and the generator**

`oscar/SleepLib/profiles.h`, after `const int STAT_MODE_RANGE = 2;`:

```cpp
const int STAT_MODE_SETTINGS = 3;   //!< one table comparing every set of device settings
```

`oscar/statistics.h`, after `QString GenerateRXChanges();`:

```cpp
    //! The "Settings" mode: how the nights went on each set of device settings.
    QString GenerateSettingsComparison();
```

`oscar/statistics.cpp`: add `#include "settingscomparison.h"` after `#include "analysispanel.h"`. Add after the end of `Statistics::GenerateRXChanges()`:

```cpp
QString Statistics::GenerateSettingsComparison()
{
    if (p_profile->GetMachines(MT_CPAP).isEmpty()) return QString();
    updateRXChanges();
    if (rxitems.isEmpty()) return QString();

    const bool rdi = p_profile->general->calculateRDI();
    const bool byBrand = AppSetting->combineSimilarMachines();

    // every settings period, then the periods with the same settings merged
    QList<SettingsComparison::Period> periods;
    QSet<QString> devices;
    for (const RXItem &rx : std::as_const(rxitems)) {
        if (!rx.machine) continue;
        SettingsComparison::Period p;
        p.mode = rx.mode;
        p.pressure = rx.pressure;
        p.relief = formatRelief(rx.relief);
        p.deviceKey = byBrand ? rx.machine->brand() : rx.machine->model() + QLatin1Char(' ') + rx.machine->serial();
        p.deviceLabel = byBrand ? rx.machine->brand()
                                : QString("%1 (%2)").arg(rx.machine->model(), rx.machine->modelnumber());
        p.dates = rx.dates.keys();
        p.hours = rx.hours;
        p.events = rdi ? rx.rdi : rx.ahi;
        periods << p;
        devices.insert(p.deviceKey);
    }
    const QList<SettingsComparison::Group> groups = SettingsComparison::group(periods);

    const QList<AnalysisDailyData> analysis = analysisRows(p_profile->FirstDay(), p_profile->LastDay());
    const double percentile = p_profile->general->prefCalcPercentile();
    QList<SettingsComparison::Row> rows;
    for (const SettingsComparison::Group &g : groups) {
        SettingsComparison::Row row = SettingsComparison::row(g);
        const QSet<QDate> dates(g.dates.cbegin(), g.dates.cend());

        QList<AnalysisDailyData> own;
        for (const AnalysisDailyData &d : analysis) {
            if (dates.contains(d.date)) own << d;
        }
        row.values[SettingsComparison::AnalysisAhi] = analysisFigureValue(QStringLiteral("ahi"), own);
        row.values[SettingsComparison::FlowLimitation] = analysisFigureValue(QStringLiteral("fl"), own);
        row.values[SettingsComparison::Odi3] = analysisFigureValue(QStringLiteral("odi3"), own);
        row.values[SettingsComparison::Below90] = analysisFigureValue(QStringLiteral("below:90"), own);

        // leak and pressure: each night's figure, weighted by its hours
        double leak = 0, leakHours = 0, pressure = 0, pressureHours = 0;
        for (const QDate &date : g.dates) {
            Day *day = p_profile->GetDay(date, MT_CPAP);
            if (!day) continue;
            day->OpenSummary();   // nights read from RXChanges.cache may not have it yet
            const double h = day->hours(MT_CPAP);
            if (h <= 0) continue;
            if (day->channelHasData(CPAP_Leak)) {
                leak += day->wavg(CPAP_Leak) * h;
                leakHours += h;
            }
            if (day->channelHasData(CPAP_Pressure)) {
                pressure += day->percentile(CPAP_Pressure, percentile / 100.0) * h;
                pressureHours += h;
            }
        }
        if (leakHours > 0) row.values[SettingsComparison::Leak] = leak / leakHours;
        if (pressureHours > 0) row.values[SettingsComparison::Pressure] = pressure / pressureHours;
        rows << row;
    }

    SettingsComparison::Options options;
    options.showDevice = devices.size() > 1;
    options.ahiName = rdi ? STR_TR_RDI : STR_TR_AHI;
    options.percentile = percentile;
    options.headingColor = heading_color;
    int counter = 0;
    for (int i = 0; i < rows.size(); ++i) options.rowColors << alternatingColor(counter);

    QString html = QStringLiteral("<div align=center><br>");
    html += QString("<p><i>%1</i></p>").arg(AnalysisPanel::disclaimer().toHtmlEscaped());
    analysis::AnalysisService *service = mainwin ? mainwin->analysisService() : nullptr;
    const int outdated = service ? service->outdatedCount() : 0;
    if (outdated > 0) {
        html += QString("<p>%1 <a href='analysis=recalculate'>%2</a></p>")
                    .arg(tr("Analysis is outdated for %n day(s).", "", outdated), tr("Recalculate"));
    }
    html += SettingsComparison::html(rows, options);
    html += QStringLiteral("</div>");
    return html;
}
```

If `heading_color` or `alternatingColor` is defined below this point in `statistics.cpp`, place `GenerateSettingsComparison()` after both definitions (they are at lines ~180–215 and ~1429; `GenerateRXChanges()` at ~1498 is already after both).

- [ ] **Step 2: Use it in `GenerateHTML()`**

In `Statistics::GenerateHTML()`, right after `htmlReportFooter = generateFooter(true);`, insert:

```cpp
    if (p_profile->general->statReportMode() == STAT_MODE_SETTINGS) {
        // one table; the blocks of the other modes stay empty so printing shows just this
        htmlUsage = GenerateSettingsComparison();
        htmlMachineSettings.clear();
        htmlMachines.clear();
        if (htmlUsage.isEmpty()) return htmlReportHeader + htmlNoData() + htmlReportFooter;
        return htmlReportHeader + htmlUsage + htmlReportFooter;
    }
```

- [ ] **Step 3: The radio button**

`oscar/mainwindow.ui`: after the `<item>` holding `reportModeRange` (it ends right before the `<item>` with `QDateEdit name="statStartDate"`), insert:

```xml
             <item>
              <widget class="QRadioButton" name="reportModeSettings">
               <property name="toolTip">
                <string>Compare how the nights went on each set of device settings</string>
               </property>
               <property name="text">
                <string>Settings</string>
               </property>
              </widget>
             </item>
```

`oscar/mainwindow.h`, after `void on_reportModeRange_clicked();`:

```cpp
    void on_reportModeSettings_clicked();
```

`oscar/mainwindow.cpp`, in the `switch (mode)` that sets the report-mode widgets (around line 3909), add before the closing `}` of the switch:

```cpp
        case STAT_MODE_SETTINGS:
            // the whole history, so no dates to pick
            ui->reportModeSettings->setChecked(true);
            ui->statStartDate->setVisible(false);
            ui->statEndDate->setVisible(false);
            ui->statEnableEndDisplay->setVisible(false);
            break;
```

and after `MainWindow::on_reportModeRange_clicked()`:

```cpp
void MainWindow::on_reportModeSettings_clicked()
{
    if (p_profile->general->statReportMode() != STAT_MODE_SETTINGS) {
        p_profile->general->setStatReportMode(STAT_MODE_SETTINGS);
        GenerateStatistics();
    }
}
```

The switch lives in `MainWindow::reset_reportModeUi()`, which `GenerateStatistics()` already calls, so the date pickers hide on the click without more code.

- [ ] **Step 4: Build all three and run the suite**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project
for d in build build-test build-nobt; do (cd $d && Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q" >/dev/null 2>&1; make -j10 > ../$d.log 2>&1; echo "$d make $?"; grep -E "error:" ../$d.log | head -3); done
tools/runtests.sh SettingsComparisonTests AnalysisPanelTests
```
Expected: `make 0` three times; both classes PASS; `0 unexpected`.

- [ ] **Step 5: Translate**

Run lupdate (see Global Constraints) and set, in context `MainWindow`:

| Source | Russian |
|---|---|
| `Settings` | `Настройки` |
| `Compare how the nights went on each set of device settings` | `Сравнить, как проходили ночи на каждом наборе настроек аппарата` |

Then `grep -c 'type="unfinished"'` → 0 and `validate_ts.py` → `TOTAL 0`. Re-run qmake + make in `../build` so the new `.qm` is in the app.

- [ ] **Step 6: Check in the app (profile «Папа»)**

Quit any running OSCAR, launch it on the dev data folder (Global Constraints), open «Статистика», click «Настройки». Expected:
- one table «Сравнение настроек аппарата», newest settings first;
- the row «APAP (дин) · Мин 9.0 Макс 16.0 (см H2O) · SoftPAP: 1 - Слабый» has 4 nights (20.09 and 21–23.09 merged), and its tooltip lists both stretches;
- 1–2-night rows are grey with «(мало ночей)»;
- the «Утечка» and «Давление 95 %» columns show numbers for the September rows, not «–»;
- green cells only in rows with 3+ nights;
- no date pickers in the bottom bar.

- [ ] **Step 7: Check the saved mode survives a restart**

Quit OSCAR with «Настройки» selected, launch again, open «Статистика». Expected: «Настройки» is selected, the comparison table shows, no date pickers. Switch back to «Стандартный»: the usual report and its date picker return.

- [ ] **Step 8: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/SleepLib/profiles.h oscar/statistics.h oscar/statistics.cpp oscar/mainwindow.ui oscar/mainwindow.h oscar/mainwindow.cpp Translations/Russkiy.ru.ts
git commit -m "Statistics: \"Settings\" mode comparing every set of device settings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: Mark the handoff item done (fork-only)**

In `docs/superpowers/plans/2026-10-01-handoff.md`, section 4.2, item 4, add a `[x]` note: settings comparison done (commit hash of Step 8, spec `specs/2026-10-02-settings-comparison-design.md`); the one-page PDF for the doctor is still open. Then:

```bash
git add -f docs/superpowers/plans/2026-10-01-handoff.md
git commit -m "Handoff plan: settings comparison done (fork-only, not for MR)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
