# Manual scoring on a sleep screener's nasal flow and of desaturations on SpO2 — design

Fork-only, not for MR. The doctor's video of 2026-10-23, items 26 and 27; approved in chat 2026-10-24
(«делаем все 4»). Items 25 (display smoothing) and 28 (burden shading) are done (9689e8f5).

## Goal

1. A night recorded with the RS01 screener (nasal flow + SpO2, no CPAP) can be scored by hand the way a
   CPAP night can: the analysis' apneas and hypopneas on the nasal-flow graph are corrected (added,
   deleted, retyped, resized), stretches marked «Артефакт», and the night gets an REI after manual
   scoring beside the analysis' REI.
2. On any night with oximetry, desaturations can be scored by hand on the SpO2 graph: a stretch marked
   «Десатурация», an analysis desaturation deleted. ODI, the hypoxic burden, T90 and the linked figures
   follow the corrected set and carry the manual mark.

Success: on the owner's RS01 night, delete one analysis apnea and add one hypopnea on the nasal flow →
the Daily panel shows «REI 7.6, ручная разметка — 7.5» and the manual analysis page lists both edits;
mark a 40 s dip on SpO2 as a desaturation → ODI 4 % rises by one per hours and shows «ручн.».

## 1. Scoring on the screener's nasal flow

- **Which session takes an edit.** `scoringSessionAt(day, t)` (daily.cpp) prefers an enabled CPAP
  session; with none, the enabled oximeter session that has `OXI_NasalFlow` and covers `t` (or is
  nearest). Exclude/Mark/Keep already go to every stretch session.
- **What is corrected.** For a session without device events but with `OXI_NasalFlow`, `resultFor(s)`
  takes as its base the analysis' breathing events, each mapped to the scored channel it would be on a
  CPAP: `AN_ObstructiveApnea → CPAP_Obstructive`, `AN_CentralApnea → CPAP_ClearAirway`, `AN_Apnea →
  CPAP_Apnea`, `AN_Hypopnea → CPAP_Hypopnea`, `AN_ObstructiveHypopnea → CPAP_ObstructiveHypopnea`,
  `AN_CentralHypopnea → CPAP_CentralHypopnea`, `AN_RERA → CPAP_RERA` (`ManualScoring::baseEvents(s)`,
  with `Result::baseIsAnalysis = true`). Edits are stored exactly as for a CPAP session (same table,
  key = profile, "RS01", session id). The stored `AN_*` channels are never changed.
- **The index.** `ManualScoring::screenerIndex(Day*)`: events after manual scoring (base + delta,
  minus those ending in excluded stretches) per hour of the analysed nasal flow less the excluded
  time (`row.flowSeconds` of the day's analysis row, from `AnalysisService::row(date)`), as
  `struct ScreenerIndex { double rei, analysisRei; int events; double hours; bool valid; }`.
- **Graphs.** `gManualScoringLayer` is added to the `OXI_NasalFlow` graph with the same flags as the
  flow-rate graph (event boxes, resize handles, the range menu). Edits on that graph use the same
  `ScoringMenus::forRange`/`forEvent` (apnea and hypopnea types, flow limitation, artifact).
  `forEvent` says «Delete episode (do not count)» and «Undo» as now; its base events are named
  «analysis» not «device» in the undo text (`Origin::Device` stays the enum, the text comes from
  `Result::baseIsAnalysis`).
- **Where it shows.** Daily left panel on a screener night: the REI frame's second line reads
  «ручная разметка — X» (or «—»); the Manual analysis page: «REI after manual scoring: X (analysis Y)»
  with the edits list and counts; Welcome tile «REI» note: «ручная разметка — X» when scored; the
  Statistics manual table takes screener nights too (the device column shows the analysis' figure,
  headed «ручн. (анализ)» on those columns); Overview: unchanged.
- **Day counts.** `Day::count(CPAP_*)` on a screener day = `manualDelta` only (no device events), so the
  index table's device block stays empty and the manual block shows what was added; the REI frame and
  the manual page use `screenerIndex`.

## 2. Manual desaturations on SpO2

- **Edits.** Two new kinds: `Kind::Desat` — a stretch startMs–endMs on the oximeter session (stored
  like Mark/Keep, on every stretch session it touches) — and `Kind::RemoveDesat` — names an analysis
  desaturation by its end time (`matchEndMs`, graph time). Undo as for any edit.
- **Into the analysis.** `OxiOverrides` gains `QVector<Span> addDesat` and `QVector<qint64>
  removeDesat`. `ManualScoring::oximetryOverrides` fills them. In `analyzeOximetry`, after the
  desaturations are found: those whose end is within 2 s of a `removeDesat` time are dropped; for
  each `addDesat` span a `Desaturation` is built — peak = the highest SpO2 in the 30 s before the
  start (or at the start), nadir = the lowest inside the span, start/end = the span, area as for any
  (within the burden window) — and flagged `manual = true` (new field). Overlaps with found
  desaturations: the found one is dropped in favour of the manual one. Everything downstream (ODI 3/4,
  area, burden, linked desaturations, zones, the daily row) follows without change.
- **Marking.** `DayResult::manualDesats` (count) → `extra_json` «n_manual_desat»;
  `AnalysisDailyData::manualDesats`. Where ODI, HBI and the SpO2 figures are shown from the row (Daily
  panel, Welcome tile, Statistics oximetry rows) a «ручн.» pill follows the figure when
  `manualDesats > 0`.
- **Graphs.** On the `OXI_SPO2` graph in scoring mode: dragging a stretch opens `ScoringMenus::
  forSpo2Range`: «Desaturation» (D), «Artifact» (X), «Not an artifact» (K). Right-click on an
  `AN_Desaturation` flag (the span under the pointer): «Delete desaturation» (R) or, on a manual one,
  «Undo» (U). `gManualScoringLayer` draws manual desaturations as green boxes with «ручн.» and deleted
  ones struck through (Item::Desat, Item::RemovedDesat). The burden shading (gDesatFill) follows the
  corrected set: it reads the day's `DayResult.oxi.desaturations` from Daily (set on the layer) rather
  than the stored `AN_Desaturation` channel, so manual ones are shaded and deleted ones not.
- **Re-scoring.** As with «Артефакт»/«Не артефакт»: an edit calls `Daily::scoringChanged()`, which has
  the analysis service re-score the day (`scoreStoredDay` with the overrides); the session's stored
  `AN_Desaturation` stays the analysis' own.

## Not in scope

- Classifying a screener's apneas by hand into obstructive/central is allowed (the menu has every type)
  but the analysis itself stays unclassified.
- The doctor PDF report: unchanged.
- Manual desaturations on CMS50/O2Ring nights without CPAP: they work the same (the oximeter session is a
  stretch session); nothing special.

## Testing

- `scoringSessionAt`: screener night → the RS01 session; CPAP + RS01 → CPAP inside its time, RS01 outside.
- `resultFor` on a nasal-flow session: base from `AN_*` with the mapping; Remove/Retype/Add/Exclude
  apply as on CPAP; `screenerIndex` = (base ± delta)/analysed hours, excluded time out.
- `oximetryOverrides`: Desat and RemoveDesat edits → `addDesat`/`removeDesat` in graph time.
- `analyzeOximetry` with overrides: a removed desaturation gone from ODI; an added one counted with
  peak/nadir/area as specified; overlap resolved; `manual` flagged; `countDesaturations(4)` of a 3 %
  manual dip excludes it.
- Row round trip of `manualDesats`; Daily panel/Welcome html shows the pill when > 0.
- Menus: `forSpo2Range` items and keys; `forEvent` text for an analysis base («analysis»).
- Statistics manual table with a screener night: column header and figures.
- Local (RS01_ROOT): the owner's night — delete one apnea, REI by hand = (2−1+92)/7.71.

## Risks

- A screener night re-scored on every edit runs the oximetry analysis again (seconds): acceptable, as
  for artefacts.
- Manual desaturations change the hypoxic burden, which the doctor reads as a clinical figure: the pill
  must be visible everywhere the figure is.
