# Manual Scoring on Nasal Flow and of Desaturations — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

Fork-only, not for MR.

**Goal:** the doctor scores a screener's breathing events by hand on the nasal-flow graph (REI after manual scoring), and desaturations by hand on the SpO2 graph (ODI/HBI follow, marked «ручн.»).

**Spec:** `docs/superpowers/specs/2026-10-24-manual-scoring-screener-and-desats-design.md`

## Global Constraints

- The stored `AN_*` channels are never changed by manual scoring; edits live in `manual_scoring` as now.
- Base mapping on a nasal-flow session: AN_ObstructiveApnea→CPAP_Obstructive, AN_CentralApnea→CPAP_ClearAirway, AN_Apnea→CPAP_Apnea, AN_Hypopnea→CPAP_Hypopnea, AN_ObstructiveHypopnea→CPAP_ObstructiveHypopnea, AN_CentralHypopnea→CPAP_CentralHypopnea, AN_RERA→CPAP_RERA.
- Edit kinds stored by name: new "desat" (Kind::Desat), "removedesat" (Kind::RemoveDesat).
- A manual desaturation: peak = highest SpO2 in the 30 s before its start (or at the start), nadir = lowest inside, area within the burden window; a removed one matches an analysis desaturation ending within 2 s.
- Every figure that includes manual desaturations shows ManualMark::pill().

## Review Focus

1. CPAP + RS01 night: scoring on the flow graph goes to CPAP; on nasal flow outside CPAP time to RS01; REI by hand only on screener nights.
2. Removing all analysis desaturations / adding one on a night without SpO2 there — no crash, ODI 0 or 1.
3. A Desat edit overlapping a Exclude («Артефакт») stretch — the artefact wins (no SpO2 there).
4. An old database without the new kinds — reading unknown kind names is skipped, not crashed.
5. A screener night with edits whose analysis was re-run (version bump) — edits still match by time.

---

### Task 1: Screener base and index
**Files:** `SleepLib/manual_scoring.{h,cpp}`, `daily.cpp` (scoringSessionAt), `Graphs/scoringgesture.cpp`; tests `analysisintegrationtests`, `scoringgesturetests`(or existing).
**Produces:** `bool ManualScoring::isScreenerSession(Session*)`; `resultFor` base from AN_* for it (`Result::baseIsAnalysis`); `struct ScreenerIndex { bool valid; double rei, analysisRei; int events; double hours; }; ScreenerIndex screenerIndex(Day*, double analysedSeconds)`; gestures on nasal flow and SpO2.
- [ ] Tests: testScreenerScoringBase (nasal session with AN_Apnea ×2, AN_Hypopnea ×1 → resultFor events mapped, delta after Remove/Add/Retype); testScreenerIndex; testScoringSessionAtScreener; gesture test: SelectRange on NasalFlow and SPO2 graphs.
- [ ] Implement, green, suite, commit.

### Task 2: Screener in the UI
**Files:** `daily.cpp` (layer on nasal graph, REI frame line, manual page, onScoringContext/updateScoringLayer include screener sessions), `nightsummary.{h,cpp}` (manual REI note), `statistics.cpp` (screener nights in the manual table, column heading), `scoringmenus.cpp` (undo text for analysis base).
- [ ] Tests: reiFiguresHtml with a manual REI; NightSummary tile note; Statistics manual table with a screener night; forEvent text for an analysis base.
- [ ] Implement, green, suite, translations, commit.

### Task 3: Manual desaturations in the analysis
**Files:** `SleepLib/manual_scoring.{h,cpp}` (Kind::Desat/RemoveDesat, kind names, oximetryOverrides), `SleepLib/analysis/oxi_analyzer.{h,cpp}` (OxiOverrides addDesat/removeDesat, Desaturation::manual, apply), `day_scorer`/`day_analysis` (DayResult::manualDesats, extra_json n_manual_desat), `database/analysis_daily_repository` (manualDesats).
- [ ] Tests: oximetryOverrides with Desat/RemoveDesat; analyzeOximetry add/remove/overlap/artefact-wins; row round trip.
- [ ] Implement, green, suite, commit.

### Task 4: Desaturations on the SpO2 graph
**Files:** `scoringmenus.{h,cpp}` (forSpo2Range, forDesat), `daily.cpp` (route SpO2 range/context, apply desat/removedesat), `Graphs/gManualScoringLayer` (Desat items), `Graphs/gDesatFill` (from the day's result), panel/tile/statistics pills.
- [ ] Tests: menus; layer items; gDesatFill uses supplied desaturations; pills shown when manualDesats > 0.
- [ ] Implement, green, suite, translations, commit.

Then final review (opus), fixes, owner check.
