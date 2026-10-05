# Flow limitation time and Glasgow Index Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** OSCAR's own analysis reports, per night and per period, the minutes of flow limitation, the longest run, the share of limited breaths, and the Glasgow Index in two variants (original port of FlowLimits.js and an amplitude-relative adaptation), with nine components and graphs.

**Architecture:** A pure module `SleepLib/analysis/glasgow_index.*` computes per-session counts from the flow chunks (original) and from our `FlowResult::breaths` (adapted). Stage 1 (`session_analysis.cpp`) stores the counts in the session stamp and two per-breath channels; day scoring sums them into `DayResult`; `analysis_daily` gets six columns (schema 21). Statistics, settings comparison, doctor report, Daily panel/graph and Overview charts read them.

**Tech Stack:** Qt 6.11, C++17, qmake, QtTest (`oscar/tests`, `tools/runtests.sh`), Node.js (reference only, outside the repo).

**Spec:** `docs/superpowers/specs/2026-10-05-glasgow-index-design.md`

## Global Constraints

- Repo `/Users/semyk/Downloads/Oscar_Project/oscar-sql`, branch `master`; build dirs `../build`, `../build-test`, `../build-nobt` — all three must build (`-Werror`); re-run qmake after adding files: `Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q"`.
- Tests: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Class…]` → `0 unexpected`.
- `glasgow_index.cpp` header: "Copyright 2025 DaveSkvn (Glasgow Index, https://github.com/DaveSkvn/GlasgowIndex), GPL-3.0-or-later; port of FlowLimits.js and an adaptation for OSCAR". `FlowLimits.js` itself is never added to the repo.
- Component order everywhere: `Skew, Spike, FlatTop, TopHeavy, MultiPeak, NoPause, InspirRate, MultiBreath, AmpVar`; index = Σ of the 8 fractions without `TopHeavy`.
- Both indices only when the flow is ≥ 10 Hz (`FlowResult::flScored`); otherwise empty counts and a dash.
- Schema `CURRENT_SCHEMA_VERSION` 20 → 21; `kAnalysisAlgoVersion` 2 → 3.
- New channels: `AN_GlasgowIndex` 0x1A09, `AN_GlasgowAdapted` 0x1A0A, value ×100 stored with gain 0.01, stamped at breath start.
- Translations: Russian for every new string; `grep -c 'type="unfinished"' Translations/Russkiy.ru.ts` = 0; `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`; non-QObject code uses `QCoreApplication::translate`.
- Real data only via `--datadir /Users/semyk/Documents/OSCAR20_Data_dev`; no health data committed; one OSCAR instance at a time.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; fork-only docs with `git add -f`, own commit ending `(fork-only, not for MR)`.

## Review Focus

1. A session whose flow has several chunks (mask off/on) — the original concatenates them like `formDataArray`; the adapted uses our breaths; neither crashes on a chunk shorter than one window (Task 1 test `testShortChunks`).
2. A night with one session < 10 Hz and one ≥ 10 Hz — counts come only from the scored session, the figure is not a dash (Task 3 test `testMixedRateNight`).
3. Old database (schema 20) with existing `analysis_daily` rows — migration keeps them, new columns empty, figures show a dash until re-analysis (Task 4 test `testMigrationV21KeepsRows`).
4. Statistics period mixing nights with and without Glasgow counts — period index uses only nights that have counts; minutes average only over FL-scored nights (Task 5 test `testGlasgowPeriodSkipsEmptyNights`).
5. Doctor summary page with the longer FL line in Russian — still one page (Task 5 test `testDoctorReportStillOnePage` extended).

---

### Task 1: Original Glasgow Index (port of FlowLimits.js) with golden numbers

**Files:**
- Create: `oscar/SleepLib/analysis/glasgow_index.h`, `oscar/SleepLib/analysis/glasgow_index.cpp`
- Create (outside repo): `/Users/semyk/Downloads/Oscar_Project/reference/glasgow/golden.js`
- Modify: `oscar/oscar.pro` (sources/headers, test sources)
- Test: `oscar/tests/glasgowindextests.{h,cpp}` (class `GlasgowIndexTests`)

**Interfaces:**
- Produces:
```cpp
namespace analysis {
enum GlasgowComponent { GiSkew, GiSpike, GiFlatTop, GiTopHeavy, GiMultiPeak, GiNoPause,
                        GiInspirRate, GiMultiBreath, GiAmpVar, GiComponentCount };
struct GlasgowCounts {
    int breaths = 0;
    std::array<int, GiComponentCount> flagged{};
    bool isEmpty() const { return breaths == 0; }
    double fraction(GlasgowComponent c) const;   // flagged/breaths, NaN when empty
    double index() const;                        // Σ 8 fractions (no TopHeavy), NaN when empty
    GlasgowCounts &operator+=(const GlasgowCounts &o);
    QString toText() const;                      // "a,b,…" (9 numbers), "" when empty
    static GlasgowCounts fromText(int breaths, const QString &text);
};
struct GlasgowBreath { qint64 start = 0; std::array<bool, GiComponentCount> flags{}; bool counted = true; };
struct GlasgowResult { GlasgowCounts counts; QVector<GlasgowBreath> breaths; };
GlasgowResult glasgowOriginal(const QVector<FlowChunk> &chunks);
}
```

- [ ] **Step 1: Write the golden generator** `reference/glasgow/golden.js`: `require` nothing; read `FlowLimits.js` with `fs` and `eval` it (it is DOM-free up to `prepIndices`). Build the synthetic series below at 25 Hz, values rounded to 2 decimals, call `findMins(a); results={}; findInspirations(a, results); calcCycleBasedIndicators(a, results); inspirationAmplitude(a, results); const c = prepIndices(results);` and print one JSON line per series: `{name, breaths: results.inspirations.length, skew, spike, flatTop, topHeavy, multiPeak, noPause, inspirRate, multiBreath, ampVar, overall}` (fractions as `prepIndices` returns them). `findMins` stores into a global the other functions read — keep the call order of `FlowLimits.html:84-94`.

Synthetic breath (same formula in JS and C++, x = phase 0–1 within the part, all lengths in samples at 25 Hz):
- inspiration `I` samples of shape `s(x)·P`, expiration `E` samples of `-0.8·P·sin(πx)`, then `R` samples of 0.
- shapes: `parabola` s=4x(1−x); `flat` s=min(1, 1.6·4x(1−x)); `double` s=sin(πx)·(1−0.45·sin²(2πx))/0.80; `spike` s=sin(πx)^4.
- series (each 60 breaths): `normal` parabola P=30 I=40 E=50 R=10; `flat` flat P=30; `double` double P=30; `spike` spike P=30; `nopause` parabola R=0; `fast` parabola I=25 E=30 R=5; `varamp` parabola with P alternating 24/36; `weak` flat P=15; `mixed` = the eight series concatenated in that order.

Run: `node reference/glasgow/golden.js > reference/glasgow/golden.json && cat reference/glasgow/golden.json`
Expected: 9 lines of JSON, `normal.overall` < 0.3, `flat.flatTop` > 0.9.

- [ ] **Step 2: Write failing tests** in `GlasgowIndexTests`:
  - `testMatchesFlowLimitsJs()` — data-driven over the 9 series: build the series with the same formula (helper `synthSeries(name, fs=25)` in the test file), `glasgowOriginal({chunk})`; `QCOMPARE(counts.breaths, golden.breaths)` and each `fraction` and `index()` within 0.01 of the golden numbers pasted into the test as a table.
  - `testEachComponent()` — `flat` flags FlatTop on ≥ 90 %, `double` MultiPeak ≥ 90 %, `spike` Spike ≥ 90 %, `nopause` NoPause ≥ 90 %, `fast` InspirRate ≥ 90 %, `varamp` AmpVar ≥ 80 %; `normal` none of these ≥ 10 %.
  - `testSampleRateIndependent()` — `normal`, `flat`, `double` built at 20 and 50 Hz (lengths scaled by fs/25) give `index()` within 0.02 of 25 Hz.
  - `testShortChunks()` — a chunk of 10 samples and an empty chunk: no crash, `breaths == 0`, `index()` is NaN.
  - `testCountsText()` — `fromText(n, c.toText())` round-trips; `+=` adds breaths and flags.

Run: `cd ../build-test && make -j10 2>&1 | grep -E 'error' ; ../tools/runtests.sh GlasgowIndexTests`
Expected: FAIL (link error or all comparisons fail) before Step 3.

- [ ] **Step 3: Implement `glasgowOriginal`** in `glasgow_index.cpp` as a line-for-line port of `findMins`, `findInspirations`, `calcCycleBasedIndicators`, `inspirationAmplitude`, `prepIndices` (`FlowLimits.js:25-480`): concatenate chunk samples in order; every sample-count constant `n25` becomes `qRound(n25 * fs / 25.0)` with `fs = 1000 / rateMs` of the first chunk (`MIN_WINDOW` 25, min inspiration 8, short ≤ 12, `EXTRAPOLATION_SAMPLES` 25, pause 10); `inspirPerMin = 5·60000 / Δstart_ms`; keep: no-min inspirations never NoPause, first 5 and last inspiration without AmpVar but counted, `lastMax` updated only before the first decrease, TopHeavy out of `index()`. `GlasgowBreath::start` = chunk start + sample·rateMs.

- [ ] **Step 4: Run tests**
Run: `../tools/runtests.sh GlasgowIndexTests`
Expected: all pass.

- [ ] **Step 5: Commit** `"Analysis: Glasgow Index, port of FlowLimits.js (DaveSkvn, GPL-3.0)"`

### Task 2: Adapted Glasgow Index and the 5-minute series

**Files:**
- Modify: `oscar/SleepLib/analysis/glasgow_index.{h,cpp}`
- Test: `oscar/tests/glasgowindextests.{h,cpp}`

**Interfaces:**
- Consumes: `FlowChunk`, `Breath` (`flow_analyzer.h`), Task 1 types.
- Produces:
```cpp
GlasgowResult glasgowAdapted(const QVector<FlowChunk> &chunks, const QVector<Breath> &breaths,
                             const QVector<Span> &blocked);
//! Index over the breaths that started in (t-300 s, t], at every counted breath.
QVector<TimedValue> glasgowSeries(const QVector<GlasgowBreath> &breaths);   // TimedValue from day_scorer.h
```

- [ ] **Step 1: Failing tests**
  - `testAdaptedMatchesOriginalAt30()` — series `normal, flat, double, spike, nopause, fast, varamp` at P=30: build `Breath`s from the known synthetic boundaries (helper `synthBreaths`), `blocked` empty; adapted `index()` within 0.05 of original; each component fraction within 0.1.
  - `testAdaptedIgnoresWeakAmplitude()` — series `normal` at P=12 and `varamp` scaled ×0.4: original FlatTop or AmpVar fraction > 0.5, adapted < 0.2.
  - `testAdaptedSkipsBlocked()` — a `blocked` span over half the breaths halves `breaths`.
  - `testSeries()` — `glasgowSeries` of `mixed`: one value per counted breath, first value at the first breath; value during the `flat` block > value during `normal` block.

Run: `../tools/runtests.sh GlasgowIndexTests` → Expected: FAIL.

- [ ] **Step 2: Implement** per spec 2.3: inspiration samples = flow in `[start, inspEnd)` from the chunk holding it; P = max of those samples; Skew/Spike/TopHeavy as original on those samples (short ≤ 0.48 s); FlatTop `midVar/P² < 0.75/900`; MultiPeak step `0.033·P`; AmpVar over this and previous 4 counted breaths' P: `var/mean² > 4/900`; InspirRate `5·60000/(start[i]-start[i-5]) > 20`; NoPause: previous breath's expiratory minimum `m` at `tm` in `[inspEnd_prev, start)`, value 1 s later `y1`; if `y1 < 0`, `intersection = tm + 1000·m/(m−y1)` ms, pause = `start − intersection`, flag if < 400 ms; MultiBreath: previous breath's expiratory minimum > −0.10·median(|pef| of the 5 breaths before). A breath overlapping `blocked` is `counted = false` and not in `breaths`.

- [ ] **Step 3: Run tests** → all `GlasgowIndexTests` pass.
- [ ] **Step 4: Commit** `"Analysis: adapted Glasgow Index and its 5-minute series"`

### Task 3: Stage 1 and day scoring — counts, channels, FL breaths and longest run

**Files:**
- Modify: `oscar/SleepLib/analysis/flow_analyzer.{h,cpp}` (`FlowResult::flLimitedBreaths`, `glasgow`, `glasgowAdapted` + series), `session_analysis.{h,cpp}` (`SessionStamp`, `runFlow`), `analysis_channels.{h,cpp}` (two channels), `day_scorer.{h,cpp}` (`CpapSession`, `DayResult`), `day_analysis.cpp` (stamp → `CpapSession`), `analysis_params.h` (version 3)
- Test: `oscar/tests/flowanalyzertests.cpp`, `oscar/tests/dayscorertests.cpp`, `oscar/tests/analysisintegrationtests.cpp`

**Interfaces:**
- Consumes: Task 1–2 functions.
- Produces: `FlowResult { int flLimitedBreaths; GlasgowResult glasgow, glasgowAdapted; }`; `SessionStamp { int flLimitedBreaths; GlasgowCounts glasgow, glasgowAdapted; }` with JSON keys `flx`, `gi`, `gia` (`gi`/`gia` = array of 10 ints: breaths then 9 flags); `CpapSession` same three fields; `DayResult { int flLimitedBreaths = 0; int flLongestSeconds = 0; GlasgowCounts glasgow, glasgowAdapted; }`.

- [ ] **Step 1: Failing tests**
  - `FlowAnalyzerTests::testLimitedBreathCount` — synthetic flow with 3 consecutive limited breaths and 1 isolated limited breath (existing helpers): `flLimitedBreaths == 4`, one `flowLimitation` span.
  - `FlowAnalyzerTests::testGlasgowNeedsTenHz` — the same flow at 8 Hz: `glasgow.counts.isEmpty()` and `glasgowAdapted.counts.isEmpty()`; at 25 Hz both non-empty.
  - `DayScorerTests::testLongestFlRun` — `in.flowLimitation` spans of 30 s and 90 s → `flLongestSeconds == 90`, `flSeconds == 120`.
  - `DayScorerTests::testMixedRateNight` — two `CpapSession`s, one with empty counts (`flScored=false`), one with `glasgow.breaths=100`: day `glasgow.breaths == 100`, `flLimitedBreaths` summed.
  - `AnalysisIntegrationTests::testStampKeepsGlasgow` — `SessionStamp` with counts → `toJson` → `read` round-trips `flx`, `gi`, `gia`.

Run: `../tools/runtests.sh FlowAnalyzerTests DayScorerTests AnalysisIntegrationTests` → FAIL.

- [ ] **Step 2: Implement.** `analyzeFlow`: count `flLimitedBreaths` in the existing loop at `flow_analyzer.cpp:569` (`b.fl >= params.flThreshold`); when `flScored`, set `glasgow = glasgowOriginal(chunks)` and `glasgowAdapted = glasgowAdapted(chunks, breaths, blocked)`. `runFlow`: copy into the stamp; write `glasgowSeries(...)` of both into `AN_GlasgowIndex` / `AN_GlasgowAdapted` via `lists.get(code, false, 0.01f)->AddEvent(t, raw(v*100))`. Channels: `add(AN_GlasgowIndex, 0x1A09, WAVEFORM, MT_CPAP, "AnGlasgowIndex", tr("Glasgow Index (analysis)"), tr("Glasgow Index over the last 5 minutes: the share of breaths with each of 8 shape signs, summed (DaveSkvn's method)"), tr("GI"), QString(), QColor(0xb0,0x30,0x60))` and `AN_GlasgowAdapted` 0x1A0A "AnGlasgowAdapted", "Glasgow Index, adapted (analysis)", "…with thresholds relative to the breath's size", "GIa", `QColor(0x30,0x60,0xb0)`; add both to the stage-1 list at `analysis_channels.cpp:119`. Day: sum counts and `flLimitedBreaths`; `flLongestSeconds` = max span length of `in.flowLimitation` (after merging overlapping spans, as `spanSeconds` does). `kAnalysisAlgoVersion = 3` with comment `// 3: Glasgow Index, limited breath count`.

- [ ] **Step 3: Run** the three classes, then the whole suite → `0 unexpected`.
- [ ] **Step 4: Commit** `"Analysis: Glasgow counts per session and night; FL breath count and longest run"`

### Task 4: Storage — schema 21 and the daily row

**Files:**
- Modify: `oscar/database/database_schema.{h,cpp}` (`migrateV20ToV21`, create-table columns, steps table, version 21), `oscar/database/analysis_daily_repository.{h,cpp}`, `oscar/SleepLib/analysis/day_analysis.cpp` (`toDailyRow`)
- Test: `oscar/tests/analysisintegrationtests.cpp`

**Interfaces:**
- Produces: `AnalysisDailyData { int flLimitedBreaths = 0; int flLongestSeconds = 0; analysis::GlasgowCounts glasgow, glasgowAdapted; }`; columns `fl_limited_breaths INTEGER DEFAULT 0`, `fl_longest_s REAL DEFAULT 0`, `gi_breaths INTEGER DEFAULT 0`, `gi_counts TEXT DEFAULT ''`, `gia_breaths INTEGER DEFAULT 0`, `gia_counts TEXT DEFAULT ''`.

- [ ] **Step 1: Failing tests**
  - extend `testDailyRowRoundTrip` — set the new fields (counts with distinct numbers), save, read back equal.
  - `testMigrationV21KeepsRows` — create a schema-20 `analysis_daily` (as `testMigrationAddsAnalysisDaily` builds older schemas), insert one row, run migrations: row present, `flSeconds` kept, new fields 0/empty, `schema_version` = 21.
  - `testToDailyRowCarriesGlasgow` — `toDailyRow` of a `DayResult` with counts and FL fields copies them.

Run: `../tools/runtests.sh AnalysisIntegrationTests` → FAIL.

- [ ] **Step 2: Implement** the migration modelled on `migrateV19ToV20` (`database_schema.cpp:2064`: transaction, `ALTER TABLE analysis_daily ADD COLUMN …` ×6, set version), register `{ 20, &migrateV20ToV21 }`, add the columns to `createAnalysisDailyTable`, read/write them in the repository (`gi_counts` via `toText`/`fromText`).
- [ ] **Step 3: Run** suite → `0 unexpected`.
- [ ] **Step 4: Commit** `"Database: schema 21 keeps Glasgow counts and flow limitation runs per night"`

### Task 5: Figures — Statistics, settings comparison, doctor report

**Files:**
- Modify: `oscar/statistics.{h,cpp}`, `oscar/settingscomparison.{h,cpp}`, `oscar/doctorreport.{h,cpp}`
- Test: `oscar/tests/analysisintegrationtests.cpp`, `oscar/tests/settingscomparisontests.cpp`, `oscar/tests/doctorreporttests.cpp`

**Interfaces:**
- Consumes: Task 4 fields.
- Produces: `analysisFigureValue` keys `flmin` (Σ`flSeconds`/60 / nights with `flBreaths>0`), `fllong` (max `flLongestSeconds`/60, minutes), `flbr` (100·Σ`flLimitedBreaths`/Σ`flBreaths`), `gi`, `gia` (index of summed counts over nights with non-empty counts); all in `analysisGroup` "fl"; `SettingsComparison::Glasgow` column after `FlowLimitation`; `DoctorReport { double flowLimitationMinutes, flowLimitedBreaths, glasgow, glasgowAdapted; }` (`kNoValue` when absent).

- [ ] **Step 1: Failing tests**
  - `testGlasgowFigures` — two `AnalysisDailyData` rows: (flSeconds 600, flBreaths 1000, flLimitedBreaths 200, flLongestSeconds 120, glasgow 1000 breaths with flags {100,0,…}) and (flSeconds 1200, flBreaths 1000, flLimitedBreaths 400, flLongestSeconds 300, glasgow 1000 breaths {300,0,…}): `flmin` = 15, `fllong` = 5, `flbr` = 30, `gi` = 0.2.
  - `testGlasgowPeriodSkipsEmptyNights` — add a third row with `flBreaths 0` and empty counts: same results.
  - Statistics rows: `periodHtml` with one analysed night contains `tr("Glasgow Index")` and `tr("Flow limitation, min per night")`.
  - `SettingsComparisonTests` — the column exists, its header `tr("Glasgow Index")`, value from `gi`.
  - `DoctorReportTests::testDoctorReportStillOnePage` (extend the existing one-page test) — with all new values set in Russian locale, one page.

Run: `../tools/runtests.sh AnalysisIntegrationTests SettingsComparisonTests DoctorReportTests` → FAIL.

- [ ] **Step 2: Implement.** Labels (`statistics.cpp` label function): `flmin` "Flow limitation, min per night", `fllong` "Longest flow limitation run, min", `flbr` "Breaths with flow limitation, %", `gi` "Glasgow Index", `gia` "Glasgow Index (adapted)"; rows after `"fl"` in the row list (`statistics.cpp:1068`). Doctor report line (`doctorreport.cpp:233`): `"OSCAR's analysis: %1 · flow limitation %2% (%3 min per night, %4% of breaths) · Glasgow Index %5 / %6 (adapted)"`, dashes for missing values.
- [ ] **Step 3: Run** suite → `0 unexpected`.
- [ ] **Step 4: Commit** `"Statistics, settings comparison, doctor report: flow limitation time and Glasgow Index"`

### Task 6: Daily panel and graph, Overview charts

**Files:**
- Modify: `oscar/analysispanel.{h,cpp}`, `oscar/daily.cpp` (graph), `oscar/Graphs/gAnalysisCharts.{h,cpp}` (kinds), `oscar/overviewpresets.cpp` (via `gAnalysisChart::kinds()`), `oscar/overview.cpp` if charts are created there
- Test: `oscar/tests/analysispaneltests.cpp`, `oscar/tests/analysischarttests.cpp`, `oscar/tests/overviewpresetstests.cpp`

**Interfaces:**
- Consumes: `DayResult` fields (Task 3).
- Produces: `gAnalysisChart::Kind` values `Glasgow` (two lines: original, adapted) and `FlowLimitationMinutes` (bars); Daily graph name `STR_GRAPH_AnalysisGlasgow = "AnalysisGlasgow"` with layers `AN_GlasgowIndex`, `AN_GlasgowAdapted`.

- [ ] **Step 1: Failing tests**
  - `AnalysisPanelTests::testFlowLimitationLine` — `DayResult` flSeconds 6300, flowSeconds 27000, flLongestSeconds 720, flLimitedBreaths 310, flBreaths 1000 → sidebar contains "1 h 45 min", "12 min", "31".
  - `AnalysisPanelTests::testGlasgowRow` — counts set → sidebar contains the formatted original and adapted index and the 9 component names; Top Heavy row has "not in the sum".
  - `AnalysisPanelTests::testGlasgowDash` — `flScored=false` → dash and the 10 Hz hint.
  - `AnalysisChartTests::testGlasgowKind` / `testFlMinutesKind` — the chart's value for a day row equals `gi`/`gia` and `flSeconds/60`.
  - `OverviewPresetsTests` — Analysis preset names include the two new charts.

Run: `../tools/runtests.sh AnalysisPanelTests AnalysisChartTests OverviewPresetsTests` → FAIL.

- [ ] **Step 2: Implement.** Sidebar FL row text (third column): `"%1 — %2, longest %3; %4% of breaths"`; Glasgow row `"%1 (original) · %2 (adapted)"` with an expandable `<details>`-style table as the panel already does for other sections, tooltip: "Author's scale: 0–0.2 clean breathing, about 3 serious problems. Experimental, not reviewed by physicians; not a diagnosis." Component names: Skew, Spike, Flat top, Top heavy (not in the sum), Double peak, No pause, Inspiration rate, Double inspiration, Variable amplitude. Daily graph created next to `AN_FLScore` (`daily.cpp:347`, `566`) with two `gLineChart` layers.
- [ ] **Step 3: Run** suite → `0 unexpected`.
- [ ] **Step 4: Commit** `"Daily and Overview: flow limitation time and Glasgow Index"`

### Task 7: Translations, check on real data, handoff

**Files:**
- Modify: `Translations/Russkiy.ru.ts`, `oscar/translations/Russkiy.ru.qm` (built), `docs/superpowers/plans/2026-10-01-handoff.md` (fork-only)

- [ ] **Step 1:** `lupdate`, translate every unfinished entry (Russian terms: «Перекос», «Пик», «Плоская вершина», «Тяжёлая вершина (не входит в сумму)», «Двойной пик», «Нет паузы», «Частота вдохов», «Двойной вдох», «Изменчивость амплитуды», «Glasgow Index (адаптированный)», «Ограничение потока, мин за ночь», «Самый длинный участок ограничения, мин», «Вдохи с ограничением, %»), `lrelease … -qm oscar/translations/Russkiy.ru.qm`; checks of Global Constraints = 0.
- [ ] **Step 2: Reference check on a real ResMed night** if the dev data has one at 25 Hz: export its flow samples to a temp CSV (outside the repo), run `FlowLimits.js` on it via a variant of `golden.js`, compare with `glasgowOriginal` on the same samples: index within 0.02. If no ResMed night exists, ledger a ruling.
- [ ] **Step 3: App check** on profile «Папа»: OSCAR re-analyses on start (version 3); «День» for 04.10 — FL row with minutes and longest run, Glasgow row and table, Glasgow graph; «Сводка» Analysis set — two new charts; «Статистика» — five new rows; PDF report «Подробно» for 7 nights — doctor page still one page with the new line.
- [ ] **Step 4: Commit** `"Translations: flow limitation time and Glasgow Index"`; handoff section (fork-only commit).
