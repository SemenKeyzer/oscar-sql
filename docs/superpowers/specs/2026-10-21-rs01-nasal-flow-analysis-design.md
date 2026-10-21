# Contec RS01, stage 2: OSCAR's analysis of the nasal flow — design

Fork-only, not for MR. Approved in chat 2026-10-21: CPAP first and the RS01 outside CPAP time;
apneas not classified; the index named REI. Stage 1 (import) is
`docs/superpowers/specs/2026-10-21-contec-rs01-import-design.md`.

## Goal

A night recorded with the RS01 sleep screener (nasal-cannula pressure + SpO2, no CPAP) gets OSCAR's
own breathing analysis: apneas and hypopneas (confirmed by SpO2 as on CPAP nights), a respiratory event
index (REI: events per hour of analysed recording — a home screener does not know when the person
slept), linked desaturations and the hypoxic burden's linked part — shown on the Welcome page, the
Daily page, the OSCAR analysis panel and tab, the Overview and Statistics, marked «анализ».

Success: on the owner's 10170150 the analysis finds the breathing pause at ≈04:29:00–04:29:20 as an
apnea, the night shows an REI, and the nasal-flow graph shows the analysis' events.

## What the analysis reads (stage 1, per session)

- `runFlow` (`session_analysis.cpp`): when a session has no `CPAP_FlowRate` but has `OXI_NasalFlow`, it
  analyses that, after **square-root linearisation** `sign(x)·sqrt(|x|)` (cannula pressure grows about as
  the square of the flow; without it a 30 % flow drop reads as 51 %). The analyzer itself is unit-free
  (relative thresholds, its own zero removal, 50 Hz decimated to 25 Hz).
- For the nasal flow: **no flow limitation and no Glasgow Index** (`flScored = false`, no FL score
  channel, no Glasgow): the 10-bit pressure is too coarse for breath shape and the original Glasgow
  has L/min constants; **apneas not classified** (all `AN_Apnea`, classifier off), RERA and periodic
  breathing as the analyzer finds them.
- The stamp records the source (`"src": "nasal"`) so the day can tell a screener's flow from a CPAP's;
  `kAnalysisAlgoVersion` 11 so stored RS01 sessions are analysed again.

## The day (stage 2)

- `buildDayInput`: the flow sessions are the CPAP sessions **plus** analysed nasal-flow sessions; a
  nasal-flow session contributes only **outside the CPAP sessions' time** (its events, its flow and
  unscoreable time inside a CPAP span are dropped), so nothing is counted twice. `CpapSession` gets
  `bool cpap` (false for the screener); device events come from CPAP sessions only.
- `scoreDay`: `hasCpap` = any CPAP session (as now); `hasFlow` = any analysed flow (CPAP or nasal);
  new `DayResult::screener` = the analysed flow is a screener's only (no CPAP flow that night).
  - hypopnea confirmation by SpO2 as now (same rule);
  - `linkDesaturations` / unexplained desaturations / the linked burden gated on `hasFlow` (was `hasCpap`);
  - `compare` (agreement with the device) only with CPAP (`hasCpap && hasFlow`);
  - `limitOxiToCpap` unchanged (CPAP spans only).
- `storeHypopneas` writes the day's hypopneas into whichever session holds that flow (CPAP or RS01).
- `analysis_daily`: the linked area / counts stored when `hasFlow && hasOximetry`; a new column
  `screener` (bool) for the label (schema bump); the AHI figures are unchanged (events / flow seconds).

## Showing it

- Name: on a screener night the index is **«REI · анализ»** (Russian «РИД · анализ»), same scale 5/15/30;
  CPAP nights keep «AHI · анализ». Glossary entry `rei`: events per hour of recording, why not AHI,
  that mouth breathing can look like apneas on a nasal cannula.
- Welcome (`nightsummary`): a night without CPAP but with screener analysis gets a tile «РИД · анализ»
  with the apnea/hypopnea split, before the SpO2 tile.
- Daily: the left panel shows the REI line and the analysis indices on such a night (today only with
  CPAP hours); the indices divide by the analysed hours; the nasal-flow graph shows `AN_Apnea`,
  `AN_Hypopnea` (and RERA) flags by default.
- Analysis panel: on a screener night the breathing table has the analysis column only (no «Device»);
  linked/unexplained desaturation rows shown with `hasFlow`.
- Overview: the analysis AHI chart (and FL charts) take every night with analysis rows, not only the
  CPAP date range (`MT_UNKNOWN`); its title on screener nights' tooltips says REI.
- Statistics: rows already read `hasFlow`; the AHI row's label says «AHI / REI · анализ» when the
  period has screener nights. Agreement stays CPAP-only.
- Doctor PDF: unchanged (CPAP nights only) — a later item.

## Testing

- `FlowAnalyzer`/session: a synthetic nasal-flow session (sine breathing in Pa at 50 Hz with a 20 s
  zero stretch) → one `AN_Apnea`, no `AN_ObstructiveApnea`/`AN_CentralApnea`, no FL score, stamp
  src nasal; the sqrt transform: a breath scaled to 0.49 in pressure (0.7 in flow) is a 30 % reduction,
  not 51 %.
- Day: a screener-only day → `hasFlow`, `screener`, no comparison, a linked desaturation; a day with
  CPAP 23:00–03:00 and RS01 22:00–07:00 → the RS01 contributes only 22:00–23:00 and 03:00–07:00 (events
  inside CPAP time dropped, flow seconds not doubled).
- UI functions: the Welcome tile and the panel without the Device column on a screener night; the
  REI label; Overview chart includes an oximeter-only date.
- Local only (not committed): `RS01_ROOT` night 10170150 → an apnea around 04:29:00–04:29:20 and an REI
  printed.

## Risks

- Mouth breathing on a nasal cannula looks like apneas (said in the help); no EEG, so REI ≤ AHI of a lab
  study; unvalidated against a scored study — marked «анализ», experimental.
