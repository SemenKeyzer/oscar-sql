# Device / OSCAR analysis / manual scoring apart; the «анализ» mark — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** Daily shows device, OSCAR analysis and manual scoring as separate groups (index table, events tree, Manual page), and every figure of OSCAR's analysis on every page carries the same «анализ» mark (pill + purple bar).

**Architecture:** A small `AnalysisMark` module (`oscar/analysismark.{h,cpp}`) owns the colours, the HTML pill, the printable pill, the row style, the heading row, the «… · analysis» title and the tree heading item; every page calls it. `Day` gains device-only counts so the index table shows the device's own figures. Daily's table and tree builders become static functions taking `analysisShown`, so tests drive them with a synthetic `Day`.

**Tech Stack:** Qt 6.11, qmake, C++17 `-Werror`, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-15-analysis-separation-design.md`

## Global Constraints

- Colours: bar `#7F77DD`; pill background `#EEEDFE`; pill text `#3C3489`.
- Pill word: source `tr("analysis")` → «анализ»; hover text: source `tr("OSCAR's second opinion, experimental")` → «Второе мнение OSCAR, экспериментально».
- Analysis titles: source `QObject::tr("%1 · analysis")` → «%1 · анализ» (one string, context `QObject`, used by channels, graphs and charts).
- Group headings: source `tr("OSCAR analysis")` → «Анализ OSCAR»; `tr("Manual scoring")` → «Ручная разметка» (existing strings where they exist).
- Hidden analysis (`analysis::analysisShown()` false): no analysis heading, rows, tree group or marks anywhere (the existing gates stay).
- Device rows in Daily's index table show the device's own figures (no manual edits); manual figures go to the «Ручной анализ» page.
- Every new string translated; `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → TOTAL 0; glossary coverage passes.
- Build `../build-test`, `../build`, `../build-nobt` and check each exit code; re-run qmake in all three after adding files; `../tools/runtests.sh` green (only the 3 known EventsTabTests failures). Launch the app only with `open -n ../build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа`.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A night with oximetry or position data only (no CPAP hours) — index table and tree build without dividing by zero and show no empty «Анализ OSCAR» heading.
2. Switching «Анализ OSCAR» on and off while a night is open — the table, the tree and the AHI frame lose / regain the analysis group at once (the switch already reloads Daily; check `applyAnalysisShown` reaches the tree and table).
3. Clicking an analysis row in the index table (`event=<code>` link) still opens the Events page on that channel's tree node now that analysis nodes sit under a heading and carry base names.
4. A disabled session — device counts skip it like `Day::count` does.
5. Printing Daily / Statistics in black and white — the printable mark stays readable (word in brackets), the bar may vanish.

Tests pinning these: 1 → Task 2 `testIndicesWithoutCpap`, Task 3 `testTreeWithoutCpap`; 2 → Task 3 `testTreeAnalysisHidden` + Task 3 Step 3 check; 3 → Task 3 Step 3 check of `Link_clicked`; 4 → Task 2 `testDeviceCountSkipsDisabled`; 5 → Task 1 `testPrintPill`.

---

### Task 1: The `AnalysisMark` module and analysis channel names

**Files:** Create `oscar/analysismark.{h,cpp}`; modify `oscar/SleepLib/analysis/analysis_channels.{h,cpp}`; add both to `oscar/oscar.pro`. Test: `oscar/tests/analysismarktests.{h,cpp}` (new class, `DECLARE_TEST`, `QApplication` in `initTestCase` like `TabButtonsTests`).

**Interfaces — Produces:**
- `namespace AnalysisMark` in `analysismark.h`:
  - `extern const char *const BarColor, *const PillBackground, *const PillText;` (the three colours)
  - `QString pill();` — `<span title='…'>` with background `#EEEDFE`, colour `#3C3489`, the word «analysis» padded with `&nbsp;`; the title attribute is the hover text.
  - `QString printPill();` — `[analysis]` in `#3C3489` (readable in black and white).
  - `QString rowStyle();` — inline CSS for a `<td>`: `border-left: 3px solid #7F77DD;`.
  - `QString headingRow(const QString &title, int colspan);` — `<tr><td colspan=N style='rowStyle'><b>title</b> pill()</td></tr>`.
  - `QString title(const QString &base);` — `QObject::tr("%1 · analysis").arg(base)`.
  - `QTreeWidgetItem *treeHeading(const QString &text, bool analysis);` — flags `Qt::ItemIsEnabled` only (not selectable), bold font, no `Qt::UserRole` data, `data(0, Qt::UserRole + 1) == "heading"`; for `analysis` also background `#EEEDFE`, foreground `#3C3489` and a 4×14 `#7F77DD` bar icon.
- `QString analysis::channelBaseName(ChannelID code);` (analysis_channels.h) — the channel's name without the analysis suffix (e.g. «Obstructive Apnea»); empty for a non-analysis channel.
- Analysis channels' `fullname()` becomes `AnalysisMark::title(base)` — implemented with the same `QObject::tr("%1 · analysis")` in analysis_channels.cpp (SleepLib must not include the widget module).

- [ ] **Step 1: Failing tests** (`AnalysisMarkTests`):
  - `testPill`: contains `analysis`, `#EEEDFE`, `#3C3489`, `title='OSCAR&apos;s second opinion, experimental'` (or the escaped form the code uses — assert the hover text is present).
  - `testPrintPill`: equals text containing `[analysis]`; contains no `background`.
  - `testHeadingRow`: `headingRow("OSCAR analysis", 2)` contains `colspan=2`, `#7F77DD`, `<b>OSCAR analysis</b>` and `pill()`.
  - `testTitle`: `title("AHI") == "AHI · analysis"`.
  - `testTreeHeading`: analysis heading — `!(flags() & Qt::ItemIsSelectable)`, `!data(0, Qt::UserRole).isValid()`, `background(0).color() == QColor("#EEEDFE")`, `!icon(0).isNull()`, font bold; manual heading (`analysis=false`) — not selectable, no icon.
  - `testChannelBaseName`: `channelBaseName(AN_ObstructiveApnea) == "Obstructive Apnea"`; `schema::channel[AN_ObstructiveApnea].fullname() == AnalysisMark::title("Obstructive Apnea")`; `channelBaseName(CPAP_Obstructive).isEmpty()`.
- [ ] **Step 2:** qmake in the three build dirs, build `../build-test` (exit 0 except the new tests' compile errors) → FAIL (missing module).
- [ ] **Step 3:** implement; rename every analysis channel in analysis_channels.cpp from `tr("X (analysis)")` to the base + `tr("%1 · analysis")`, keeping a base-name table for `channelBaseName`.
- [ ] **Step 4:** `../tools/runtests.sh AnalysisMarkTests` → green (fix tests elsewhere that pinned the old «(analysis)» names).
- [ ] **Step 5:** commit "AnalysisMark: one look for OSCAR's analysis".

### Task 2: Device figures and the Daily index table

**Files:** `oscar/SleepLib/day.{h,cpp}`, `oscar/daily.{h,cpp}`. Test: `oscar/tests/analysisintegrationtests.{h,cpp}` (has the profile, `Day` builders and `Session::setManualScoring`).

**Interfaces — Consumes:** Task 1 `AnalysisMark::headingRow`, `rowStyle`, `analysis::channelBaseName`. **Produces:**
- `EventDataType Day::deviceCount(ChannelID code);` — like `count()` (same `ahiChannelGroup` expansion, enabled sessions only) without `manualDelta()`.
- `double Day::devicePerHour(ChannelID code);` — `deviceCount(code) / hours(MT_CPAP)`, 0 without CPAP hours.
- `static QString Daily::indicesHtml(Day *day, bool analysisShown, QHash<ChannelID, EventDataType> &values);` — replaces the member `getIndices` (the caller passes `analysis::analysisShown()`): device rows first (CPAP flag rows by `devicePerHour`, spans and `CPAP_VSnore2` as now), then `AnalysisMark::headingRow(tr("OSCAR analysis"), 2)` and the analysis rows (their `<td>`s styled with `rowStyle()`, named by `channelBaseName`), only when `analysisShown` and the day has analysis rows; the Apple rows stay last in the device group as now.
- `static QString Daily::manualCountsHtml(Day *day);` — a two-column table «type: device per hour → after manual scoring per hour» (`devicePerHour` → `perHour`, two decimals, «%1 → %2») for each AHI/RERA channel whose `deviceCount` differs from `count`; empty when none.
- `Daily::manualAnalysisHtml(…)` gains a last parameter `const QString &countsTable = QString()`, placed right under the «AHI after manual scoring» line; `updateManualPage` passes `manualCountsHtml(day)`.

- [ ] **Step 1: Failing tests:**
  - `testDeviceCounts`: a day with one CPAP session holding 2 `CPAP_Obstructive` events, `setManualScoring({{CPAP_Obstructive, -1}}, 0, 0, true)` → `deviceCount(CPAP_Obstructive) == 2`, `count(CPAP_Obstructive) == 1`, `devicePerHour == 2 / hours(MT_CPAP)`.
  - `testDeviceCountSkipsDisabled`: the same session disabled → `deviceCount == 0`.
  - `testIndicesDeviceThenAnalysis`: that day plus `AN_Hypopnea` events → `indicesHtml(day, true, v)`: index of `schema::channel[CPAP_Obstructive].fullname()` < index of `OSCAR analysis` < index of `channelBaseName(AN_Hypopnea)`; the obstructive row shows `QString::number(2 / hours, 'f', 2)`; contains `#7F77DD`; no `· analysis` in the table. `indicesHtml(day, false, v)` contains neither `OSCAR analysis` nor the analysis name.
  - `testIndicesWithoutCpap`: a day with only an oximetry session → `indicesHtml` returns without crashing and has no `OSCAR analysis`.
  - `testManualCountsHtml`: the edited day → contains the obstructive name and `→`; an unedited day → empty.
  - `testManualPageCounts`: `manualAnalysisHtml("X", 3.86, 3.71, "AHI", true, "COUNTS")` → `COUNTS` sits after `AHI after manual scoring` and before `X`.
- [ ] **Step 2:** build (exit 0) → FAIL.
- [ ] **Step 3:** implement; `Daily::Load` / the sidebar code calls `indicesHtml(day, analysis::analysisShown(), values)`.
- [ ] **Step 4:** suite green; `../build` and `../build-nobt` exit 0.
- [ ] **Step 5:** commit "Daily: the device's own figures, then OSCAR's analysis apart; manual figures on their page".

### Task 3: The events tree in three groups

**Files:** `oscar/daily.{h,cpp}`, `oscar/scoringmenus.cpp`. Test: `oscar/tests/analysisintegrationtests.cpp`.

**Interfaces — Consumes:** Task 1 `AnalysisMark::treeHeading`, `channelBaseName`. **Produces:**
- `static void Daily::UpdateEventsTree(QTreeWidget *tree, Day *day, bool analysisShown);` (now static; both callers pass `analysis::analysisShown()`).
- Top-level order: device channel nodes (sorted as now); «Session Start Times», «Session End Times»; «All» (when consolidating, device events only); then `treeHeading(tr("OSCAR analysis"), true)` and the analysis channel nodes (named by `channelBaseName` + count, as device nodes are); then `treeHeading(tr("Manual scoring"), false)` and the edits node from `ScoringMenus::treeNode` (its text becomes `tr("Edits (%1)")`, expanded). A group without rows is left out with its heading.

- [ ] **Step 1: Failing tests:**
  - `testTreeGroups`: day with `CPAP_Obstructive`, `AN_Hypopnea` events and one manual edit row (as `testManualScoring…` tests make one) → top-level texts in order: obstructive node, «Session Start Times», «Session End Times», «OSCAR analysis», the hypopnea base-name node, «Manual scoring», «Edits (1)»; the two headings are not selectable.
  - `testTreeAnalysisHidden`: same day, `analysisShown=false` → no «OSCAR analysis» and no analysis node; «Manual scoring» still there.
  - `testTreeWithoutEdits`: no edits → no «Manual scoring» heading.
  - `testTreeWithoutCpap`: oximetry-only day → no crash, no headings.
- [ ] **Step 2:** build (exit 0) → FAIL.
- [ ] **Step 3:** implement; `on_treeWidget_itemClicked` ignores items whose `data(0, Qt::UserRole + 1) == "heading"`; check `Link_clicked`'s `event=` path finds analysis channel nodes by code (not by the old name) and that `applyAnalysisShown` rebuilds the tree — fix if not.
- [ ] **Step 4:** suite green (the 3 EventsTabTests failures stay the known ones; update their expected order only if a new failure appears there).
- [ ] **Step 5:** commit "Daily events: device, then OSCAR analysis, then manual scoring".

### Task 4: The mark across Daily and the other pages

**Files:** `oscar/daily.cpp` (AHI frame, «Analysis (second opinion)» header, Analysis Flags graph title), `oscar/statistics.cpp`, `oscar/Graphs/gAnalysisCharts.cpp`, `oscar/settingscomparison.cpp`, `oscar/nightsummary.cpp`, `oscar/doctorreport.cpp`. Tests: `oscar/tests/helplinkstests.cpp` (Daily statics), `oscar/tests/statisticstests` or `analysisintegrationtests.cpp` (Statistics/nightsummary), `oscar/tests/settingscomparisontests.cpp`, `oscar/tests/doctorreporttests.cpp`, `oscar/tests/analysischarttests.cpp`.

**Interfaces — Consumes:** Task 1 `pill`, `printPill`, `rowStyle`, `headingRow`, `title`.

- Daily: `ahiFiguresHtml` appends `pill()` after «OSCAR analysis — X» (the text stays intact); the «Analysis (second opinion)» section header carries `pill()`; the Analysis Flags graph title is `AnalysisMark::title(tr("Flags"))`.
- Statistics: the analysis section heading row carries `pill()`; each `SC_ANALYSIS` row's cells use `rowStyle()` and its name `pill()`; `analysisName("ahi")` becomes `tr("AHI")`; printing uses the same HTML.
- Overview: every `gAnalysisChart::title` is `AnalysisMark::title(base)` (bases: «AHI», «ODI 3% / 4%», «SpO2 Time in Ranges», «Oximetry Problem Zones», «Hypoxic Burden (approx.)», «Flow Limitation», «Flow Limitation, minutes», «Glasgow Index», «Pulse Rise Index»); the AHI tooltip line reads `AnalysisMark::title(tr("AHI"))`.
- Settings comparison: the headers of `AnalysisAhi, FlowLimitation, Glasgow, Odi3, Below90` get `pill()` after the escaped title.
- Night summary (Welcome): «OSCAR's analysis: X» on the AHI tile, the ODI 3% line on the SpO2 tile and the «OSCAR's analysis counts an AHI of …» warning carry `pill()` (HTML outputs) — use `printPill()` where the text goes to the doctor's PDF.
- Doctor PDF: the «OSCAR's analysis: …» line and its ODI carry `printPill()`.

- [ ] **Step 1: Failing tests:** `testAhiFiguresMarked` (helplinks: `ahiFiguresHtml(... analysisShown=true ...)` contains `AnalysisMark::pill()` and still `OSCAR analysis — 3.0`; with `false` no pill); `testStatisticsAnalysisMarked` (Statistics HTML for a period with an analysis row contains `#7F77DD` and no `AHI (analysis)`); `testAnalysisChartTitles` (every `gAnalysisChart::title(kind)` ends with `· analysis`); `testComparisonAnalysisMarked` (comparison HTML with `options.analysis` contains the pill once per analysis column — 5 times; without analysis 0); `testNightSummaryMarked` (Welcome HTML with an analysis AHI contains the pill); `testDoctorReportMarked` (doctor HTML with analysis contains `[analysis]`).
- [ ] **Step 2:** build (exit 0) → FAIL.
- [ ] **Step 3:** implement.
- [ ] **Step 4:** suite green; update tests that pinned old titles/headers.
- [ ] **Step 5:** commit "The «analysis» mark on every page".

### Task 5: Translation, check in the app

**Files:** `Translations/Russkiy.ru.ts`, `oscar/translations/Russkiy.ru.qm`, `oscar/uiglossary.cpp` (only if an explanation names the old «(анализ)» titles).

- [ ] **Step 1:** `/opt/homebrew/bin/lupdate oscar/oscar.pro -ts Translations/Russkiy.ru.ts`; Russian: «анализ», «Второе мнение OSCAR, экспериментально», «%1 · анализ», «Анализ OSCAR», «Правки (%1)», «%1 → %2» and the base channel/graph names (reuse existing translations of the bases); `validate_ts.py` → TOTAL 0; `lrelease … -qm oscar/translations/Russkiy.ru.qm`.
- [ ] **Step 2:** three builds exit 0; `../tools/runtests.sh` green.
- [ ] **Step 3:** commit "Russian for the separated groups and the analysis mark".
- [ ] **Step 4:** `open -n` the app; the owner checks Daily (table, tree, Manual page, AHI frame, switch on/off), Statistics, Welcome, Overview, Settings comparison, the doctor's PDF.
