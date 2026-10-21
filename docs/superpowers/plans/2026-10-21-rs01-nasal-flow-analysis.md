# RS01 Nasal-Flow Analysis (stage 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Fork-only, not for MR.

**Goal:** OSCAR's breathing analysis on the RS01's nasal flow: apneas/hypopneas, REI, linked desaturations, shown everywhere the analysis is.

**Architecture:** Stage 1 (`runFlow`) falls back to `OXI_NasalFlow` (sqrt-linearised, no FL/Glasgow, no classification) and stamps the source; the day (`buildDayInput`/`scoreDay`) treats analysed nasal-flow sessions as flow sessions outside CPAP time and marks the night `screener`; the UI names the index REI on such nights and shows it without CPAP.

**Tech Stack:** C++17, Qt 6, qmake; QtTest via `../tools/runtests.sh`.

**Spec:** `docs/superpowers/specs/2026-10-21-rs01-nasal-flow-analysis-design.md`

## Global Constraints

- Linearisation `sign(x)·sqrt(|x|)` on every nasal-flow sample before `analyzeFlow`.
- Nasal flow: `flScored = false`, no `AN_FLScore`, no Glasgow, `FlowParams.classifyApneas = false`.
- Stamp field `"src"`: `"cpap"` or `"nasal"`; `kAnalysisAlgoVersion` 11.
- A nasal-flow session counts only outside CPAP sessions' spans; device events from CPAP sessions only; agreement only with CPAP.
- `DayResult::screener` / `AnalysisDailyData::screener` (new column, schema bump through the repository's own migration path).
- Index name: «REI · analysis» / «РИД · анализ» on screener nights; glossary key `rei`; scale 5/15/30.
- Owner recordings only via `RS01_ROOT` locally; never committed.

## Review Focus

1. A night with CPAP and RS01 overlapping — no double events, flow seconds not doubled — test in Task 2.
2. A screener night with no SpO2 (probe off all night) — hypopneas by flow only, no crash — test in Task 2.
3. A stored RS01 session analysed before this change (empty flow stamp) — re-analysed after the version bump — test in Task 1.
4. A CPAP night unchanged by all this (AHI label, device column, agreement) — tests in Tasks 2 and 3.
5. An oximeter-only night without nasal flow (CMS50, O2Ring) — nothing new appears (no REI tile) — test in Task 3.

---

### Task 1: Stage 1 on the nasal flow
**Files:** `oscar/SleepLib/analysis/session_analysis.{h,cpp}` (runFlow, SessionStamp src), `analysis_params.h` (version 11); tests `oscar/tests/analysisintegrationtests.{h,cpp}`, `oscar/tests/flowanalyzertests.cpp` (sqrt helper).
**Produces:** `float analysis::linearisedNasal(float pa)`; `SessionStamp::source` ("cpap"/"nasal", JSON "src").
- [ ] Tests: `testNasalLinearisation` (0.49 → 0.7; sign kept); `testStageOneNasalFlow` (oximeter session with OXI_NasalFlow sine breathing 50 Hz, 20 s flat → `AN_Apnea` 1, no OA/CA, no `AN_FLScore`, stamp flowAnalyzed, source "nasal", flScored false); `testNasalSessionReanalysedAfterBump` (stamp with old version → stageOneNeeded flow).
- [ ] Red, implement, green, suite, commit "Analysis: OSCAR's flow analysis on a screener's nasal flow".

### Task 2: The day
**Files:** `oscar/SleepLib/analysis/day_analysis.cpp`, `day_scorer.{h,cpp}`, `oscar/database/analysis_daily_repository.{h,cpp}` (+ schema), tests `dayscorertests`, `analysisintegrationtests`.
**Consumes:** Task 1 stamp source. **Produces:** `CpapSession::cpap`, `DayResult::screener`, `AnalysisDailyData::screener`.
- [ ] Tests: `testScreenerDay` (DayInput with a non-CPAP flow session + SpO2 dip after an apnea → hasFlow, !hasCpap, screener, linked desaturation, !hasComparison); `testScreenerNoSpo2` (flow-only hypopneas, no crash); `testCpapAndScreenerOverlap` (integration: CPAP session 1 h inside a 3 h RS01 session → RS01 events inside CPAP time dropped, flowSeconds = CPAP + RS01 outside); `testCpapDayUnchanged` (existing CPAP fixture: screener false, comparison as before); repository round-trip of `screener`.
- [ ] Red, implement, green, suite, commit "Day analysis: a screener's nasal flow outside CPAP time".

### Task 3: Showing it
**Files:** `oscar/nightsummary.{h,cpp}`, `oscar/daily.cpp`, `oscar/analysispanel.cpp`, `oscar/Graphs/gAnalysisCharts.cpp`, `oscar/Graphs/gLineChart.cpp`, `oscar/statistics.cpp`, `oscar/glossary.cpp`, translations; tests `nightsummarytests`, `analysispaneltests`, `analysischarttests`, `glossarytests`.
- [ ] Tests: `testScreenerTile` (NightSummary without CPAP, analysis REI → a tile «REI · analysis»; a CMS50 night → none); `testSidebarScreenerNoDeviceColumn`; `testChartKindsTakeOximeterNights` (Ahi chart machine type MT_UNKNOWN); glossary `rei`.
- [ ] Red, implement (Daily left panel REI line + indices on analysed hours on oximeter-only nights; nasal-flow graph flags AN_Apnea/AN_Hypopnea/AN_RERA by default), green, suite, translations, commit "Show the screener's REI".

Then local check on 10170150 (`RS01_ROOT`), final review (opus), fixes, owner check.
