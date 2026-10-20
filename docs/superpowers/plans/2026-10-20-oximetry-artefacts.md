# Oximetry Artefacts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Fork-only, not for MR.

**Goal:** Detect oximeter artefacts automatically, show them, leave them out of the oximetry analysis, and let the user add («Артефакт») or cancel («Не артефакт») them by hand.

**Architecture:** `analyzeOximetry` finds artefact spans on the raw 1 Hz grids, merges them with the day's manual overrides, masks them out and reports them in `OxiResult::artifacts`; everything downstream (desaturations, histogram, burden, zones, hypopnea confirmation) then sees the masked grids. Stage 1 writes the automatic spans as a new analysis channel `AN_OxiArtifact`; the day scoring gets the manual Exclude/Keep stretches through `DayInput::oxiOverrides`, which also enter the day's inputs hash. Manual Exclude/Keep stretches are stored on oximeter sessions too, so they work on nights without CPAP.

**Tech Stack:** C++17, Qt 6, qmake; QtTest via `../tools/runtests.sh`.

**Spec:** `docs/superpowers/specs/2026-10-16-oximetry-artefacts-design.md`

## Global Constraints

- Thresholds (constants in `oxi_analyzer.cpp`, not settings): R3 fall >= 4 points within <= 2 s, back within 2 points of the pre-fall level within 30 s; R4 SpO2 fall >= 3 points starting within 5 s of a pulse jump >= 30 bpm within 2 s or of a pulse gap; R5 SpO2 and pulse both exactly constant >= 3 min; R6 edge stretch before the first / after the last 60 s with SpO2 range <= 3 points, at most 5 min.
- «Не артефакт» cancels only R3–R6 (and nothing manual); R1, R2, R7 always stay excluded.
- `kAnalysisAlgoVersion` 10; the oximetry stage-1 version is bumped once.
- New channel `AN_OxiArtifact` = 0x1A26, SPAN, MT_OXIMETER, code "AnOxiArtifact", label «aAR», grey `QColor(0x90, 0x90, 0x90)`.
- Manual kind `Kind::Keep`, stored as "keep" (`manual_scoring.kind` is TEXT: no migration).
- Translations: lupdate → fill → `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts --unfinished` → TOTAL 0 → lrelease.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Re-run qmake only after a new file has content.

## Review Focus

1. A night with only an oximeter: a manual «Артефакт» / «Не артефакт» must be stored and used (there is no CPAP session) — test in Task 4.
2. SpO2 with no pulse at all: R4's "pulse gap" must not flag every fall — test in Task 1.
3. A short recording (never 60 s stable): R6 trims at most 5 min at each end, never the whole record — test in Task 1.
4. An oximeter with a clock correction: manual stretches (device time) must line up with the shifted SpO2 grid — test in Task 4.
5. A «Не артефакт» stretch over a 10-point spike: the spike stays excluded — test in Task 2.

---

### Task 1: Automatic artefact detection and masking

**Files:**
- Modify: `oscar/SleepLib/analysis/oxi_analyzer.h`, `oscar/SleepLib/analysis/oxi_analyzer.cpp`
- Test: `oscar/tests/oxianalyzertests.{h,cpp}`

**Interfaces:**
- Produces:
  - `enum class OxiArtifactReason { OutOfRange, Spike, FastFall, PulseDisturbed, Frozen, ProbeOnOff, ShortFragment, Manual };`
  - `struct OxiArtifact { qint64 start = 0; qint64 end = 0; OxiArtifactReason reason; };`
  - `QVector<OxiArtifact> findOxiArtifacts(const Grid &spo2Raw, const Grid &pulseRaw);` — R1–R7, merged: overlapping or touching spans become one, with the reason of the longest contributing span; sorted by start.
  - `void maskArtifacts(Grid &g, const QVector<OxiArtifact> &artifacts);` — sets the covered seconds to kNoData.
  - `OxiResult::artifacts` (`QVector<OxiArtifact>`) and `int OxiResult::artifactSeconds() const`.
  - `analyzeOximetry` masks SpO2 with every artefact and pulse with OutOfRange/Frozen/ProbeOnOff ones before its existing cleaning.

- [ ] **Step 1: Write the failing tests** (synthetic grids from `tests/analysis_synth.h`, pulse `synth::flat(n, 60)` with ±1 bpm wobble where a varying pulse is needed)
  - `testFastFallIsArtifact`: 96 → 91 in 1 s, back at 96 after 10 s → one artefact FastFall covering it; `countDesaturations(3) == 0`. Guard: the same 5-point dip with `fall` 15 s → no artefact, one desaturation.
  - `testFallWithPulseJumpIsArtifact`: 4-point dip over 5 s whose start coincides with pulse 60 → 95 within 1 s → PulseDisturbed. Guard: pulse 60 → 75 over 8 s → no artefact.
  - `testSpo2WithoutPulseHasNoPulseArtifacts`: dips with an empty pulse grid → no PulseDisturbed artefacts (Review Focus 2).
  - `testFrozenSignalIsArtifact`: SpO2 95 and pulse 60 exactly constant 4 min inside a varying night → Frozen covering those 4 min. Guard: SpO2 constant 10 min with a wobbling pulse → none.
  - `testProbeOnOffTrimmed`: first 90 s jumping 80–99, then stable → ProbeOnOff from the start to about 90 s. `testProbeOnOffAtMostFiveMinutes`: a 20-minute record never stable → artefacts at each end of exactly 300 s, the middle kept (Review Focus 3).
  - `testBasicCleaningIsReported`: a value 40 → OutOfRange; a 12-point spike back in 3 s → Spike; both counted in `artifactSeconds()`.
  - `testArtifactsLeaveTheFigures`: a night with one FastFall: `spo2Seconds` equals the night's seconds minus `artifactSeconds()`, and the SpO2 histogram has no seconds from the fall.

- [ ] **Step 2: Run** `../tools/runtests.sh OxiAnalyzerTests` — Expected: build fails on the missing names.

- [ ] **Step 3: Implement** the types and functions above. R1/R2/R7 reuse `keepRange` / `removeSpikes` / `dropShortSegments` on a copy and report the seconds they remove; R7 is computed after all other masks.

- [ ] **Step 4: Run** `../tools/runtests.sh OxiAnalyzerTests` then the whole suite — Expected: all pass (existing oximetry tests unchanged).

- [ ] **Step 5: Commit** "Oximetry analysis: detect, report and leave out oximeter artefacts".

### Task 2: Manual overrides in the oximetry analysis and the day scoring

**Files:**
- Modify: `oscar/SleepLib/analysis/oxi_analyzer.{h,cpp}`, `oscar/SleepLib/analysis/day_scorer.{h,cpp}`
- Test: `oscar/tests/oxianalyzertests.cpp`, `oscar/tests/dayscorertests.{h,cpp}`

**Interfaces:**
- Consumes: Task 1's `findOxiArtifacts`, `maskArtifacts`, `OxiArtifact`.
- Produces:
  - `struct OxiOverrides { QVector<Span> exclude; QVector<Span> keep; };`
  - `OxiResult analyzeOximetry(const Grid &spo2, const Grid &pulse, const OxiParams &params, const OxiOverrides &overrides = OxiOverrides());` — drops automatic FastFall/PulseDisturbed/Frozen/ProbeOnOff artefacts that overlap a keep span; adds each exclude span as reason Manual; then merges and masks as in Task 1.
  - `DayInput::oxiOverrides` (`OxiOverrides`, in the same corrected time as `DayInput::spo2`); `scoreDay` passes it and builds `cleanedSpo2` / `cleanedPulse` from the masked grids.

- [ ] **Step 1: Failing tests**
  - `testManualExcludeRemovesDesaturation` (OxiAnalyzerTests): a real 5-point desaturation inside an exclude span → no desaturation, one Manual artefact.
  - `testKeepCancelsAutomaticArtifact`: the Task 1 fast fall with a keep span over it → no artefact, `spo2Seconds` back to the whole night.
  - `testKeepDoesNotCancelSpike`: a 12-point spike under a keep span → still a Spike artefact (Review Focus 5).
  - `testDayExcludeLeavesLinkedBurden` (DayScorerTests, on its `cpapNight`-style fixture): an exclude span over a linked desaturation → `r.linkedDesatArea` and `r.oxi.countDesaturations(3)` drop by that desaturation.
- [ ] **Step 2: Run** `../tools/runtests.sh OxiAnalyzerTests DayScorerTests` — Expected: build fails / FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** both classes, then the whole suite — Expected: pass.
- [ ] **Step 5: Commit** "Oximetry analysis: manual Exclude and Keep stretches".

### Task 3: Stage-1 channel and graphs

**Files:**
- Modify: `oscar/SleepLib/analysis/analysis_channels.{h,cpp}`, `oscar/SleepLib/analysis/session_analysis.cpp` (`runOximetry`), the oximetry stage-1 version constant, `oscar/Graphs/gLineChart.cpp:235`
- Test: the test class that already checks `AN_OxiProblemZone` after a session's stage 1 (`grep -l AN_OxiProblemZone oscar/tests`)

**Interfaces:**
- Consumes: `OxiResult::artifacts`.
- Produces: `AN_OxiArtifact` (values from Global Constraints), in the oximetry channel list next to `AN_OxiProblemZone`; each span's value is `int(reason)`.

- [ ] **Step 1: Failing test** `testStageOneWritesOxiArtifacts`: a session whose SpO2 has a Task 1 fast fall → after stage 1 the session has one `AN_OxiArtifact` span over it with value `int(OxiArtifactReason::FastFall)`; `isAnalysisChannel(AN_OxiArtifact)` is true; its name ends with " · analysis" like the other analysis channels.
- [ ] **Step 2: Run** — Expected: build fails.
- [ ] **Step 3: Implement**; bump the oximetry stage-1 version so stored sessions are redone; `gLineChart` draws `AN_OxiArtifact` spans on the SpO2 graph the way it draws `AN_Desaturation` there.
- [ ] **Step 4: Run** the class and the whole suite — Expected: pass.
- [ ] **Step 5: Commit** "Analysis channel for oximeter artefacts (aAR)".

### Task 4: Storing Keep, overrides from the day, hash and version

**Files:**
- Modify: `oscar/SleepLib/manual_scoring.{h,cpp}`, `oscar/database/manual_scoring_repository.cpp`, `oscar/SleepLib/analysis/day_analysis.cpp` (`buildDayInput`, `dayInputsHash`), `oscar/SleepLib/analysis/analysis_params.h`, `oscar/daily.cpp` (`onScoringChoice` exclude branch, `getManualScoring`, `scoringSessionAt` unchanged), `oscar/scoringmenus.cpp` (`editRows`)
- Test: `oscar/tests/manualscoringtests.{h,cpp}`, `oscar/tests/analysisintegrationtests.{h,cpp}`

**Interfaces:**
- Consumes: Task 2's `OxiOverrides`, `DayInput::oxiOverrides`.
- Produces:
  - `Kind::Keep` ("keep"); ignored by `apply()` (like Mark) and not a "corrected" state.
  - `OxiOverrides ManualScoring::oximetryOverrides(Day *day);` — Exclude and Keep stretches of every enabled CPAP or oximeter session of the day, each shifted by its session's `correctionMs()`, duplicates (same stretch on several sessions) once.
  - `ManualScoring::isStretchSession(Session *s)` — enabled and MT_CPAP or MT_OXIMETER; the exclude/keep branch in Daily adds the edit to every such session the stretch touches.
  - `dayInputsHash` appends the overrides; `kAnalysisAlgoVersion` 10.

- [ ] **Step 1: Failing tests**
  - `testKeepRoundTrip` (ManualScoringTests): a Keep edit saved and read back has `Kind::Keep`; `apply()` ignores it.
  - `testOverridesFromOximeterSession`: a day with only an oximeter session holding an Exclude and a Keep → `oximetryOverrides(day)` returns both (Review Focus 1).
  - `testOverridesFollowClockCorrection`: the oximeter session with `correctionMs` 95 000 → the spans are shifted by 95 000 (Review Focus 4).
  - `testInputsHashChangesWithOverrides` (AnalysisIntegrationTests): adding an Exclude stretch to a day changes `dayInputsHash`.
- [ ] **Step 2: Run** `../tools/runtests.sh ManualScoringTests AnalysisIntegrationTests` — Expected: build fails / FAIL.
- [ ] **Step 3: Implement**, including every `switch (kind)` the compiler flags (`-Wswitch`): the manual list shows a Keep stretch as «не артефакт: N s» with the same ✕ removal as an excluded one; `editRows` includes Exclude/Keep edits of oximeter sessions.
- [ ] **Step 4: Run** the classes, then the whole suite — Expected: pass.
- [ ] **Step 5: Commit** "Manual Keep stretches; oximetry overrides from CPAP and oximeter sessions".

### Task 5: Panel, tab, menus, help, translations

**Files:**
- Modify: `oscar/analysispanel.{h,cpp}`, `oscar/scoringmenus.{h,cpp}`, `oscar/daily.cpp`, `oscar/glossary.cpp`, `Translations/Russkiy.ru.ts`, `oscar/translations/Russkiy.ru.qm`
- Test: `oscar/tests/analysispaneltests.cpp`, `oscar/tests/scoringmenustests.cpp`, `oscar/tests/glossarytests.cpp`

**Interfaces:**
- Consumes: `OxiResult::artifacts`, `artifactSeconds()`, `Kind::Keep`, `isStretchSession`.
- Produces:
  - Sidebar Oximetry block: row «Excluded as artifact» = «N min (M stretches)», hover help (term key "oxi_artifact") listing minutes per reason; absent when there are none.
  - `AnalysisTab`: group «Oximeter artifacts» with one item per artefact («hh:mm:ss, N s, reason»), click shows it; context menu «Not an artifact» emits `void keepRequested(qint64 from, qint64 to)` (corrected times); Daily connects it to the same path as the scoring menu's keep action.
  - `ScoringMenus::forRange`: «Not an artifact» after «Artifact», key 'K' (and its Russian-layout letter in `installLetterKeys`), data `data("keep")`.
  - Glossary entry `oxi_artifact`: what counts as an artefact (the seven rules in plain words), that the figures leave them out, how to undo one.
- [ ] **Step 1: Failing tests**
  - `testSidebarShowsExcludedArtifacts` (AnalysisPanelTests): oximetry night with one fast fall → html has «Excluded as artifact»; `oximetryNight()` without artefacts → absent.
  - `testTabListsArtifacts`: `setResult` with one artefact → a group «Oximeter artifacts (1)».
  - `testRangeMenuHasKeep` (ScoringMenusTests): `forRange` has an action with data action "keep" and shortcut letter K.
  - Glossary key list in `glossarytests.cpp` gains "oxi_artifact".
- [ ] **Step 2: Run** the three classes — Expected: FAIL.
- [ ] **Step 3: Implement**; lupdate, translate every new string, validate (TOTAL 0), lrelease.
- [ ] **Step 4: Run** the whole suite — Expected: all pass; build `../build`.
- [ ] **Step 5: Commit** "Show oximeter artefacts, «Не артефакт» in the tab and the scoring menu".

Then: final review of the whole range (opus subagent), fixes RED→GREEN, owner check on a Contec and an O2Ring night.
