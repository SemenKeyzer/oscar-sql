# Sleep analysis ("second opinion")

**Kind:** reference — keep in sync with `oscar/SleepLib/analysis/`.

OSCAR's own analysis of each night, independent of the device's event scoring: apneas,
hypopneas and RERA-like episodes from the CPAP flow waveform, flow limitation from the
shape of each inspiration, periodic breathing, desaturations and problem zones from the
oximeter, pulse rises, and a comparison with the events the device scored.

> Experimental analysis for self-review. It is not a medical diagnosis; discuss therapy
> changes with your clinician. (Shown wherever the analysis is: Daily sidebar, Statistics,
> Preferences.)

The analysis **never** changes the device's events, AHI, statistics or reported channels.
Its channels are flagged `Channel::isComputed()`, which keeps them out of
`Machine::noteReportedChannels()` (so they do not change device capabilities such as the
OH/CH split), out of `respiratory_events`, out of the Event Flags graph and the pie chart,
and out of the AHI channel lists.

## Pipeline

```
import ─► Session::UpdateSummaries ─► stage 1 (per session) ─► AN_* event lists + AN_Stamp
       └► MainWindow::updateAnalysis ─► stage 2 (per day, pending days) ─► AN_*Hypopnea + analysis_daily

Daily::Load ─► AnalysisService::dayResult ─► [stage 1 if outdated] ─► [stage 2 if outdated] ─► panel, tab, graphs
time correction / session on-off / purge ─► MainWindow::updateAnalysis ─► stage 2
Overview / Statistics ─► AnalysisService cache of analysis_daily (no events loaded)
```

- **Stage 1** (`session_analysis`) runs on one session whose events are in memory, in raw
  device time: the flow analyzer on `CPAP_FlowRate`, the oximetry analyzer on `OXI_SPO2` /
  `OXI_Pulse` (oximetry channels go to the session holding the SpO2, e.g. a ResMed session
  with an oximeter adapter). It runs from `Session::UpdateSummaries()` during import (loaders
  may run on worker threads, so it reads the parameters from `analysis::activeParams()`, a
  copy the main thread keeps current) and from the oximeter wizard's session builder. It is
  skipped for sessions without events in memory or only partly loaded (it would record an
  analysis of nothing as current) and for Apple Health spot checks.
- **Stage 2** (`day_analysis`, `day_scorer`) works on a whole day in **corrected** time
  (`t + Session::correctionMs()`), from the stage 1 channels, the device's respiratory
  events and the day's 1 Hz SpO2/pulse. It needs no waveform, so it loads only those event
  channels (`Session::LoadEventsFromDatabase(QSet)`) and puts them away again.
- **`AnalysisService`** (owned by `MainWindow`) holds the parameters in force, caches the
  profile's `analysis_daily` rows and decides what to recalculate:
  - *pending* days — stage 1 of every session current, day scoring missing or outdated
    (new imports, time corrections, a session switched on or off, a purge): brought up to
    date right away, a few days directly, more behind a cancellable progress dialog;
  - *outdated* days — some session needs stage 1 (data imported before the analysis
    existed, changed flow/oximetry parameters): these need the waveforms, so they wait
    for the day to be opened or for *Data → Recalculate Analysis…*. The Overview and
    Statistics show how many there are, with a link to recalculate them.

## Files

| File | What |
|---|---|
| `analysis_params.{h,cpp}` | `AnalysisParams` (oxi / flow / day), `kAnalysisAlgoVersion`, per-stage parameter hashes |
| `signal_utils.{h,cpp}` | 1 Hz grids from lists, median/moving filters, percentiles, decimation |
| `oxi_analyzer.{h,cpp}` | SpO2 cleaning, desaturations, cyclic runs, pulse rises, low/high pulse, histograms, problem zones |
| `flow_analyzer.{h,cpp}` | breaths, envelope and baseline, apneas and hypopnea candidates, unscoreable time, FL score, RERA, periodic breathing |
| `apnea_classifier.{h,cpp}` | obstructive / central evidence (cardiogenic oscillations, breaths around the event, Prisma obstruction level) |
| `session_analysis.{h,cpp}` | stage 1 adapter; `SessionStamp`; `activeParams()` |
| `event_matcher.{h,cpp}` | pairing device and analysis events |
| `device_event_conventions.{h,cpp}` | how each loader times its events |
| `day_scorer.{h,cpp}` | stage 2, pure: hypopnea rules, classification, linkage, comparison, offset hint |
| `day_analysis.{h,cpp}` | stage 2 adapter: `Day` → `DayInput`, inputs hash, storing |
| `analysis_service.{h,cpp}` | cache, pending/outdated days, batch recalculation |
| `analysis_channels.{h,cpp}` | the `AN_*` channels |
| `database/analysis_daily_repository.{h,cpp}` | `analysis_daily` table |
| `analysispanel.{h,cpp}`, `analysisprefs.{h,cpp}`, `Graphs/gAnalysisCharts.{h,cpp}` | Daily sidebar and tab, Preferences tab, Overview charts |

The analyzers use QtCore only (no `Session`, `Profile` or GUI) and are tested on synthetic
signals.

## Channels (0x1A00–0x1A3F, group `ANALYSIS`)

| ID | Code | Type | Stage | Notes |
|---|---|---|---|---|
| 0x1A00 | `AnObstructiveApnea` (aOA) | flag | 1 | |
| 0x1A01 | `AnCentralApnea` (aCA) | flag | 1 | |
| 0x1A02 | `AnApnea` (aA) | flag | 1 | not classified |
| 0x1A03 | `AnFlowReduction` (aFR) | minor flag | 1 | hypopnea candidate; data2 = reduction % (hidden by default) |
| 0x1A04 | `AnRERA` (aRE) | flag | 1 | |
| 0x1A05 | `AnFlowLimitation` (aFL) | span | 1 | |
| 0x1A06 | `AnPeriodicBreathing` (aPB) | span | 1 | data2 = period, s |
| 0x1A07 | `AnUnscoreable` (aUS) | span | 1 | hidden by default |
| 0x1A08 | `AnFLScore` | waveform (one value per breath) | 1 | 0–1, "Flow Limitation (analysis)" graph |
| 0x1A10–12 | `AnObstructiveHypopnea`, `AnCentralHypopnea`, `AnHypopnea` | flag | 2 | written per CPAP session |
| 0x1A20 | `AnDesaturation` (aDS) | flag | 1 | data2 = depth; on the SpO2 graph by default |
| 0x1A21 | `AnCyclicDesaturation` (aCD) | span | 1 | data2 = number of desaturations |
| 0x1A22 | `AnPulseRise` (aPR) | flag | 1 | data2 = amplitude; on the pulse graph by default |
| 0x1A23–24 | `AnBradycardia`, `AnTachycardia` | span | 1 | |
| 0x1A25 | `AnOxiProblemZone` (aPZ) | span | 1 | data2 = severity 1–2 |
| 0x1A30 | `AnStamp` | session setting (text) | 1 | see below |

Events follow OSCAR's convention: time = end of the event, data = duration in seconds.

## Storage

- Stage 1 channels are ordinary event lists (`event_lists`/`event_data`).
  `Session::StoreChannelEvents()` replaces only the given channels, without rewriting the
  waveforms. A partly loaded session refuses `StoreEventsToDatabase()`.
- **Stamp** (`AN_Stamp`, `session_settings`, text):
  `{"flow":{"v":2,"p":"<hash>","a":true,"hz":25,"fls":true,"s":25200,"u":300,"fl":123.4,"flb":6000},"oxi":{"v":2,"p":"<hash>"}}`
  — per part the algorithm version and parameter hash (a part is outdated when either
  differs), and the flow totals day scoring needs without the waveform: analysed, sample
  rate, FL scored, scoreable and unscoreable seconds, FL score sum and breath count.
  Sessions without any stored events are stamped as analysed with nothing found.
- **`analysis_daily`** (schema v20): one row per profile-day with counts and seconds, so
  that periods aggregate exactly; a group of columns is `NULL` when it does not apply. See
  `Notes/Database/DATABASE_SCHEMA.md`. `inputs_hash` covers the algorithm version, the day
  parameters and, per CPAP/oximetry session of the day, its id, enabled state, time range,
  time correction, stamp and device event counts (from the summary, so the hash does not
  depend on what is loaded). Derived data: not in `.oscar` backups; purges delete the rows.

## Algorithms (defaults in brackets)

### Oximetry (stage 1, and again per day in stage 2)
- 1 Hz grid per list (a value holds until the next one); out-of-range values (SpO2 outside
  50–100, pulse outside 30–220) and gaps are missing data. SpO2 spikes of ≥ 10 points that
  return within 5 s are removed; 5 s median; stretches under 60 s between gaps of ≥ 10 s
  are dropped.
- **Desaturation**: a peak–nadir state machine against the highest SpO2 of the last 120 s
  since the previous event; starts at 1 point below the peak once the fall reaches [3]
  points, ends back within 1 point of the peak or 2/3 recovered; at least [10] s, the fall
  within [120] s, cut at [180] s. Area = Σ(peak − SpO2)·s. ODI 4 % counts the same events
  with depth ≥ 4.
- **Cyclic desaturation**: ≥ 3 desaturations with nadirs 20–120 s apart.
- **Pulse rise**: baseline = median of the pulse 30–5 s before; a rise of [6] bpm within
  10 s, held ≥ 3 s. **Low/high pulse**: below [40] / above [120] bpm for ≥ [30] s.
- **Histograms**: seconds at each SpO2 % (50–100) and bpm (30–220), so any "time below"
  threshold is read later without recalculating.
- **Problem zones**: 5-minute windows every 30 s flagged by ≥ 3 desaturations, ≥ 60 s
  below [90] % or ≥ 30 s below [85] %; flagged windows merge, are trimmed to what flagged
  them and widened to at least [120] s; zones closer than [120] s merge. Severity 2 when
  SpO2 fell below the critical level for ≥ 30 s (or its minimum is below it).

### Flow (stage 1)
- Sample rate < 4 Hz: no flow analysis; < 10 Hz: no flow limitation score and no
  cardiogenic oscillations; > 25 Hz: averaged down to 25 Hz. A 20 s moving mean is
  subtracted (leak compensation drift).
- **Breaths**: zero crossings with hysteresis; a breath before a pause ends at the end of
  its expiration.
- **Envelope** E: peak-to-peak flow over about one breath, at 1 Hz. **Baseline** B: 70th
  percentile of E over the previous [120] s, outside unscoreable time.
- **Events**: E ≤ 0.7·B for ≥ [10] s (edges refined on breaths). An **apnea** has ≥ 10 s
  with ≥ [90] % reduction (judged on 2 s windows); anything else with ≥ [30] % is a
  **hypopnea candidate** (its reduction in data2). Reductions longer than [120] s,
  gaps, device leak/artifact spans and a weak signal are **unscoreable**.
- **Flow limitation score** per inspiration: the largest of a flat top, an M-shaped dip and
  an early peak with a plateau, 0–1; a breath is limited from [0.5]. Runs of ≥ 3 limited
  breaths (or ≥ 10 s) outside events are FL spans.
- **RERA-like**: ≥ 2 flow-limited or shrinking breaths (≥ 10 s) ended by a breath ≥ 1.5×
  their mean amplitude, outside events.
- **Periodic breathing**: autocorrelation of E at lags 30–100 s in 600 s windows; ≥ 10 min.
- **Apnea class** (on by default, experimental): evidence sum from cardiogenic oscillations
  during the apnea (−1), limited breaths before (+0.5), an abrupt recovery breath (+0.5), a
  decrescendo before (−0.5), periodic breathing (−0.5) and Prisma's obstruction level
  (±1); ≥ 0.75 obstructive, ≤ −0.75 central, otherwise unclassified.

### Day scoring (stage 2)
- **Hypopnea rule** [Auto]: AASM 3 % — a candidate is confirmed by a desaturation of ≥ 3
  points with its nadir in [start, end + [30] s]; CMS 4 % — ≥ 4 points; Flow only — a
  reduction of ≥ [50] %; Auto — AASM 3 % where SpO2 covers the candidate (≥ 75 % of
  [start, end + 30 s]), Flow only elsewhere. Under AASM/CMS an uncovered candidate is also
  scored by flow only and counted as unconfirmable. Optionally a pulse rise starting in
  [end − 5 s, end + 15 s] also confirms (not AASM). All three counts are always stored.
- **Hypopnea class**: ≥ 50 % of its (≥ 2) scored breaths limited → obstructive; < 20 %
  during periodic breathing → central; otherwise unclassified.
- **Linkage**: a desaturation belongs to an event when its nadir is in [start, end + 30 s];
  counted separately for the analysis' and the device's events. The **hypoxic burden
  (approx.)** is the area of desaturations linked to analysis events over the analysis'
  hours (%·min/h; a simplification of Azarbarzin's method). Desaturations during scoreable
  CPAP time linked to no analysis event are **unexplained**.
- **Pulse response (ΔHR)**: maximum pulse in [start, end + 20 s] minus the mean of the
  10 s before, per event with enough pulse data.
- **Comparison**: device events inside scoreable analysed flow are paired with the
  analysis' apneas, hypopneas and RERAs: intervals widened by 5 s, greedy by the largest
  overlap, first within the same group (apnea / hypopnea / RERA) then across groups (a type
  mismatch). Agreement = matched / (device + analysis − matched).
- **Oximeter clock hint**: with ≥ 10 desaturations and ≥ 10 events, lags of ±20 min in
  5 s steps are tried; if the best lag matches ≥ 1.5× the desaturations of lag 0, at least
  8, and is ≥ 30 s away from 0, the Daily panel suggests it ("Align oximeter…" opens the
  alignment mode with it applied). Only when the SpO2 is not from the CPAP itself.

### Aggregation
Overview and Statistics sum counts and seconds over a period and divide (events per hour
of the analysis' own time), rather than averaging nightly indices.

## Parameters

All of `AnalysisParams` are profile preferences (`AnalysisSettings`, keys `STR_AN_*`),
edited on the Preferences → Analysis tab. Each stage has its own hash (flow, oximetry,
day), so changing a flow parameter does not recalculate oximetry and vice versa. The
"time below" SpO2 thresholds (94, 90, 88, 85, 80 %) are read from the histograms and need
no recalculation.

**Bump `kAnalysisAlgoVersion`** whenever an algorithm changes its results: every stored
result then counts as outdated.

## Tests

`analysissignaltests`, `oxianalyzertests`, `flowanalyzertests`, `flowfeaturetests`,
`apneaclassifiertests`, `eventmatchertests`, `dayscorertests` (synthetic signals from
`tests/analysis_synth`), `analysisintegrationtests` (stage 1 and 2 on a temporary
database, the repository and migration, the service, the report query) and
`analysispaneltests` (sidebar, tab, Statistics figures, Preferences page).

## To check on real nights

- The sign of the flow (inspiration positive) for ResMed and Prisma; Prisma's flow rate.
- ResMed EVE timing: the annotation time is formally the onset, but OSCAR draws it as the
  end. Day scoring logs the median offset of matched apneas from the analysis' event end
  and start; `device_event_conventions.cpp` is where the answer goes.
- Thresholds, on ≥ 30 nights of ResMed and Prisma: apnea agreement, the ratio of the two
  AHIs, FL against ResMed's FLG, OA/CA against the device. If OA/CA agree on fewer than
  70 % of apneas, apnea classification should default to off.
