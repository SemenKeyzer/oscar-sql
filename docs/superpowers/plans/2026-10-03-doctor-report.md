# Doctor Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** «Файл → Отчёт для врача (PDF)…» saves a one-page A4 PDF: header with the current settings, six target tiles, a night-by-night chart with settings changes, and the settings comparison for the chosen dates.

**Architecture:** A new module `doctorreport` holds the page's data (`struct DoctorReport`) and renders it: tile levels, `chart()` (QPainter into a QImage), `html()` and `writePdf()` (QTextDocument → QPrinter). `Statistics` fills the model (`doctorReport(from, to)`) and gains `settingsComparisonRows(from, to)`, shared with the «Настройки» mode. A small dialog picks the dates and saves the file.

**Tech Stack:** Qt 6.11 (Homebrew; Widgets, Gui, PrintSupport), qmake, C++17, QtTest via the project's `AutoTest.h`.

**Spec:** `docs/superpowers/specs/2026-10-03-doctor-report-design.md`

## Global Constraints

- Work on `master` of the fork (`/Users/semyk/Downloads/Oscar_Project/oscar-sql`); push only to `origin`, and only when the user says so.
- Build dirs: `../build` (app), `../build-test` (tests), `../build-nobt`. After adding files to `oscar/oscar.pro` or changing `.ts`, re-run qmake in each: `Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q"`, then `make -j10`.
- Tests: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Class…]` — 0 unexpected failures (currently 378 pass).
- Russian translation of every new string in `Translations/Russkiy.ru.ts`: run `/opt/homebrew/bin/lupdate oscar/oscar.pro -ts Translations/Russkiy.ru.ts`, replace each new `<translation type="unfinished">…</translation>` with `<translation>…</translation>`, then `grep -c 'type="unfinished"' Translations/Russkiy.ru.ts` → `0` and `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`.
- Running OSCAR: only `open -n /Users/semyk/Downloads/Oscar_Project/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`; never `~/Documents/OSCAR20_Data`, never two instances; quit via the app menu «OSCAR20 → Quit OSCAR20». If no window appears after a rebuild, macOS is waiting for Documents access — ask the user to allow it.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; fork-only docs with `git add -f` in their own commit ending `(fork-only, not for MR)`.
- Spec values: A4 portrait, margins 12 mm; default period = last CPAP night − 29 days … last CPAP night; profile key `DoctorReportFrom`; targets `NightSummary::kAhiTarget` (5), `kOdiTarget` (5), `kT90Target` (5); usage target `p_profile->cpap->complianceHours()`; hours are CPAP-only (`day->hours(MT_CPAP)`); «–» for no data; one page with up to 12 comparison rows.
- Code style: match OSCAR (4-space indent, `//!` doc comments in headers, file header block with `Copyright (c) 2026 The OSCAR Team`).

## Review Focus

1. A year-long period (365 nights) — the chart still draws every night's bar and thins the date labels instead of crashing or smearing (Task 2 test `testChartLongPeriod`).
2. Analysis never computed — every analysis figure reads «–», never `nan` (Task 3 test `testHtmlWithoutAnalysis`).
3. An unwritable path — `writePdf()` returns false with a message and the dialog stays open; no crash (Task 3 test `testWritePdfFailsOnBadPath`).
4. A profile in RDI mode — the AHI tile and the comparison table say «RDI» (Task 3 test `testHtmlRdi`).
5. Current settings that started before the chosen period — the header says «с <that earlier date>» (Task 4 test `testDoctorReportSettingsSince`).

---

### Task 1: `settingsComparisonRows(from, to)` and `settingsLabel()`

**Files:**
- Modify: `oscar/settingscomparison.h`, `oscar/settingscomparison.cpp` (`settingsText` → public `settingsLabel`)
- Modify: `oscar/statistics.h`, `oscar/statistics.cpp` (`GenerateSettingsComparison` split)
- Test: `oscar/tests/settingscomparisontests.{h,cpp}`, `oscar/tests/analysisintegrationtests.{h,cpp}`

**Interfaces:**
- Consumes: existing `SettingsComparison::Period/Group/Row/group()/row()/Options/html()`, `Statistics::updateRXChanges()`, `rxitems`, `analysisRows()`, `analysisFigureValue()`.
- Produces:
  - `QString SettingsComparison::settingsLabel(const QString &mode, const QString &pressure, const QString &relief);` — parts trimmed, empty ones left out, joined with `" · "`.
  - `QList<SettingsComparison::Row> Statistics::settingsComparisonRows(const QDate &from, const QDate &to, bool *showDevice = nullptr);` (public) — rows over the nights in `[from, to]` only; hours and events recounted per night.

- [ ] **Step 1: Write the failing tests**

`oscar/tests/settingscomparisontests.h`: add the slot `void testSettingsLabel();` after `void testDateList();`.

`oscar/tests/settingscomparisontests.cpp`, append:

```cpp
void SettingsComparisonTests::testSettingsLabel()
{
    QCOMPARE(settingsLabel(QStringLiteral("APAP"), QStringLiteral("Min 7 Max 10"), QStringLiteral(" SoftPAP: 1 ")),
             QStringLiteral("APAP · Min 7 Max 10 · SoftPAP: 1"));
    QCOMPARE(settingsLabel(QStringLiteral("CPAP"), QString(), QStringLiteral("  ")), QStringLiteral("CPAP"));
}
```

`oscar/tests/analysisintegrationtests.h`: add `void testSettingsComparisonRowsTrimmedToDates();` after `void testSettingsPeriodCountsCpapHoursOnly();`.

`oscar/tests/analysisintegrationtests.cpp`: add `#include <QFile>` if missing (it is already there) and append:

```cpp
void AnalysisIntegrationTests::testSettingsComparisonRowsTrimmedToDates()
{
    // Two nights on the same settings; the comparison for the second night alone counts
    // just that night, its hours recounted from the night itself.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    const QDate first = kNightDate.addDays(50), second = first.addDays(1);
    Machine cpap(p_profile, 62);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Day *a = new Day();
    a->setDate(first);
    a->addSession(hypopneaSession(&cpap, 90, m_machineRow));
    Day *b = new Day();
    b->setDate(second);
    b->addSession(hypopneaSession(&cpap, 91, m_machineRow));
    p_profile->daylist.insert(first, a);
    p_profile->daylist.insert(second, b);

    Statistics stats;
    bool showDevice = true;
    const QList<SettingsComparison::Row> rows = stats.settingsComparisonRows(second, second, &showDevice);
    p_profile->daylist.remove(first);
    p_profile->daylist.remove(second);

    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().group.dates, QList<QDate>({ second }));
    QCOMPARE(rows.first().group.hours, double(b->hours(MT_CPAP)));
    QVERIFY(!showDevice);
    delete a;
    delete b;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:" | head -3`
Expected: compile errors — `settingsLabel` undeclared, `settingsComparisonRows` is not a member of `Statistics`.

- [ ] **Step 3: Implement**

`oscar/settingscomparison.h`, after the `dateList` declaration:

```cpp
//! "mode · pressure · relief": each part trimmed, empty ones left out.
QString settingsLabel(const QString &mode, const QString &pressure, const QString &relief);
```

`oscar/settingscomparison.cpp`: replace the anonymous-namespace function

```cpp
QString settingsText(const Group &g)
{
    QStringList parts;
    for (const QString &part : { g.mode, g.pressure, g.relief }) {
        if (!part.trimmed().isEmpty()) parts << part.trimmed();
    }
    return parts.join(QStringLiteral(" · "));
}
```

with

```cpp
QString settingsText(const Group &g)
{
    return settingsLabel(g.mode, g.pressure, g.relief);
}
```

and add, outside the anonymous namespace (after `dateList()`):

```cpp
QString settingsLabel(const QString &mode, const QString &pressure, const QString &relief)
{
    QStringList parts;
    for (const QString &part : { mode, pressure, relief }) {
        if (!part.trimmed().isEmpty()) parts << part.trimmed();
    }
    return parts.join(QStringLiteral(" · "));
}
```

(`settingsText` is defined in the anonymous namespace above `html()`; `settingsLabel` must be declared before it is used — the header declaration covers that.)

`oscar/statistics.h`: in the `public:` section after `QString UpdateRecordsBox();` add:

```cpp
    //! The settings comparison over the nights in [from, to]; \a showDevice says whether
    //! more than one device took part.
    QList<SettingsComparison::Row> settingsComparisonRows(const QDate &from, const QDate &to, bool *showDevice = nullptr);
```

and add `#include "settingscomparison.h"` to the includes of `oscar/statistics.h` (it is already included by `statistics.cpp`; keep that too).

`oscar/statistics.cpp`: replace the whole of `QString Statistics::GenerateSettingsComparison()` with these two functions (the row building moves into the new one):

```cpp
QList<SettingsComparison::Row> Statistics::settingsComparisonRows(const QDate &from, const QDate &to, bool *showDevice)
{
    QList<SettingsComparison::Row> rows;
    if (showDevice) *showDevice = false;
    if (p_profile->GetMachines(MT_CPAP).isEmpty()) return rows;
    updateRXChanges();

    const bool rdi = p_profile->general->calculateRDI();
    const bool byBrand = AppSetting->combineSimilarMachines();

    // every settings period cut to the nights in [from, to], then equal settings merged;
    // hours and events are counted per night so a period cut in two counts only its part
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
        for (auto it = rx.dates.cbegin(); it != rx.dates.cend(); ++it) {
            const QDate &date = it.key();
            if (date < from || date > to) continue;
            Day *day = p_profile->GetDay(date, MT_CPAP);
            if (!day) continue;
            const double h = day->hours(MT_CPAP);
            if (h <= 0) continue;
            p.dates << date;
            p.hours += h;
            p.events += day->count(AllAhiChannels) + (rdi ? day->count(CPAP_RERA) : 0);
        }
        if (p.dates.isEmpty()) continue;
        periods << p;
        devices.insert(p.deviceKey);
    }
    const QList<SettingsComparison::Group> groups = SettingsComparison::group(periods);

    const QList<AnalysisDailyData> analysis = analysisRows(from, to);
    const double percentile = p_profile->general->prefCalcPercentile();
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
            Day *day = p_profile->GetDay(date, MT_CPAP);   // opens the night's summary
            if (!day) continue;
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
    if (showDevice) *showDevice = devices.size() > 1;
    return rows;
}

QString Statistics::GenerateSettingsComparison()
{
    bool showDevice = false;
    const QList<SettingsComparison::Row> rows =
        settingsComparisonRows(p_profile->FirstDay(), p_profile->LastDay(), &showDevice);
    if (rows.isEmpty()) return QString();

    const bool rdi = p_profile->general->calculateRDI();
    SettingsComparison::Options options;
    options.showDevice = showDevice;
    options.ahiName = rdi ? STR_TR_RDI : STR_TR_AHI;
    options.percentile = p_profile->general->prefCalcPercentile();
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

The old `GenerateSettingsComparison()` is replaced entirely by these two functions. Also move the stray comment `// Report no data available` from above `GenerateSettingsComparison()` back above `QString Statistics::htmlNoData()`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh SettingsComparisonTests AnalysisIntegrationTests`
Expected: both classes PASS (SettingsComparisonTests 12 PASS, AnalysisIntegrationTests 24 PASS), `0 unexpected`.

- [ ] **Step 5: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/settingscomparison.h oscar/settingscomparison.cpp oscar/statistics.h oscar/statistics.cpp oscar/tests/settingscomparisontests.h oscar/tests/settingscomparisontests.cpp oscar/tests/analysisintegrationtests.h oscar/tests/analysisintegrationtests.cpp
git commit -m "Statistics: settings comparison rows for any span of dates

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `DoctorReport` model, tile levels and the night-by-night chart

**Files:**
- Create: `oscar/doctorreport.h`, `oscar/doctorreport.cpp`
- Create: `oscar/tests/doctorreporttests.h`, `oscar/tests/doctorreporttests.cpp`
- Modify: `oscar/oscar.pro` (sources/headers after `settingscomparison.*`, tests after `tests/settingscomparisontests.*`)

**Interfaces:**
- Consumes: `NightSummary::Level`, `NightSummary::kAhiTarget/kOdiTarget/kT90Target`, `NightSummaryView::levelColor()` (`nightsummary.h`); `SettingsComparison::Row` (`settingscomparison.h`).
- Produces (`oscar/doctorreport.h`):
  - `struct DoctorReport` with fields listed in the header below, `int days() const`, `usageLevel() / ahiLevel() / leakLevel() / odiLevel() / below90Level()` → `NightSummary::Level`, and `static constexpr double kNoValue` (NaN).
  - `namespace DoctorReportPage { struct ChartLayout { QRect ahi; QRect hours; double column; int left; }; ChartLayout chartLayout(const QSize &, int nights); QColor hoursColor(); QColor changeColor(); QImage chart(const DoctorReport &, const QSize &); }`

- [ ] **Step 1: Header and stub**

`oscar/doctorreport.h`:

```cpp
/* Doctor Report Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORT_H
#define DOCTORREPORT_H

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>

#include "nightsummary.h"
#include "settingscomparison.h"

//! The one-page report for the doctor: how the therapy went over a span of dates.
struct DoctorReport {
    static constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

    //! One date of the period; no hours and AHI when nothing was recorded that night.
    struct Night {
        QDate date;
        double hours = kNoValue;
        double ahi = kNoValue;
    };

    // header
    QString patient;                //!< empty when personal data is hidden
    QDate birthDate;                //!< invalid when hidden or unknown
    QStringList cpapDevices;
    QString oximeter;               //!< empty without oximetry in the period
    QDate from;
    QDate to;
    int nights = 0;                 //!< nights with CPAP data
    int oximetryNights = 0;
    QString currentSettings;        //!< empty when unknown
    QDate settingsSince;

    // totals
    double meanHours = kNoValue;
    int compliantNights = 0;
    double complianceHours = 4;
    QString ahiName = QStringLiteral("AHI");
    double deviceAhi = kNoValue;
    double analysisAhi = kNoValue;
    double flowLimitation = kNoValue;
    double leak = kNoValue;
    QString leakUnits;
    double leakRedline = 0;         //!< 0: none set
    double pressure = kNoValue;
    QString pressureUnits;
    double percentile = 95;
    double odi3 = kNoValue;
    double below90 = kNoValue;

    QVector<Night> nightList;       //!< every date of the period, oldest first
    QList<QDate> settingsChanges;   //!< nights a new settings period started on
    QList<SettingsComparison::Row> comparison;
    bool showDevice = false;

    //! Dates in the period, nights without data included.
    int days() const { return from.isValid() && to.isValid() ? int(from.daysTo(to)) + 1 : 0; }
    NightSummary::Level usageLevel() const;
    NightSummary::Level ahiLevel() const;
    NightSummary::Level leakLevel() const;
    NightSummary::Level odiLevel() const;
    NightSummary::Level below90Level() const;

    // DoctorReport::tr(), so lupdate finds the page's texts; last, as the macro ends in private:
    Q_DECLARE_TR_FUNCTIONS(DoctorReport)
};

//! Turns a DoctorReport into the chart, the page and the PDF.
namespace DoctorReportPage {

//! Where the chart puts things, for drawing and for tests.
struct ChartLayout {
    QRect ahi;              //!< the AHI bars' area
    QRect hours;            //!< the hours bars' area
    double column = 0;      //!< width of one night
    int left = 0;           //!< x of the first night
};

ChartLayout chartLayout(const QSize &size, int nights);
//! Colour of the hours bars at or above the compliance hours.
QColor hoursColor();
//! Colour of the lines where the settings changed.
QColor changeColor();
//! AHI (top) and hours of use (bottom) for every night, settings changes as grey lines.
QImage chart(const DoctorReport &report, const QSize &size);

} // namespace DoctorReportPage

#endif // DOCTORREPORT_H
```

`oscar/doctorreport.cpp` (stub):

```cpp
/* Doctor Report
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreport.h"

NightSummary::Level DoctorReport::usageLevel() const { return NightSummary::Unknown; }
NightSummary::Level DoctorReport::ahiLevel() const { return NightSummary::Unknown; }
NightSummary::Level DoctorReport::leakLevel() const { return NightSummary::Unknown; }
NightSummary::Level DoctorReport::odiLevel() const { return NightSummary::Unknown; }
NightSummary::Level DoctorReport::below90Level() const { return NightSummary::Unknown; }

namespace DoctorReportPage {

ChartLayout chartLayout(const QSize &, int) { return ChartLayout(); }
QColor hoursColor() { return QColor(); }
QColor changeColor() { return QColor(); }
QImage chart(const DoctorReport &, const QSize &) { return QImage(); }

} // namespace DoctorReportPage
```

Register in `oscar/oscar.pro`: `doctorreport.cpp \` after `settingscomparison.cpp \`; `doctorreport.h \` after `settingscomparison.h \`; `tests/doctorreporttests.cpp \` after `tests/settingscomparisontests.cpp \`; `tests/doctorreporttests.h \` after `tests/settingscomparisontests.h \`.

- [ ] **Step 2: Write the failing tests**

`oscar/tests/doctorreporttests.h`:

```cpp
/* Doctor Report Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORTTESTS_H
#define DOCTORREPORTTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the one-page report for the doctor.
class DoctorReportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLevels();
    void testChartLayout();
    void testChart();
    void testChartLongPeriod();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(DoctorReportTests)

#endif // DOCTORREPORTTESTS_H
```

`oscar/tests/doctorreporttests.cpp`:

```cpp
/* Doctor Report Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreporttests.h"

#include <QApplication>
#include <cmath>

#include "doctorreport.h"

using namespace DoctorReportPage;

namespace {

// Five nights from 01.09.2026: the second without data; settings changed on the fourth.
DoctorReport sampleReport()
{
    DoctorReport r;
    r.from = QDate(2026, 9, 1);
    r.to = QDate(2026, 9, 5);
    const double hours[] = { 7.5, DoctorReport::kNoValue, 3, 6, 8 };
    const double ahi[] = { 3, DoctorReport::kNoValue, 8, 2, 4 };
    for (int i = 0; i < 5; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = hours[i];
        n.ahi = ahi[i];
        r.nightList << n;
    }
    r.nights = 4;
    r.settingsChanges << QDate(2026, 9, 4);
    return r;
}

QColor pixel(const QImage &image, double x, int y) { return QColor(image.pixel(int(x), y)); }

} // namespace

void DoctorReportTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void DoctorReportTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void DoctorReportTests::testLevels()
{
    DoctorReport r;
    QCOMPARE(r.usageLevel(), NightSummary::Unknown);
    QCOMPARE(r.ahiLevel(), NightSummary::Unknown);
    QCOMPARE(r.leakLevel(), NightSummary::Unknown);
    QCOMPARE(r.odiLevel(), NightSummary::Unknown);
    QCOMPARE(r.below90Level(), NightSummary::Unknown);

    r.complianceHours = 4;
    r.meanHours = 6;
    QCOMPARE(r.usageLevel(), NightSummary::Good);
    r.meanHours = 3.9;
    QCOMPARE(r.usageLevel(), NightSummary::Attention);

    r.deviceAhi = 4.9;
    QCOMPARE(r.ahiLevel(), NightSummary::Good);
    r.deviceAhi = 5;
    QCOMPARE(r.ahiLevel(), NightSummary::Attention);

    r.leak = 10;
    QCOMPARE(r.leakLevel(), NightSummary::Unknown);     // no red line set
    r.leakRedline = 24;
    QCOMPARE(r.leakLevel(), NightSummary::Good);
    r.leak = 30;
    QCOMPARE(r.leakLevel(), NightSummary::Attention);

    r.odi3 = 4;
    QCOMPARE(r.odiLevel(), NightSummary::Good);
    r.odi3 = 6;
    QCOMPARE(r.odiLevel(), NightSummary::Attention);

    r.below90 = 4.9;
    QCOMPARE(r.below90Level(), NightSummary::Good);
    r.below90 = 5;
    QCOMPARE(r.below90Level(), NightSummary::Attention);
}

void DoctorReportTests::testChartLayout()
{
    const ChartLayout l = chartLayout(QSize(800, 320), 5);
    QCOMPARE(l.left, l.ahi.left());
    QCOMPARE(l.hours.left(), l.ahi.left());
    QCOMPARE(l.column, l.ahi.width() / 5.0);
    QVERIFY(l.hours.top() > l.ahi.bottom());
    QVERIFY(l.hours.bottom() < 320);
    QVERIFY(l.ahi.height() > l.hours.height());
}

void DoctorReportTests::testChart()
{
    const DoctorReport r = sampleReport();
    const QSize size(800, 320);
    const QImage image = chart(r, size);
    QCOMPARE(image.size(), size);

    const ChartLayout l = chartLayout(size, 5);
    auto centre = [&](int night) { return l.left + (night + 0.5) * l.column; };
    const int ahiBottom = l.ahi.top() + l.ahi.height() - 3;
    const int hoursBottom = l.hours.top() + l.hours.height() - 3;

    // AHI under the target is green, above it orange; the night without data stays empty
    QCOMPARE(pixel(image, centre(0), ahiBottom), NightSummaryView::levelColor(NightSummary::Good));
    QCOMPARE(pixel(image, centre(2), ahiBottom), NightSummaryView::levelColor(NightSummary::Attention));
    QCOMPARE(pixel(image, centre(1), ahiBottom), QColor(Qt::white));
    QCOMPARE(pixel(image, centre(1), hoursBottom), QColor(Qt::white));

    // hours at or above the compliance hours in blue, below in orange
    QCOMPARE(pixel(image, centre(0), hoursBottom), hoursColor());
    QCOMPARE(pixel(image, centre(2), hoursBottom), NightSummaryView::levelColor(NightSummary::Attention));

    // a grey line where the settings changed, on the left edge of the fourth night
    const double x = l.left + 3 * l.column;
    bool line = false;
    for (int dx = -1; dx <= 1 && !line; ++dx) {
        for (int y = l.ahi.top(); y <= l.hours.bottom() && !line; ++y) {
            line = pixel(image, x + dx, y) == changeColor();
        }
    }
    QVERIFY(line);

    // nothing to draw: a blank image
    const QImage empty = chart(DoctorReport(), size);
    QCOMPARE(empty.size(), size);
    QCOMPARE(QColor(empty.pixel(400, 160)), QColor(Qt::white));
}

void DoctorReportTests::testChartLongPeriod()
{
    // a year still gets a bar for every night
    DoctorReport r;
    r.from = QDate(2025, 10, 1);
    r.to = r.from.addDays(364);
    for (int i = 0; i < 365; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = 7;
        n.ahi = 2;
        r.nightList << n;
    }
    const QSize size(2400, 720);
    const QImage image = chart(r, size);
    const ChartLayout l = chartLayout(size, 365);
    QVERIFY(l.column > 1);
    for (int night : { 0, 100, 364 }) {
        const double x = l.left + (night + 0.5) * l.column;
        QCOMPARE(pixel(image, x, l.ahi.top() + l.ahi.height() - 3), NightSummaryView::levelColor(NightSummary::Good));
    }
}
```

- [ ] **Step 3: Run them to verify they fail**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project/build-test && Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q" >/dev/null 2>&1; make -j10 2>&1 | grep -E "error:" | head -3; ./test > run.log 2>&1; grep -E "^(PASS|FAIL!).*DoctorReportTests" run.log
```
Expected: builds; `FAIL!` for `testLevels`, `testChartLayout`, `testChart`, `testChartLongPeriod`.

- [ ] **Step 4: Implement**

Replace `oscar/doctorreport.cpp` with:

```cpp
/* Doctor Report
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreport.h"

#include <QCoreApplication>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <cmath>

namespace {

bool known(double value) { return !std::isnan(value); }

NightSummary::Level below(double value, double target)
{
    if (!known(value)) return NightSummary::Unknown;
    return value < target ? NightSummary::Good : NightSummary::Attention;
}

} // namespace

NightSummary::Level DoctorReport::usageLevel() const
{
    if (!known(meanHours)) return NightSummary::Unknown;
    return meanHours >= complianceHours ? NightSummary::Good : NightSummary::Attention;
}

NightSummary::Level DoctorReport::ahiLevel() const { return below(deviceAhi, NightSummary::kAhiTarget); }

NightSummary::Level DoctorReport::leakLevel() const
{
    if (leakRedline <= 0) return NightSummary::Unknown;
    return below(leak, leakRedline);
}

NightSummary::Level DoctorReport::odiLevel() const { return below(odi3, NightSummary::kOdiTarget); }
NightSummary::Level DoctorReport::below90Level() const { return below(below90, NightSummary::kT90Target); }

namespace DoctorReportPage {

ChartLayout chartLayout(const QSize &size, int nights)
{
    // laid out for a chart 320 px high and scaled from there
    const double s = size.height() / 320.0;
    const int left = qRound(64 * s), right = qRound(8 * s), top = qRound(10 * s);
    const int bottom = qRound(30 * s), gap = qRound(16 * s);
    const int width = qMax(1, size.width() - left - right);
    const int plot = qMax(2, size.height() - top - bottom - gap);
    const int ahiHeight = plot * 55 / 100;

    ChartLayout l;
    l.ahi = QRect(left, top, width, ahiHeight);
    l.hours = QRect(left, top + ahiHeight + gap, width, plot - ahiHeight);
    l.left = left;
    l.column = nights > 0 ? double(width) / nights : 0;
    return l;
}

QColor hoursColor() { return QColor(0x3a, 0x78, 0xc3); }
QColor changeColor() { return QColor(0x90, 0x90, 0x90); }

QImage chart(const DoctorReport &r, const QSize &size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    const int n = r.nightList.size();
    if (n == 0 || size.isEmpty()) return image;

    const ChartLayout l = chartLayout(size, n);
    const double s = size.height() / 320.0;

    double maxAhi = NightSummary::kAhiTarget * 2;
    double maxHours = qMax(r.complianceHours * 2, 8.0);
    for (const DoctorReport::Night &night : r.nightList) {
        if (known(night.ahi)) maxAhi = qMax(maxAhi, night.ahi * 1.1);
        if (known(night.hours)) maxHours = qMax(maxHours, night.hours * 1.1);
    }

    QPainter p(&image);
    QFont font = p.font();
    font.setPixelSize(qMax(8, qRound(13 * s)));
    p.setFont(font);

    auto height = [](const QRect &area, double value, double max) {
        return qMin(1.0, value / max) * (area.height() - 1);
    };
    auto bar = [&](const QRect &area, int i, double value, double max) {
        const double w = qMax(1.0, l.column * 0.7);
        const double h = height(area, value, max);
        return QRectF(l.left + i * l.column + (l.column - w) / 2, area.top() + area.height() - h, w, h);
    };

    // frames
    p.setPen(QColor(0xd0, 0xd0, 0xd0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(l.ahi.adjusted(0, 0, -1, -1));
    p.drawRect(l.hours.adjusted(0, 0, -1, -1));

    // bars
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const DoctorReport::Night &night = r.nightList[i];
        if (known(night.ahi) && night.ahi > 0) {
            p.setBrush(NightSummaryView::levelColor(night.ahi < NightSummary::kAhiTarget ? NightSummary::Good
                                                                                        : NightSummary::Attention));
            p.drawRect(bar(l.ahi, i, night.ahi, maxAhi));
        }
        if (known(night.hours) && night.hours > 0) {
            p.setBrush(night.hours >= r.complianceHours ? hoursColor()
                                                        : NightSummaryView::levelColor(NightSummary::Attention));
            p.drawRect(bar(l.hours, i, night.hours, maxHours));
        }
    }

    // targets
    QPen target(QColor(0x60, 0x60, 0x60));
    target.setStyle(Qt::DashLine);
    target.setWidthF(qMax(1.0, s));
    p.setPen(target);
    const double ahiY = l.ahi.top() + l.ahi.height() - height(l.ahi, NightSummary::kAhiTarget, maxAhi);
    const double hoursY = l.hours.top() + l.hours.height() - height(l.hours, r.complianceHours, maxHours);
    p.drawLine(QPointF(l.ahi.left(), ahiY), QPointF(l.ahi.right(), ahiY));
    p.drawLine(QPointF(l.hours.left(), hoursY), QPointF(l.hours.right(), hoursY));

    // settings changes, on the left edge of the night they started
    QPen change(changeColor());
    change.setStyle(Qt::DashLine);
    change.setWidthF(qMax(1.0, 1.5 * s));
    p.setPen(change);
    for (const QDate &date : r.settingsChanges) {
        const qint64 i = r.from.daysTo(date);
        if (i <= 0 || i >= n) continue;
        const double x = l.left + i * l.column;
        p.drawLine(QPointF(x, l.ahi.top()), QPointF(x, l.hours.bottom()));
    }

    // scales and dates
    p.setPen(QColor(0x40, 0x40, 0x40));
    const QLocale locale;
    const int labelRight = l.left - qRound(6 * s);
    const QRect ahiLabels(0, l.ahi.top(), labelRight, l.ahi.height());
    const QRect hoursLabels(0, l.hours.top(), labelRight, l.hours.height());
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignTop, locale.toString(maxAhi, 'f', 0));
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignVCenter, r.ahiName);
    p.drawText(ahiLabels, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0"));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignTop, locale.toString(maxHours, 'f', 0));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignVCenter, DoctorReport::tr("h"));
    p.drawText(hoursLabels, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0"));
    const int every = qMax(1, int(std::ceil(70 * s / l.column)));
    for (int i = 0; i < n; i += every) {
        const double x = l.left + (i + 0.5) * l.column;
        p.drawText(QRectF(x - 40 * s, l.hours.bottom() + 4 * s, 80 * s, 22 * s), Qt::AlignHCenter | Qt::AlignTop,
                   r.nightList[i].date.toString(QStringLiteral("dd.MM")));
    }
    return image;
}

} // namespace DoctorReportPage
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh DoctorReportTests`
Expected: `DoctorReportTests: 6 PASS`, `0 unexpected`.

- [ ] **Step 6: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/oscar.pro oscar/doctorreport.h oscar/doctorreport.cpp oscar/tests/doctorreporttests.h oscar/tests/doctorreporttests.cpp
git commit -m "Doctor report: the page's data, target levels and the night-by-night chart

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The page (`html()`) and the PDF (`writePdf()`)

**Files:**
- Modify: `oscar/doctorreport.h`, `oscar/doctorreport.cpp`
- Modify: `oscar/tests/doctorreporttests.h`, `oscar/tests/doctorreporttests.cpp`
- Modify: `Translations/Russkiy.ru.ts`

**Interfaces:**
- Consumes: Task 2's `DoctorReport`, levels, `chart()`; `SettingsComparison::html()/Options/kNoData`; `AnalysisPanel::disclaimer()` (`analysispanel.h`); `getVersion().displayString()` (`version.h`); `NightSummaryView::levelColor()`.
- Produces:
  - `QString DoctorReportPage::html(const DoctorReport &report, const QString &chartUrl, const QSizeF &chartSize);`
  - `bool DoctorReportPage::writePdf(const DoctorReport &report, const QString &path, QString *error = nullptr);`

- [ ] **Step 1: Declare the API and stub it**

`oscar/doctorreport.h`, before `} // namespace DoctorReportPage`:

```cpp
//! The whole page; the chart is the image at \a chartUrl, shown \a chartSize big.
QString html(const DoctorReport &report, const QString &chartUrl, const QSizeF &chartSize);
//! Writes the page as an A4 PDF at \a path; false, with \a error set, when that failed.
bool writePdf(const DoctorReport &report, const QString &path, QString *error = nullptr);
```

`oscar/doctorreport.cpp`, inside `namespace DoctorReportPage` at the end:

```cpp
QString html(const DoctorReport &, const QString &, const QSizeF &) { return QString(); }
bool writePdf(const DoctorReport &, const QString &, QString *) { return false; }
```

- [ ] **Step 2: Write the failing tests**

`oscar/tests/doctorreporttests.h`: add after `void testChartLongPeriod();`:

```cpp
    void testHtmlHeader();
    void testHtmlWithoutPersonalData();
    void testHtmlWithoutOximetry();
    void testHtmlWithoutAnalysis();
    void testHtmlRdi();
    void testHtmlSigns();
    void testHtmlEscapes();
    void testWritePdf();
    void testWritePdfFailsOnBadPath();
```

`oscar/tests/doctorreporttests.cpp`: add `#include <QFile>`, `#include <QLocale>`, `#include <QRegularExpression>`, `#include <QTemporaryDir>` to the includes, add to the anonymous namespace:

```cpp
QString reportText(const char *source) { return QCoreApplication::translate("DoctorReport", source); }

// A full report: personal data, oximetry, analysis, current settings and a comparison.
DoctorReport fullReport()
{
    DoctorReport r = sampleReport();
    r.patient = QStringLiteral("Иван Петров");
    r.birthDate = QDate(1954, 1, 1);
    r.cpapDevices << QStringLiteral("Löwenstein Prisma 20A");
    r.oximeter = QStringLiteral("Contec CMS50FW");
    r.oximetryNights = 3;
    r.currentSettings = QStringLiteral("APAP · Min 7 Max 10 · SoftPAP: 1");
    r.settingsSince = QDate(2026, 8, 20);
    r.meanHours = 6.1;
    r.compliantNights = 3;
    r.deviceAhi = 4.3;
    r.analysisAhi = 5.2;
    r.flowLimitation = 11;
    r.leak = 3.1;
    r.leakUnits = QStringLiteral("L/min");
    r.leakRedline = 24;
    r.pressure = 9.4;
    r.pressureUnits = QStringLiteral("cmH2O");
    r.odi3 = 7.5;
    r.below90 = 0.4;
    SettingsComparison::Group g;
    g.mode = QStringLiteral("APAP");
    g.pressure = QStringLiteral("Min 7 Max 10");
    g.dates << QDate(2026, 9, 4) << QDate(2026, 9, 5);
    g.hours = 14;
    g.events = 42;
    r.comparison << SettingsComparison::row(g);
    return r;
}

const QSizeF kChart(500, 150);
```

and append:

```cpp
void DoctorReportTests::testHtmlHeader()
{
    const DoctorReport r = fullReport();
    const QLocale locale;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(reportText("CPAP Therapy Report")));
    QVERIFY(html.contains(reportText("%1, born %2").arg(r.patient, locale.toString(r.birthDate, QLocale::ShortFormat))));
    QVERIFY(html.contains(QStringLiteral("Löwenstein Prisma 20A")));
    QVERIFY(html.contains(QStringLiteral("Contec CMS50FW")));
    QVERIFY(html.contains(reportText("%1 — since %2").arg(r.currentSettings, locale.toString(r.settingsSince, QLocale::ShortFormat))
                              .toHtmlEscaped()));
    QVERIFY(html.contains(reportText("%1 – %2 · nights with data %3 of %4 · with an oximeter %5")
                              .arg(locale.toString(r.from, QLocale::ShortFormat), locale.toString(r.to, QLocale::ShortFormat))
                              .arg(4).arg(5).arg(3)));
    QVERIFY(html.contains(QStringLiteral("<img src='chart.png' width=500 height=150>")));
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "Device Settings Compared")));
}

void DoctorReportTests::testHtmlWithoutPersonalData()
{
    DoctorReport r = fullReport();
    r.patient.clear();
    r.birthDate = QDate();
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(!html.contains(reportText("Patient:")));
    QVERIFY(!html.contains(QLocale().toString(QDate(1954, 1, 1), QLocale::ShortFormat)));
}

void DoctorReportTests::testHtmlWithoutOximetry()
{
    DoctorReport r = fullReport();
    r.oximeter.clear();
    r.oximetryNights = 0;
    r.odi3 = DoctorReport::kNoValue;
    r.below90 = DoctorReport::kNoValue;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(!html.contains(reportText("Oximeter:")));
    QVERIFY(html.contains(QStringLiteral("<b>%1</b>").arg(SettingsComparison::kNoData)));
}

void DoctorReportTests::testHtmlWithoutAnalysis()
{
    DoctorReport r = fullReport();
    r.analysisAhi = DoctorReport::kNoValue;
    r.flowLimitation = DoctorReport::kNoValue;
    r.odi3 = DoctorReport::kNoValue;
    r.below90 = DoctorReport::kNoValue;
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(reportText("OSCAR's analysis: %1 · flow limitation %2%")
                              .arg(SettingsComparison::kNoData, SettingsComparison::kNoData).toHtmlEscaped()));
    QVERIFY(!html.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
}

void DoctorReportTests::testHtmlRdi()
{
    DoctorReport r = fullReport();
    r.ahiName = QStringLiteral("RDI");
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(QStringLiteral("<font color='#606060'>RDI</font>")));                  // the tile
    QVERIFY(html.contains(QCoreApplication::translate("SettingsComparison", "Device %1").arg(QStringLiteral("RDI"))));
}

void DoctorReportTests::testHtmlSigns()
{
    DoctorReport r = fullReport();   // usage, AHI, leak and SpO2 fine; ODI 7.5 is not
    QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QCOMPARE(html.count(QStringLiteral("<b>✓</b>")), 4);
    QCOMPARE(html.count(QStringLiteral("<b>!</b>")), 1);

    r.odi3 = 2;
    html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QCOMPARE(html.count(QStringLiteral("<b>!</b>")), 0);
}

void DoctorReportTests::testHtmlEscapes()
{
    DoctorReport r = fullReport();
    r.currentSettings = QStringLiteral("Min <4 & Max 7");
    r.patient = QStringLiteral("A <b>B</b>");
    const QString html = DoctorReportPage::html(r, QStringLiteral("chart.png"), kChart);
    QVERIFY(html.contains(QStringLiteral("Min &lt;4 &amp; Max 7")));
    QVERIFY(html.contains(QStringLiteral("A &lt;b&gt;B&lt;/b&gt;")));
    QVERIFY(!html.contains(QStringLiteral("Min <4")));
}

void DoctorReportTests::testWritePdf()
{
    // 12 nights and 11 settings rows: still one page
    DoctorReport r = fullReport();
    r.to = r.from.addDays(11);
    r.nightList.clear();
    for (int i = 0; i < 12; ++i) {
        DoctorReport::Night n;
        n.date = r.from.addDays(i);
        n.hours = 6 + (i % 3);
        n.ahi = 2 + i % 5;
        r.nightList << n;
    }
    r.comparison.clear();
    for (int i = 0; i < 11; ++i) {
        SettingsComparison::Group g;
        g.mode = QStringLiteral("APAP (dyn)");
        g.pressure = QStringLiteral("Min %1 Max %2 (cmH2O)").arg(4 + i).arg(10 + i);
        g.relief = QStringLiteral("SoftPAP: 1 - Slight");
        g.dates << r.from.addDays(i);
        g.hours = 7;
        g.events = 20;
        r.comparison << SettingsComparison::row(g);
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.pdf"));
    QString error;
    QVERIFY2(DoctorReportPage::writePdf(r, path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray pdf = file.readAll();
    QVERIFY(pdf.startsWith("%PDF"));
    const int pages = int(QString::fromLatin1(pdf).count(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))));
    QCOMPARE(pages, 1);
}

void DoctorReportTests::testWritePdfFailsOnBadPath()
{
    QString error;
    QVERIFY(!DoctorReportPage::writePdf(fullReport(), QStringLiteral("/nonexistent-folder-oscar/report.pdf"), &error));
    QVERIFY(!error.isEmpty());
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:" | head -3; ./test > run.log 2>&1; grep -E "^FAIL!.*DoctorReportTests" run.log | cut -c1-120`
Expected: `FAIL!` for the nine new slots; Task 2's four still pass.

- [ ] **Step 4: Implement**

`oscar/doctorreport.cpp`: add to the includes

```cpp
#include <QDateTime>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTextDocument>
#include <QUrl>

#include "analysispanel.h"
#include "version.h"
```

add to the file's top anonymous namespace:

```cpp
// A tile: a coloured stripe, the caption, the figure with its sign and a short note.
QString tile(const QString &caption, const QString &value, const QString &note, NightSummary::Level level)
{
    const QString color = NightSummaryView::levelColor(level).name();
    QString sign;
    if (level == NightSummary::Good) sign = QStringLiteral(" <font color='%1'><b>✓</b></font>").arg(color);
    if (level == NightSummary::Attention) sign = QStringLiteral(" <font color='%1'><b>!</b></font>").arg(color);
    return QStringLiteral("<table width='100%' cellspacing=0 cellpadding=3><tr><td width=4 bgcolor='%1'></td>"
                          "<td><font color='#606060'>%2</font><br><font size='+2'><b>%3</b></font>%4"
                          "<br><font size='-1' color='#606060'>%5</font></td></tr></table>")
        .arg(color, caption.toHtmlEscaped(), value.toHtmlEscaped(), sign, note.toHtmlEscaped());
}
```

and replace the two stubs inside `namespace DoctorReportPage` with:

```cpp
QString html(const DoctorReport &r, const QString &chartUrl, const QSizeF &chartSize)
{
    const QLocale locale;
    const QString none = SettingsComparison::kNoData;
    auto number = [&](double value, int decimals) { return known(value) ? locale.toString(value, 'f', decimals) : none; };
    auto withUnits = [&](double value, int decimals, const QString &units) {
        return known(value) ? QStringLiteral("%1 %2").arg(locale.toString(value, 'f', decimals), units).trimmed() : none;
    };
    auto date = [&](const QDate &d) { return locale.toString(d, QLocale::ShortFormat); };

    QString html = QStringLiteral("<p><font size='+3'><b>%1</b></font></p>").arg(DoctorReport::tr("CPAP Therapy Report").toHtmlEscaped());

    // who, on what, when
    html += QStringLiteral("<table cellspacing=0 cellpadding=1>");
    auto line = [&](const QString &label, const QString &text) {
        html += QStringLiteral("<tr><td><font color='#606060'>%1</font>&nbsp;&nbsp;</td><td>%2</td></tr>")
                    .arg(label.toHtmlEscaped(), text.toHtmlEscaped());
    };
    if (!r.patient.isEmpty()) {
        line(DoctorReport::tr("Patient:"), r.birthDate.isValid() ? DoctorReport::tr("%1, born %2").arg(r.patient, date(r.birthDate)) : r.patient);
    }
    if (!r.cpapDevices.isEmpty()) line(DoctorReport::tr("Device:"), r.cpapDevices.join(QStringLiteral(", ")));
    if (!r.oximeter.isEmpty()) line(DoctorReport::tr("Oximeter:"), r.oximeter);
    line(DoctorReport::tr("Period:"), DoctorReport::tr("%1 – %2 · nights with data %3 of %4 · with an oximeter %5")
                            .arg(date(r.from), date(r.to)).arg(r.nights).arg(r.days()).arg(r.oximetryNights));
    html += QStringLiteral("</table>");
    if (!r.currentSettings.isEmpty()) {
        html += QStringLiteral("<table width='100%' border=1 cellspacing=0 cellpadding=4><tr><td><b>%1</b> %2</td></tr></table>")
                    .arg(DoctorReport::tr("Current settings:").toHtmlEscaped(),
                         DoctorReport::tr("%1 — since %2").arg(r.currentSettings, date(r.settingsSince)).toHtmlEscaped());
    }

    // the six tiles
    QString usage = none;
    if (known(r.meanHours)) {
        const int minutes = qRound(r.meanHours * 60);
        usage = DoctorReport::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
    }
    const QStringList tiles {
        tile(DoctorReport::tr("Usage"), usage,
             DoctorReport::tr("%1 of %2 nights ≥ %3 h").arg(r.compliantNights).arg(r.days()).arg(locale.toString(r.complianceHours)),
             r.usageLevel()),
        tile(r.ahiName, number(r.deviceAhi, 1),
             DoctorReport::tr("OSCAR's analysis: %1 · flow limitation %2%").arg(number(r.analysisAhi, 1), number(r.flowLimitation, 0)),
             r.ahiLevel()),
        tile(DoctorReport::tr("Leak"), withUnits(r.leak, 1, r.leakUnits),
             r.leakRedline > 0 ? DoctorReport::tr("red line %1").arg(locale.toString(r.leakRedline)) : DoctorReport::tr("no red line set"),
             r.leakLevel()),
        tile(DoctorReport::tr("Pressure %1%").arg(locale.toString(r.percentile)), withUnits(r.pressure, 1, r.pressureUnits),
             DoctorReport::tr("average over the nights"), NightSummary::Unknown),
        tile(DoctorReport::tr("ODI 3%"), known(r.odi3) ? DoctorReport::tr("%1 an hour").arg(number(r.odi3, 1)) : none,
             DoctorReport::tr("nights with an oximeter: %1").arg(r.oximetryNights), r.odiLevel()),
        tile(DoctorReport::tr("SpO2 below 90%"), known(r.below90) ? QStringLiteral("%1 %").arg(number(r.below90, 1)) : none,
             DoctorReport::tr("nights with an oximeter: %1").arg(r.oximetryNights), r.below90Level()),
    };
    html += QStringLiteral("<p><b>%1</b></p><table width='100%' cellspacing=4 cellpadding=0><tr>")
                .arg(DoctorReport::tr("Summary for the period").toHtmlEscaped());
    for (int i = 0; i < tiles.size(); ++i) {
        if (i == 3) html += QStringLiteral("</tr><tr>");
        html += QStringLiteral("<td width='33%'>%1</td>").arg(tiles[i]);
    }
    html += QStringLiteral("</tr></table>");

    // night by night
    html += QStringLiteral("<p><b>%1</b></p><img src='%2' width=%3 height=%4><br><font size='-1' color='#606060'>%5</font>")
                .arg(DoctorReport::tr("Night by night").toHtmlEscaped(), chartUrl)
                .arg(qRound(chartSize.width())).arg(qRound(chartSize.height()))
                .arg(DoctorReport::tr("AHI per night (dashed: %1) · hours of use (dashed: %2 h) · grey lines: settings changed")
                         .arg(locale.toString(NightSummary::kAhiTarget), locale.toString(r.complianceHours)).toHtmlEscaped());

    // the settings over the period
    if (!r.comparison.isEmpty()) {
        SettingsComparison::Options options;
        options.showDevice = r.showDevice;
        options.ahiName = r.ahiName;
        options.percentile = r.percentile;
        options.rowColors = { QStringLiteral("#ffffff"), QStringLiteral("#f2f2f2") };
        html += QStringLiteral("<br>") + SettingsComparison::html(r.comparison, options);
    }

    html += QStringLiteral("<p><font size='-1' color='#606060'><i>%1</i><br>%2</font></p>")
                .arg(AnalysisPanel::disclaimer().toHtmlEscaped(),
                     DoctorReport::tr("Prepared by OSCAR %1 on %2")
                         .arg(getVersion().displayString(), locale.toString(QDateTime::currentDateTime(), QLocale::ShortFormat))
                         .toHtmlEscaped());
    return html;
}

bool writePdf(const DoctorReport &report, const QString &path, QString *error)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);

    QTextDocument doc;
    const QSizeF page = printer.pageRect(QPrinter::Point).size();
    doc.setPageSize(page);
    doc.setDocumentMargin(0);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSizeF(8.5);
    doc.setDefaultFont(font);

    const QSize chartPixels(2400, 720);
    const QString url = QStringLiteral("doctorreport-chart.png");
    doc.addResource(QTextDocument::ImageResource, QUrl(url), chart(report, chartPixels));
    const QSizeF chartSize(page.width(), page.width() * chartPixels.height() / chartPixels.width());
    doc.setHtml(html(report, url, chartSize));
    doc.print(&printer);

    const QFileInfo written(path);
    if (printer.printerState() == QPrinter::Error || !written.exists() || written.size() == 0) {
        if (error) *error = DoctorReport::tr("Could not write %1.").arg(path);
        return false;
    }
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh DoctorReportTests`
Expected: `DoctorReportTests: 15 PASS`, `0 unexpected`. If `testWritePdf` reports 2 pages, lower the default font to 8 pt and re-run; ledger that as a ruling.

- [ ] **Step 6: Translate**

Run lupdate (Global Constraints) and set, in context `DoctorReport`:

| Source | Russian |
|---|---|
| `h` | `ч` |
| `CPAP Therapy Report` | `Отчёт о CPAP-терапии` |
| `Patient:` | `Пациент:` |
| `%1, born %2` | `%1, дата рождения %2` |
| `Device:` | `Аппарат:` |
| `Oximeter:` | `Оксиметр:` |
| `Period:` | `Период:` |
| `%1 – %2 · nights with data %3 of %4 · with an oximeter %5` | `%1 – %2 · ночей с данными %3 из %4 · с оксиметром %5` |
| `Current settings:` | `Текущие настройки:` |
| `%1 — since %2` | `%1 — с %2` |
| `Summary for the period` | `Итог за период` |
| `Usage` | `Использование` |
| `%1 h %2 min` | `%1 ч %2 мин` |
| `%1 of %2 nights ≥ %3 h` | `%1 из %2 ночей ≥ %3 ч` |
| `OSCAR&apos;s analysis: %1 · flow limitation %2%` | `анализ OSCAR: %1 · огр. потока %2 %` |
| `Leak` | `Утечка` |
| `red line %1` | `красная линия %1` |
| `no red line set` | `красная линия не задана` |
| `Pressure %1%` | `Давление %1 %` |
| `average over the nights` | `среднее по ночам` |
| `ODI 3%` | `ODI 3 %` |
| `%1 an hour` | `%1 в час` |
| `nights with an oximeter: %1` | `ночей с оксиметром: %1` |
| `SpO2 below 90%` | `SpO2 ниже 90 %` |
| `Night by night` | `По ночам` |
| `AHI per night (dashed: %1) · hours of use (dashed: %2 h) · grey lines: settings changed` | `AHI за ночь (пунктир — %1) · часы использования (пунктир — %2 ч) · серые линии — смена настроек` |
| `Prepared by OSCAR %1 on %2` | `Отчёт подготовлен OSCAR %1, %2` |
| `Could not write %1.` | `Не удалось записать %1.` |

Check `unfinished` → 0, `validate_ts.py` → `TOTAL 0`.

- [ ] **Step 7: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/doctorreport.h oscar/doctorreport.cpp oscar/tests/doctorreporttests.h oscar/tests/doctorreporttests.cpp Translations/Russkiy.ru.ts
git commit -m "Doctor report: the page and the A4 PDF

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `Statistics::doctorReport(from, to)` — filling the page from the profile

**Files:**
- Modify: `oscar/statistics.h`, `oscar/statistics.cpp`
- Test: `oscar/tests/analysisintegrationtests.{h,cpp}`

**Interfaces:**
- Consumes: Task 1's `settingsComparisonRows()` and `SettingsComparison::settingsLabel()`; Task 2's `DoctorReport`; existing `updateRXChanges()`, `rxitems`, `analysisRows()`, `analysisFigureValue()`, `formatRelief()`.
- Produces: `DoctorReport Statistics::doctorReport(const QDate &from, const QDate &to);` (public).

- [ ] **Step 1: Write the failing tests**

`oscar/tests/analysisintegrationtests.h`: add after `void testSettingsComparisonRowsTrimmedToDates();`:

```cpp
    void testDoctorReportCountsCpapHoursOnly();
    void testDoctorReportSettingsSince();
```

`oscar/tests/analysisintegrationtests.cpp`, append:

```cpp
void AnalysisIntegrationTests::testDoctorReportCountsCpapHoursOnly()
{
    // A two-day report around one night on the CPAP with an oximeter worn longer.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    const qint64 oxiRow = insertRow(QStringLiteral("INSERT INTO machines (profile_id, machine_id, loader_name, machine_type, serial_number) "
                                                   "VALUES (?, 3004, 'TestOxi', ?, 'OX3')"), { m_profileId, int(MT_OXIMETER) });
    QVERIFY(oxiRow > 0);
    Machine cpap(p_profile, 63);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    Machine oxi(p_profile, 64);
    oxi.info.type = MT_OXIMETER;
    oxi.setDatabaseId(oxiRow);
    const QDate date = kNightDate.addDays(60);
    Day *day = new Day();
    day->setDate(date);
    day->addSession(hypopneaSession(&cpap, 92, m_machineRow));
    day->addSession(oximetrySession(&oxi, 93, oxiRow));
    const double cpapHours = day->hours(MT_CPAP);
    p_profile->daylist.insert(date, day);

    Statistics stats;
    const DoctorReport r = stats.doctorReport(date.addDays(-1), date);
    p_profile->daylist.remove(date);

    QCOMPARE(r.days(), 2);
    QCOMPARE(r.nights, 1);
    QCOMPARE(r.nightList.size(), 2);
    QVERIFY(std::isnan(r.nightList[0].hours));
    QCOMPARE(r.nightList[1].hours, cpapHours);
    QCOMPARE(r.meanHours, cpapHours);
    QCOMPARE(r.comparison.size(), 1);
    QCOMPARE(r.settingsSince, date);
    QVERIFY(r.settingsChanges.isEmpty());
    delete day;
}

void AnalysisIntegrationTests::testDoctorReportSettingsSince()
{
    // Settings unchanged since the night before the period: «since» names that night.
    QFile::remove(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
    Machine cpap(p_profile, 65);
    cpap.info.type = MT_CPAP;
    cpap.setDatabaseId(m_machineRow);
    const QDate first = kNightDate.addDays(70), second = first.addDays(1);
    Day *a = new Day();
    a->setDate(first);
    a->addSession(hypopneaSession(&cpap, 94, m_machineRow));
    Day *b = new Day();
    b->setDate(second);
    b->addSession(hypopneaSession(&cpap, 95, m_machineRow));
    p_profile->daylist.insert(first, a);
    p_profile->daylist.insert(second, b);

    Statistics stats;
    const DoctorReport r = stats.doctorReport(second, second);
    p_profile->daylist.remove(first);
    p_profile->daylist.remove(second);

    QCOMPARE(r.nights, 1);
    QCOMPARE(r.settingsSince, first);
    QVERIFY(r.settingsChanges.isEmpty());
    delete a;
    delete b;
}
```

Add `#include "doctorreport.h"` to the includes of `oscar/tests/analysisintegrationtests.cpp` (after `#include "statistics.h"`).

- [ ] **Step 2: Run them to verify they fail**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:" | head -3`
Expected: compile error — `no member named 'doctorReport' in 'Statistics'`.

- [ ] **Step 3: Implement**

`oscar/statistics.h`: add `#include "doctorreport.h"` next to `#include "settingscomparison.h"`, and in `public:` after `settingsComparisonRows(...)`:

```cpp
    //! Everything the report for the doctor shows for the nights in [from, to].
    DoctorReport doctorReport(const QDate &from, const QDate &to);
```

`oscar/statistics.cpp`, after `Statistics::settingsComparisonRows()`:

```cpp
DoctorReport Statistics::doctorReport(const QDate &from, const QDate &to)
{
    DoctorReport r;
    r.from = from;
    r.to = to;
    const bool rdi = p_profile->general->calculateRDI();
    r.ahiName = rdi ? STR_TR_RDI : STR_TR_AHI;
    r.complianceHours = p_profile->cpap->complianceHours();
    r.leakRedline = p_profile->cpap->leakRedline();
    r.leakUnits = schema::channel[CPAP_Leak].units();
    r.pressureUnits = schema::channel[CPAP_Pressure].units();
    r.percentile = p_profile->general->prefCalcPercentile();
    if (AppSetting->showPersonalData()) {
        r.patient = (p_profile->user->firstName() + QLatin1Char(' ') + p_profile->user->lastName()).trimmed();
        r.birthDate = p_profile->user->DOB();
    }

    auto label = [](Machine *m) {
        QString text = QStringList { m->brand(), m->model() }.join(QLatin1Char(' ')).trimmed();
        if (AppSetting->includeSerial() && !m->serial().isEmpty()) text += QStringLiteral(" (%1)").arg(m->serial());
        return text;
    };

    // night by night: CPAP hours only, events per the AHI or RDI setting
    double hours = 0, events = 0, leak = 0, leakHours = 0, pressure = 0, pressureHours = 0;
    QDate lastNight;
    for (QDate date = from; date.isValid() && date <= to; date = date.addDays(1)) {
        DoctorReport::Night night;
        night.date = date;
        Day *day = p_profile->GetDay(date);
        Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
        if (day && r.oximeter.isEmpty()) {
            if (Machine *oxi = day->machine(MT_OXIMETER)) r.oximeter = label(oxi);
        }
        const double h = cpap ? day->hours(MT_CPAP) : 0;
        if (h > 0) {
            const QString device = label(cpap);
            if (!device.isEmpty() && !r.cpapDevices.contains(device)) r.cpapDevices << device;
            const double e = day->count(AllAhiChannels) + (rdi ? day->count(CPAP_RERA) : 0);
            night.hours = h;
            night.ahi = e / h;
            ++r.nights;
            hours += h;
            events += e;
            if (h >= r.complianceHours) ++r.compliantNights;
            if (day->channelHasData(CPAP_Leak)) {
                leak += day->wavg(CPAP_Leak) * h;
                leakHours += h;
            }
            if (day->channelHasData(CPAP_Pressure)) {
                pressure += day->percentile(CPAP_Pressure, r.percentile / 100.0) * h;
                pressureHours += h;
            }
            lastNight = date;
        }
        r.nightList << night;
    }
    if (r.nights > 0) r.meanHours = hours / r.nights;
    if (hours > 0) r.deviceAhi = events / hours;
    if (leakHours > 0) r.leak = leak / leakHours;
    if (pressureHours > 0) r.pressure = pressure / pressureHours;

    // OSCAR's analysis
    const QList<AnalysisDailyData> analysis = analysisRows(from, to);
    for (const AnalysisDailyData &d : analysis) {
        if (d.hasOximetry) ++r.oximetryNights;
    }
    r.analysisAhi = analysisFigureValue(QStringLiteral("ahi"), analysis);
    r.flowLimitation = analysisFigureValue(QStringLiteral("fl"), analysis);
    r.odi3 = analysisFigureValue(QStringLiteral("odi3"), analysis);
    r.below90 = analysisFigureValue(QStringLiteral("below:90"), analysis);

    // the settings: compared over the period, changed on, and in use on the last night
    r.comparison = settingsComparisonRows(from, to, &r.showDevice);   // brings rxitems up to date
    for (const RXItem &rx : std::as_const(rxitems)) {
        if (rx.start > from && rx.start <= to) r.settingsChanges << rx.start;
        if (lastNight.isValid() && rx.dates.contains(lastNight)) {
            r.currentSettings = SettingsComparison::settingsLabel(rx.mode, rx.pressure, formatRelief(rx.relief));
            r.settingsSince = rx.start;
        }
    }
    return r;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && make -j10 2>&1 | grep -E "error:"; /Users/semyk/Downloads/Oscar_Project/tools/runtests.sh AnalysisIntegrationTests DoctorReportTests`
Expected: `AnalysisIntegrationTests: 26 PASS`, `DoctorReportTests: 15 PASS`, `0 unexpected`.

- [ ] **Step 5: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/statistics.h oscar/statistics.cpp oscar/tests/analysisintegrationtests.h oscar/tests/analysisintegrationtests.cpp
git commit -m "Statistics: the doctor report's figures for a span of dates

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The dialog, the menu item, the remembered date — and the check on real data

**Files:**
- Create: `oscar/doctorreportdialog.h`, `oscar/doctorreportdialog.cpp`
- Modify: `oscar/oscar.pro` (after `doctorreport.*`)
- Modify: `oscar/SleepLib/profiles.h` (`STR_US_DoctorReportFrom`, getter, setter, `initPref`)
- Modify: `oscar/mainwindow.ui`, `oscar/mainwindow.h`, `oscar/mainwindow.cpp`
- Modify: `oscar/tests/doctorreporttests.{h,cpp}`, `Translations/Russkiy.ru.ts`
- Modify (fork-only): `docs/superpowers/plans/2026-10-01-handoff.md`

**Interfaces:**
- Consumes: Task 3's `DoctorReportPage::writePdf()`; Task 4's `Statistics::doctorReport()`.
- Produces: `class DoctorReportDialog : public QDialog` with `static QDate defaultFrom(const QDate &saved, const QDate &first, const QDate &last);`; `UserSettings::doctorReportFrom()` / `setDoctorReportFrom(const QDate &)`; `MainWindow::on_actionDoctor_Report_triggered()`.

- [ ] **Step 1: Write the failing test**

`oscar/tests/doctorreporttests.h`: add `void testDefaultFrom();` after `void testWritePdfFailsOnBadPath();`.

`oscar/tests/doctorreporttests.cpp`: add `#include "doctorreportdialog.h"` and append:

```cpp
void DoctorReportTests::testDefaultFrom()
{
    const QDate first(2026, 2, 10), last(2026, 10, 2);
    QCOMPARE(DoctorReportDialog::defaultFrom(QDate(), first, last), QDate(2026, 9, 3));             // last 30 days
    QCOMPARE(DoctorReportDialog::defaultFrom(QDate(2026, 8, 1), first, last), QDate(2026, 8, 1));   // remembered
    QCOMPARE(DoctorReportDialog::defaultFrom(QDate(2026, 10, 5), first, last), QDate(2026, 9, 3));  // after the data
    QCOMPARE(DoctorReportDialog::defaultFrom(QDate(2025, 1, 1), first, last), QDate(2026, 9, 3));   // before the data
    QCOMPARE(DoctorReportDialog::defaultFrom(QDate(), last.addDays(-5), last), last.addDays(-5));   // shorter history
}
```

- [ ] **Step 2: Header, stub, registration — run to see it fail**

`oscar/doctorreportdialog.h`:

```cpp
/* Doctor Report Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORTDIALOG_H
#define DOCTORREPORTDIALOG_H

#include <QDate>
#include <QDialog>

class QDateEdit;
class QPushButton;

//! Picks the dates of the report for the doctor and saves it as a PDF.
class DoctorReportDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit DoctorReportDialog(QWidget *parent = nullptr);

    //! The first date to offer: \a saved when it lies within the data, otherwise the last
    //! 30 days up to \a last, never before \a first.
    static QDate defaultFrom(const QDate &saved, const QDate &first, const QDate &last);

  private slots:
    void save();
    void updateButtons();

  private:
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
    QPushButton *m_save = nullptr;
};

#endif // DOCTORREPORTDIALOG_H
```

`oscar/doctorreportdialog.cpp` (stub):

```cpp
/* Doctor Report Dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreportdialog.h"

DoctorReportDialog::DoctorReportDialog(QWidget *parent) : QDialog(parent) {}
QDate DoctorReportDialog::defaultFrom(const QDate &, const QDate &, const QDate &) { return QDate(); }
void DoctorReportDialog::save() {}
void DoctorReportDialog::updateButtons() {}
```

Register in `oscar/oscar.pro`: `doctorreportdialog.cpp \` after `doctorreport.cpp \`, `doctorreportdialog.h \` after `doctorreport.h \`.

Run: `cd /Users/semyk/Downloads/Oscar_Project/build-test && Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q" >/dev/null 2>&1; make -j10 2>&1 | grep -E "error:" | head -3; ./test > run.log 2>&1; grep -E "^(PASS|FAIL!).*testDefaultFrom" run.log`
Expected: `FAIL!  : DoctorReportTests::testDefaultFrom()`.

- [ ] **Step 3: Implement the dialog, the setting and the menu item**

`oscar/SleepLib/profiles.h`:
- after `const QString STR_US_LastOverviewPreset = "LastOverviewPreset";` add `const QString STR_US_DoctorReportFrom = "DoctorReportFrom";`
- in `UserSettings()` after `initPref(STR_US_LastOverviewPreset, QStringLiteral("all"));` add `initPref(STR_US_DoctorReportFrom, QDate());`
- after the `lastOverviewPreset()` getter add:

```cpp
    //! The first date of the last report for the doctor; invalid before the first one.
    QDate doctorReportFrom() const { return getPref(STR_US_DoctorReportFrom).toDate(); }
```

- after `setLastOverviewPreset(...)` add `void setDoctorReportFrom(const QDate &date) { setPref(STR_US_DoctorReportFrom, date); }`

Replace `oscar/doctorreportdialog.cpp` with:

```cpp
/* Doctor Report Dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreportdialog.h"

#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "SleepLib/profiles.h"
#include "doctorreport.h"
#include "statistics.h"

DoctorReportDialog::DoctorReportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Doctor Report"));
    const QDate first = p_profile->FirstDay();
    QDate last = p_profile->LastDay(MT_CPAP);
    if (!last.isValid()) last = p_profile->LastDay();

    m_from = new QDateEdit(this);
    m_to = new QDateEdit(this);
    for (QDateEdit *edit : { m_from, m_to }) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
        if (first.isValid() && last.isValid()) edit->setDateRange(first, last);
    }
    m_to->setDate(last);
    m_from->setDate(defaultFrom(p_profile->general->doctorReportFrom(), first, last));

    auto *form = new QFormLayout;
    form->addRow(tr("From"), m_from);
    form->addRow(tr("To"), m_to);

    auto *buttons = new QDialogButtonBox(this);
    m_save = buttons->addButton(tr("Save PDF..."), QDialogButtonBox::ActionRole);
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(m_save, &QPushButton::clicked, this, &DoctorReportDialog::save);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_from, &QDateEdit::dateChanged, this, &DoctorReportDialog::updateButtons);
    connect(m_to, &QDateEdit::dateChanged, this, &DoctorReportDialog::updateButtons);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    updateButtons();
}

QDate DoctorReportDialog::defaultFrom(const QDate &saved, const QDate &first, const QDate &last)
{
    if (saved.isValid() && (!first.isValid() || saved >= first) && saved <= last) return saved;
    QDate from = last.addDays(-29);
    if (first.isValid() && from < first) from = first;
    return from;
}

void DoctorReportDialog::updateButtons()
{
    m_save->setEnabled(m_from->date() <= m_to->date());
}

void DoctorReportDialog::save()
{
    const QDate from = m_from->date(), to = m_to->date();
    Statistics stats;
    const DoctorReport report = stats.doctorReport(from, to);
    if (report.nights == 0) {
        QMessageBox::information(this, windowTitle(), tr("There are no CPAP nights between these dates."));
        return;
    }

    const QString name = tr("CPAP report %1 %2–%3.pdf")
                             .arg(p_profile->user->userName(), from.toString(QStringLiteral("dd.MM")),
                                  to.toString(QStringLiteral("dd.MM.yyyy")));
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Doctor Report"), folder + QLatin1Char('/') + name,
                                                      tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;

    QString error;
    if (!DoctorReportPage::writePdf(report, path, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    p_profile->general->setDoctorReportFrom(from);

    QMessageBox done(QMessageBox::Information, windowTitle(), tr("The report is saved."), QMessageBox::Close, this);
    QPushButton *open = done.addButton(tr("Open"), QMessageBox::AcceptRole);
    done.exec();
    if (done.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    accept();
}
```

`oscar/mainwindow.ui`:
- after `<addaction name="actionPrint_Report"/>` add `<addaction name="actionDoctor_Report"/>`;
- after the `<action name="actionPrint_Report">…</action>` block add:

```xml
  <action name="actionDoctor_Report">
   <property name="text">
    <string>Doctor Report (PDF)...</string>
   </property>
  </action>
```

`oscar/mainwindow.h`: after `void on_actionPrint_Report_triggered();` add `void on_actionDoctor_Report_triggered();`.

`oscar/mainwindow.cpp`:
- add `#include "doctorreportdialog.h"` after `#include "statistics.h"`;
- after `ui->actionPrint_Report->setEnabled(open);` add `ui->actionDoctor_Report->setEnabled(open);`;
- after the body of `MainWindow::on_actionPrint_Report_triggered()` add:

```cpp
void MainWindow::on_actionDoctor_Report_triggered()
{
    if (!p_profile) return;
    DoctorReportDialog dialog(this);
    dialog.exec();
}
```

- [ ] **Step 4: Build all three and run the suite**

Run:
```bash
cd /Users/semyk/Downloads/Oscar_Project
for d in build build-test build-nobt; do (cd $d && Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q" >/dev/null 2>&1; make -j10 > ../$d.log 2>&1; echo "$d make $?"; grep -E "error:" ../$d.log | head -3); done
tools/runtests.sh DoctorReportTests AnalysisIntegrationTests SettingsComparisonTests
```
Expected: `make 0` three times; `DoctorReportTests: 16 PASS`; `0 unexpected`.

- [ ] **Step 5: Translate**

Run lupdate and set:

| Context | Source | Russian |
|---|---|---|
| `DoctorReportDialog` | `Doctor Report` | `Отчёт для врача` |
| `DoctorReportDialog` | `From` | `С` |
| `DoctorReportDialog` | `To` | `По` |
| `DoctorReportDialog` | `Save PDF...` | `Сохранить PDF…` |
| `DoctorReportDialog` | `Cancel` | `Отмена` |
| `DoctorReportDialog` | `There are no CPAP nights between these dates.` | `За эти даты нет ночей с данными CPAP.` |
| `DoctorReportDialog` | `CPAP report %1 %2–%3.pdf` | `Отчёт CPAP %1 %2–%3.pdf` |
| `DoctorReportDialog` | `Save Doctor Report` | `Сохранить отчёт для врача` |
| `DoctorReportDialog` | `PDF files (*.pdf)` | `Файлы PDF (*.pdf)` |
| `DoctorReportDialog` | `The report is saved.` | `Отчёт сохранён.` |
| `DoctorReportDialog` | `Open` | `Открыть` |
| `MainWindow` | `Doctor Report (PDF)...` | `Отчёт для врача (PDF)…` |

Then `unfinished` → 0, `validate_ts.py` → `TOTAL 0`; re-run qmake + make in `../build`.

- [ ] **Step 6: Check on the father's data**

Quit OSCAR, launch it on the dev folder, profile «Папа». Menu «Файл → Отчёт для врача (PDF)…» (via `app_menu`). Expected:
- the dialog offers «с 03.09.2026 по 02.10.2026» (or the last 30 days up to the last CPAP night);
- «Сохранить PDF…» proposes «Отчёт CPAP Папа 03.09–02.10.2026.pdf» in «Документы»; save it;
- «Отчёт сохранён» with «Открыть».

Then check the file from the shell:
```bash
f="$HOME/Documents/Отчёт CPAP Папа 03.09–02.10.2026.pdf"; head -c 4 "$f"; echo; grep -ac "/Type /Page$\|/Type /Page " "$f"; sips -s format png "$f" --out /private/tmp/claude-501/-Users-semyk-Downloads-Oscar-Project/6d6c21cf-dfbf-42f3-9733-0df1105dd5ab/scratchpad/doctor-report.png
```
Expected: `%PDF`; one page; the PNG (read it) shows the header with «Текущие настройки … — с 01.10.2026», six tiles, the chart with grey lines at the settings changes, the comparison table, the footer. Usage / AHI / leak figures agree with the «Настройки» mode over the same nights.

- [ ] **Step 7: Commit**

```bash
cd /Users/semyk/Downloads/Oscar_Project/oscar-sql
git add oscar/oscar.pro oscar/doctorreportdialog.h oscar/doctorreportdialog.cpp oscar/SleepLib/profiles.h oscar/mainwindow.ui oscar/mainwindow.h oscar/mainwindow.cpp oscar/tests/doctorreporttests.h oscar/tests/doctorreporttests.cpp Translations/Russkiy.ru.ts
git commit -m "Doctor report: File menu item and the dialog that saves the PDF

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Mark the handoff item done (fork-only)**

In `docs/superpowers/plans/2026-10-01-handoff.md`, section 4.2 item 4, replace the line `   - Одностраничный PDF для врача — ещё не сделан, следующий подпроект.` with a `[x]` line: the doctor report is done (commit range of Tasks 1–5, spec `specs/2026-10-03-doctor-report-design.md`), checked on «Папа» 03.09–02.10.2026. Then:

```bash
git add -f docs/superpowers/plans/2026-10-01-handoff.md
git commit -m "Handoff plan: doctor report done (fork-only, not for MR)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
