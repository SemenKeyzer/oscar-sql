# "OSCAR analysis" show/hide switch — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** One switch, «Анализ OSCAR» (Daily bottom bar + View menu), shows or hides everything of OSCAR's own analysis on every page at once; off by default, kept per profile; the analysis computation is untouched.

**Architecture:** The profile keeps `AnalysisSettings::shown()` (default false). Display code asks it at the few places analysis data enters the UI: Statistics' `analysisRows()`, `buildNightSummary*`, Welcome's oximetry figures, Overview's analysis charts (`gAnalysisChart` row source), Daily's `loadAnalysis()`, and the channel lists of Daily (sidebar table, events tree). Analysis graphs stay created and keep the user's own visibility; a new `gGraph::setSuppressed(bool)` makes them count as empty, so gGraphView skips them. `MainWindow::setAnalysisShown(bool)` stores the switch, syncs the button and the menu action and refreshes the pages (`refreshAnalysisViews`).

**Tech Stack:** Qt 6.11, qmake, C++17 `-Werror`, QtTest (`../build-test/test`).

**Spec:** `docs/superpowers/specs/2026-10-09-analysis-toggle-design.md`

## Global Constraints

- Button text «Анализ OSCAR» (source `tr("OSCAR analysis")`), checkable, next to «Разметка»; menu «Вид → Анализ OSCAR» checkable, always in the same state.
- Default off; stored per profile (`STR_AN_Shown`, initPref false).
- Switching is immediate: no restart, no profile reload.
- Hidden when off (spec §3): Daily analysis tab content, sidebar «Анализ (второе мнение)», «анализ OSCAR» line in the AHI frame, Analysis Flags graph and graphs of computed channels, flow difference marks, «(анализ)» rows in the sidebar table and events tree; Welcome analysis figures and warnings; Overview analysis charts and the «Анализ» set button (selected «Анализ» → «Все»); Statistics analysis rows; settings comparison analysis columns; doctor PDF analysis notes.
- The computation (`AnalysisParams::enabled`, background analysis, import) does not depend on the switch.
- Shown but computation off: the Daily analysis section says «Анализ OSCAR не вычисляется — включите его в Настройки → Анализ» (source `tr("OSCAR's analysis is not computed: switch it on in Preferences → Analysis.")`).
- Clinical/permissive mode not tied to the switch; only their explanations (uiglossary `ui.prefs.clinicalMode` / permissive entry) are rewritten per spec §5.
- Every new string translated; `validate_ts.py` TOTAL 0; glossary coverage test passes.
- Build `../build-test`, `../build`, `../build-nobt` and check each exit code; `../tools/runtests.sh` green.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A profile opened with the switch off whose Overview last preset was «Анализ» — opens on «Все», no empty page.
2. Graph visibility the user chose (e.g. hid «Флаги анализа» by hand) survives switching off and on, and is not overwritten in the saved graph settings while off.
3. The doctor PDF / Statistics printed while the switch is off contain no analysis rows (they read the same `analysisRows`).
4. Switching profiles: the new profile's own switch state applies (button, menu, pages).
5. The switch on, a day without analysis rows (not yet computed) — Daily shows nothing broken; the "not computed" hint only when computation is off.

---

### Task 1: The setting and the data gates

**Files:** `oscar/SleepLib/profiles.h` (+ STR key where the other `STR_AN_*` are defined), `oscar/statistics.cpp` (`analysisRows`), `oscar/nightsummary.cpp:252`, `oscar/welcome.cpp:521`, `oscar/Graphs/gAnalysisCharts.cpp:25`, `oscar/daily.cpp` (`loadAnalysis`).
**Tests:** `oscar/tests/analysisintegrationtests.{h,cpp}` (has a profile and an `AnalysisService`).

**Interfaces — Produces:**
- `bool AnalysisSettings::shown() const; void setShown(bool);` (`STR_AN_Shown`, default false).
- `bool analysis::analysisShown();` (in `SleepLib/analysis/analysis_service.h`): `p_profile && p_profile->analysis && p_profile->analysis->shown()`.

- [ ] **Step 1: Failing tests**
  - `testAnalysisShownDefaultOff`: fresh `AnalysisSettings` → `shown()` false; `setShown(true)` → true after re-reading the profile pref.
  - `testHiddenAnalysisLeavesPages`: a day with a stored analysis row (reuse the fixtures of `testAnalysisServiceKeepsDaysCurrent`); switch off → `Statistics` analysis rows for the period are empty (`analysisFigure`/the HTML has no "AHI (analysis)"), `buildNightSummaryFor` has no analysis AHI (`hasAnalysisAhi` false), doctor report figures have no analysis; switch on → present.
- [ ] **Step 2:** build (exit 0), run → FAIL.
- [ ] **Step 3:** add the pref; gate `analysisRows()`, `nightsummary.cpp:252` (`analysisOn` also needs `analysis::analysisShown()`), `welcome.cpp:521`, the `gAnalysisCharts` row source, and `Daily::loadAnalysis` (`m_analysisShown` false when hidden).
- [ ] **Step 4:** suite green.
- [ ] **Step 5:** commit "OSCAR analysis switch: the setting and the data it hides".

### Task 2: Graphs and channel lists

**Files:** `oscar/Graphs/gGraph.{h,cpp}`, `oscar/daily.{h,cpp}`, `oscar/overview.{h,cpp}`, `oscar/overviewpresets.{h,cpp}`.
**Tests:** `oscar/tests/scoringmodetests.cpp` (graph-free helpers) or a new small test in `analysispaneltests`; `oscar/tests/overviewpresetstests.*` if present (else `analysisintegrationtests`).

**Interfaces — Consumes:** Task 1 `analysis::analysisShown()`. **Produces:**
- `void gGraph::setSuppressed(bool on); bool gGraph::suppressed() const;` — `isEmpty()` returns true while suppressed; the user's `visible()` is untouched.
- `static bool Daily::channelShown(ChannelID code, bool analysisShown);` — false for `schema::channel[code].isComputed()` when the analysis is hidden.
- `QList<OverviewPresets::Preset> OverviewPresets::presets(bool analysisShown);` — no `Analysis` when hidden (the existing no-argument `presets()` keeps returning all).
- `OverviewPresets::Preset OverviewPresets::shownPreset(Preset p, bool analysisShown);` — `All` for `Analysis` when hidden, else `p` (used at profile open and on switching).
- `void Daily::applyAnalysisShown(); void Overview::applyAnalysisShown();` — suppress analysis graphs (Daily: Analysis Flags + graphs of computed channels; Overview: `gAnalysisChart` graphs), hide the «Анализ» set button and leave a selected «Анализ» for «Все».

- [ ] **Step 1: Failing tests**
  - `testSuppressedGraphIsEmpty`: a `gGraph` with a non-empty layer → `isEmpty()` false; `setSuppressed(true)` → true; `visible()` unchanged; `setSuppressed(false)` → false.
  - `testChannelShown`: `channelShown(CPAP_Obstructive, false)` true; an analysis channel (`isComputed`) → false when hidden, true when shown.
  - `testPresetsWithoutAnalysis`: `presets(false)` lacks `Analysis`, `presets(true)` has it; `shownPreset(Analysis, false)` is `All`, `shownPreset(Analysis, true)` is `Analysis`, `shownPreset(Oxygen, false)` is `Oxygen`.
- [ ] **Step 2:** build (exit 0), run → FAIL.
- [ ] **Step 3:** implement; use `channelShown` in Daily's sidebar channel table and `UpdateEventsTree`; call `applyAnalysisShown()` at the end of `Daily::Load` and `Overview::ReloadGraphs` (and from Task 3's switch).
- [ ] **Step 4:** suite green; app builds.
- [ ] **Step 5:** commit "OSCAR analysis switch: graphs, channel lists and the Overview set".

### Task 3: The switch, the hint, the mode explanations

**Files:** `oscar/daily.{h,cpp}` (button next to `scoringButton`, object name `analysisButton`), `oscar/mainwindow.{h,cpp,ui}` (action `actionShowAnalysis` in the View menu), `oscar/analysispanel.cpp` or `daily.cpp` `getAnalysisInformation` (hint), `oscar/uiglossary.cpp` (entries `ui.daily.analysisButton`, `ui.menu.actionShowAnalysis`; rewritten clinical/permissive explanations), `Translations/Russkiy.ru.ts`.
**Tests:** `oscar/tests/analysisintegrationtests.cpp`, `oscar/tests/uicoveragetests.cpp` (must stay green).

**Interfaces — Consumes:** Tasks 1–2. **Produces:** `void MainWindow::setAnalysisShown(bool on);` (stores, syncs action and Daily button without re-emitting, calls `daily->applyAnalysisShown()`, `overview->applyAnalysisShown()`, `refreshAnalysisViews()`); `Daily::analysisToggled(bool)` signal from the button.

- [ ] **Step 1: Failing tests**
  - `testNotComputedHint`: switch shown, `AnalysisParams::enabled` false → the Daily analysis section HTML (`Daily::getAnalysisInformation` path or a static helper `AnalysisPanel::notComputedHtml()`) contains the hint; computation on → no hint.
  - glossary coverage: new `analysisButton` / `actionShowAnalysis` have entries (UiCoverageTests / glossary tests fail until added).
- [ ] **Step 2:** build (exit 0), run → FAIL.
- [ ] **Step 3:** implement; on profile open MainWindow applies the stored state to the action and button.
- [ ] **Step 4:** lupdate, Russian translations (button «Анализ OSCAR», menu «Анализ OSCAR», hint, explanations), `validate_ts.py` TOTAL 0, lrelease; all three builds (exit 0); suite green.
- [ ] **Step 5:** commit "OSCAR analysis switch: the button, the View menu item and the hint".

### Task 4: Check in the app

- [ ] Launch with `open -n ../build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа` (never from the shell directly). Check: off by default on a profile without the pref… (the dev profile has none → off); Daily without analysis flags/rows; switch on → all back; Overview set «Анализ» appears; owner confirms by hand.
- [ ] Handoff notes (§4.9 area) with `git add -f`; commit.
