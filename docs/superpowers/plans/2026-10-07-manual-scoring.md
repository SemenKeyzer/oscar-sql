# Manual Scoring of Respiratory Events Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A doctor adds, removes, retypes apneas/hypopneas and excludes noisy stretches on the Daily flow graph; OSCAR recalculates AHI everywhere while the device's own events stay untouched.

**Architecture:** Edits are a layer stored apart from device data (`manual_scoring`). A pure core (`SleepLib/manual_scoring.*`) turns a session's device events + edits into per-channel count deltas, excluded time and the effective event list; the result is cached per session (`manual_scoring_summary`) so `Day` can adjust counts and the AHI denominator without loading events. Daily gets a scoring mode (drag on Flow Rate → menu, right-click on an event → menu), a drawing layer and a sidebar block.

**Tech Stack:** Qt 6.11, qmake, C++17 (`-Werror`), SQLite via `database/`, QtTest via `tools/runtests.sh`.

**Spec:** `docs/superpowers/specs/2026-10-07-manual-scoring-design.md`

## Global Constraints

- Scored channels: `CPAP_Obstructive` (OA), `CPAP_ClearAirway` (CA), `CPAP_Apnea` (A), `CPAP_Hypopnea` (H); retype only among them.
- An event counts by its end time (device events: `EventList` time; added: `end_ms`). A `remove`/`retype` edit matches a device event of the same channel within ±1000 ms.
- AHI denominator = `hours(MT_CPAP)` minus the union of excluded stretches clipped to sessions. Usage, compliance, pressure, leak, other channels and OSCAR's own analysis are unchanged.
- Device event lists in the database are never modified by this feature.
- Edits are allowed in clinical mode; every night with edits is marked «исправлено вручную».
- Short-event warning threshold: 10 s.
- Schema 21 → 22, migration adds empty tables only (the upgrade prompt is OSCAR's usual one).
- Russian for every new string; `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`; `lrelease` the `.qm`.
- New controls get object names and glossary entries; `UiCoverageTests` stays green.
- Builds `build`, `build-test`, `build-nobt` green; rerun qmake when files are added; after changing `Session`/`Day` layout delete dependent `.o` in all three dirs.
- App check only with `--datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа`, background `app_*` tools only.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; no push without the owner's command.

## Deviations from the spec (decided while planning)

- Besides `manual_scoring` (the edits) a second table `manual_scoring_summary(session_id PK, deltas TEXT, excluded_ms INTEGER, not_found TEXT)` caches the per-session result, because Statistics/Overview read hundreds of days without loading events. The cache is recomputed whenever a session's edits change and whenever a session with edits is (re)imported or rebuilt.

## Review Focus

1. A night whose device events were rebuilt so a `remove`/`retype` target moved by more than 1 s → that edit is reported "not found", the count is the device's → `testNotFoundAfterRebuild` (Task 3).
2. An added event of a type the device never recorded that night (no `m_cnt` entry) still counts → `testAddedTypeAbsentFromDevice` (Task 3).
3. Two edits on the same device event (remove then retype, or retype twice) → the latest wins, no double count → `testLatestEditWins` (Task 1).
4. Excluded stretch covering a whole session or the gap between sessions → never negative hours, AHI of a fully excluded night shows "—"/0 like a night with no hours → `testExcludeWholeNight` (Task 1, Task 4).
5. Disabling a session that has edits (permissive mode) → its edits drop out with it → `testDisabledSessionEditsIgnored` (Task 3).

---

### Task 1: Scoring core

**Files:**
- Create: `oscar/SleepLib/manual_scoring.{h,cpp}`; Modify: `oscar/oscar.pro`
- Test: `oscar/tests/manualscoringtests.{h,cpp}`

**Interfaces:**
- Produces:
  ```cpp
  namespace ManualScoring {
  enum class Kind { Add, Remove, Retype, Exclude };
  struct Edit { qint64 id = 0; SessionID session = 0; Kind kind; ChannelID channel = 0; ChannelID newChannel = 0;
                qint64 startMs = 0, endMs = 0; QString note; QDateTime createdAt; };
  struct DeviceEvent { ChannelID channel; qint64 endMs; double durationSec; };
  enum class Origin { Device, Added, Removed, Retyped };
  struct EffectiveEvent { ChannelID channel; ChannelID originalChannel; qint64 endMs; double durationSec; Origin origin;
                          qint64 editId; bool excluded; };
  struct Result { QHash<ChannelID, int> delta; qint64 excludedMs = 0; QList<qint64> notFound; QList<EffectiveEvent> events; };
  const QList<ChannelID> &scoredChannels();                       // OA, CA, A, H
  Result apply(const QList<DeviceEvent> &device, const QList<Edit> &edits, const QList<QPair<qint64,qint64>> &sessionSpans);
  qint64 kMatchToleranceMs = 1000; double kShortEventSec = 10.0;  // constexpr
  }
  ```
  `delta[c]` = effective count of c − device count of c (excluded events count as removed). `events` lists every device and added event of the scored channels with its origin, for drawing.

- [ ] **Step 1: Failing tests** `ManualScoringTests` (device events: OA@100 s, OA@200 s, CA@300 s, H@400 s; one session 0–3600 s):
  - `testNoEditsNoChange`: all deltas 0, `excludedMs == 0`.
  - `testAdd`: Add H end 500 s → `delta[H] == 1`.
  - `testRemove`: Remove OA@200 s (end 200 400 ms, within tolerance) → `delta[OA] == -1`; Remove OA@250 s → `notFound == {id}`, delta 0.
  - `testRetype`: Retype CA@300 s → OA → `delta[CA] == -1`, `delta[OA] == 1`, sum of deltas 0; event origin `Retyped`, `originalChannel == CA`.
  - `testLatestEditWins`: Remove OA@100 then Retype OA@100 → H (later id) → `delta[OA] == -1`, `delta[H] == 1`.
  - `testExclude`: Exclude 150–350 s → OA@200 and CA@300 excluded → `delta[OA] == -1`, `delta[CA] == -1`, `excludedMs == 200000`.
  - `testExcludeOverlapAndClip`: two sessions 0–1000 s and 2000–3000 s; excludes 900–2100 s and 950–1200 s → `excludedMs == 200000` (100 s + 100 s, gap not counted, overlap once).
  - `testExcludeWholeNight`: exclude −100…4000 s on one session → `excludedMs == 3600000`, never more than session time.
  - `testAddedInsideExcludeNotCounted`: Add H end 200 s + Exclude 150–350 → `delta[H] == 0`.
- [ ] **Step 2: Run** `tools/runtests.sh ManualScoringTests` → FAIL (no header).
- [ ] **Step 3: Implement** `apply`: sort edits by id; resolve each Remove/Retype to the nearest device event of `channel` within tolerance (an event already consumed by a later-id edit is re-resolved, latest wins); union exclude spans, clip to `sessionSpans`; mark events with end in a span excluded.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `Manual scoring: core calculation`.

### Task 2: Storage

**Files:**
- Modify: `oscar/database/database_schema.{h,cpp}` (`CURRENT_SCHEMA_VERSION = 22`, `migrateV21ToV22`, tables in the fresh schema too)
- Create: `oscar/database/manual_scoring_repository.{h,cpp}`; Modify `oscar/oscar.pro`
- Test: `oscar/tests/manualscoringtests.cpp` (DB part, temp DB like `ContecBleImportTests::initTestCase`)

**Interfaces:**
- Produces: `class ManualScoringRepository { static QList<ManualScoring::Edit> editsForSession(SessionID); static QList<ManualScoring::Edit> editsForSessions(const QList<SessionID>&); static qint64 add(const ManualScoring::Edit&); static bool remove(qint64 id); static bool removeAllForSessions(const QList<SessionID>&); static bool storeSummary(SessionID, const ManualScoring::Result&); static bool loadSummary(SessionID, QHash<ChannelID,int> &delta, qint64 &excludedMs, int &notFound); static bool removeSummary(SessionID); };`
- Tables: `manual_scoring(id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(…) ON DELETE CASCADE, kind TEXT, channel INTEGER, new_channel INTEGER, start_ms INTEGER, end_ms INTEGER, note TEXT, created_at TEXT)`, index on `session_id`; `manual_scoring_summary(session_id INTEGER PRIMARY KEY … ON DELETE CASCADE, deltas TEXT /* "ch:n,ch:n" */, excluded_ms INTEGER, not_found INTEGER)`. Use the same `sessions` key column other tables reference.

- [ ] **Step 1: Failing tests** `testStoreAndLoadEdits` (add 4 edits of all kinds, read back equal), `testRemoveEdit`, `testSummaryRoundTrip`, `testSessionDeleteCascades` (delete the session row → both tables empty for it), `testMigration21To22` (a v21 DB file upgrades, tables exist, version 22).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** repository + migration (follow `migrateV20ToV21` style).
- [ ] **Step 4: Run** `tools/runtests.sh ManualScoringTests` → PASS; full suite green (other schema tests may pin 21 → update them, ledger it).
- [ ] **Step 5: Commit** `Manual scoring: storage`.

### Task 3: Session and Day

**Files:**
- Modify: `oscar/SleepLib/session.{h,cpp}`, `oscar/SleepLib/day.{h,cpp}`, `oscar/SleepLib/manual_scoring.{h,cpp}`
- Test: `oscar/tests/manualscoringtests.cpp`

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces:
  - `Session`: `const QHash<ChannelID,int> &manualDelta() const; qint64 manualExcludedMs() const; int manualNotFound() const; bool hasManualScoring() const;` loaded with the session's summary (`LoadSummary` path), defaults empty.
  - `ManualScoring::refresh(Session *s)` — loads edits, needs events loaded (loads them if not), runs `apply`, stores/removes the summary, updates the session's fields. `ManualScoring::addEdit(Session*, Edit)`, `removeEdit(Session*, qint64 id)`, `clearDay(Day*)` — each saves then `refresh`.
  - `Day`: `count(code)` adds `manualDelta()[code]` of enabled sessions for the scored channels (also when the device has no `m_cnt` entry for the code); `double ahiHours()` = `hours(MT_CPAP)` − Σ enabled sessions' `manualExcludedMs()`/3.6e6, never below 0; `calcAHI/calcOAHI/calcCAHI/calcRDI` divide by `ahiHours()`; `bool hasManualScoring()`; `EventDataType deviceAHI()` (no edits, `hours(MT_CPAP)`).
  - Hook: after a session's events are (re)stored on import/rebuild (`Session::StoreToDatabase` when `hasManualScoring()` or edits exist), call `ManualScoring::refresh`.

- [ ] **Step 1: Failing tests** on a synthetic Day/Session (see `analysis_synth` helpers): `testDayCountsEdits` (Day::count and calcAHI move by the deltas; `deviceAHI()` unchanged), `testAddedTypeAbsentFromDevice`, `testAhiHoursSubtractsExcluded` (calcAHI uses reduced hours, `hours(MT_CPAP)` and usage unchanged), `testDisabledSessionEditsIgnored`, `testNotFoundAfterRebuild` (store a Remove, shift the device event by 5 s, refresh → `manualNotFound()==1`, count = device), `testClearDayRestoresDevice`, `testEditsSurviveReopen` (drop and reload the session from the DB).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.** Delete the dependent `.o` files (session.h/day.h consumers) in all three build dirs before building.
- [ ] **Step 4: Run** `tools/runtests.sh` → all green.
- [ ] **Step 5: Commit** `Manual scoring: sessions and days count the edits`.

### Task 4: One AHI everywhere

**Files:**
- Modify: `oscar/statistics.cpp` (AHI denominators at ~633–860 use a new `rx.ahiHours`; settings comparison ~1748/1844 uses `ahiHours()`; `calcAHI(QDate,QDate)` ~1291), `oscar/SleepLib/profiles.cpp` (`Profile::calcCount`/AHI range helpers if they compute AHI), `oscar/welcome.cpp:265`, `oscar/daily.cpp:1969` (`getAHI`), `oscar/nightsummary.cpp` (uses `calcAHI`, fine), `oscar/Graphs/gAHIChart.cpp` (per-day AHI), `oscar/exports/exportcsv.cpp:294` (session AHI), `oscar/Graphs/gdailysummary.cpp`.
- Test: `oscar/tests/manualscoringtests.cpp` or `analysisintegrationtests.cpp`

- [ ] **Step 1: Failing test** `testAhiSameEverywhere`: one profile day with edits (add 2, exclude 30 min) → equal values from `Day::calcAHI`, `calcAHI(date,date)`, the Statistics AHI row for that night, `NightSummaryView` data `ahi`, settings-comparison row AHI, `gAHIChart` value for the day; and `testExcludeWholeNight` there gives 0 / "—" without division by zero.
- [ ] **Step 2: Run** → FAIL on the consumers still dividing by `hours(MT_CPAP)`.
- [ ] **Step 3: Implement**: every AHI/RDI/OAHI/CAHI denominator uses `ahiHours()` (or the summed `ahiHours` of the period); usage/compliance keep `hours(MT_CPAP)`.
- [ ] **Step 4: Run** full suite → green.
- [ ] **Step 5: Commit** `Manual scoring: one corrected AHI on every page`.

### Task 5: Scoring mode on the graphs

**Files:**
- Modify: `oscar/Graphs/gGraphView.{h,cpp}`, `oscar/Graphs/gGraph.cpp` (`mouseReleaseEvent` ~1049, right-click ~961/1150)
- Test: `oscar/tests/scoringmodetests.{h,cpp}`

**Interfaces:**
- Produces on `gGraphView`: `void setScoringMode(bool)`, `bool scoringMode() const`, signals `scoringRangeSelected(gGraph *graph, qint64 startMs, qint64 endMs, QPoint globalPos)` and `scoringContextRequested(gGraph *graph, qint64 timeMs, QPoint globalPos)`; key `Esc` in scoring mode emits `scoringModeExitRequested()`.
- Rule: in scoring mode, a left drag on the graph named `STR_GRAPH_FlowRate` emits `scoringRangeSelected` and does **not** change X bounds; a right click inside the plot of the Flow Rate or Event Flags (`STR_GRAPH_SleepFlags`) graph emits `scoringContextRequested` and does not zoom out. Everything else unchanged; out of scoring mode nothing changes.

- [ ] **Step 1: Failing tests**: `testDragSelectsInScoringMode` (bounds unchanged, signal with ms range), `testDragZoomsOutsideScoringMode` (no signal, bounds changed), `testRightClickRequestsMenu`, `testOtherGraphsUnaffected`, `testEscRequestsExit`.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** → PASS + full suite.
- [ ] **Step 5: Commit** `Graphs: scoring mode`.

### Task 6: Daily — mode, menus, sidebar

**Files:**
- Create: `oscar/scoringmenus.{h,cpp}` (builds the menus; testable without Daily)
- Modify: `oscar/daily.{h,cpp}` (button `scoringButton` next to `alignButton`, banner label `scoringBanner`, slots for the graph signals, sidebar), `oscar/oscar.pro`
- Test: `oscar/tests/scoringmenustests.{h,cpp}`

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces: `namespace ScoringMenus { QMenu *forRange(qint64 durationMs, QWidget *parent); QMenu *forEvent(const ManualScoring::EffectiveEvent &e, QWidget *parent); QMenu *forExcluded(qint64 editId, QWidget *parent); }` — actions carry `data()` = `QVariantMap{ "kind", "channel", "editId" }`.
- Menu texts (English sources): range — title "Selected: %1 s"; "Obstructive apnea", "Central apnea", "Apnea (unclassified)", "Hypopnea", separator, "Exclude stretch (noise / awake)"; when `durationMs < 10000` each event item gets the suffix " — shorter than 10 s". Event — "Remove event (do not count)", submenu "Change type" with the three other types; for `Added`/`Removed`/`Retyped` origin also "Undo this change". Excluded — "Cancel exclusion".
- Sidebar (`getAHI` + new block): AHI line "AHI %1 (device %2) · corrected by hand" when `day->hasManualScoring()`; block "Manual scoring": "added %1, removed %2, type changed %3, excluded %4 min (%5 stretches)", one row per edit with time (link `scoring=jump:<editId>` and `scoring=undo:<editId>`), "event not found" rows for `notFound`, and link "Undo all scoring of this night" (`scoring=clear`, confirm dialog).

- [ ] **Step 1: Failing tests** `ScoringMenusTests`: item texts and `data()` for each menu; short-event suffix at 9.9 s and not at 10 s; "Undo this change" only for edited origins.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** menus and Daily wiring: button toggles `GraphView->setScoringMode`, shows the banner, Esc/toggle exits; a chosen action calls `ManualScoring::addEdit/removeEdit`, then reloads the day (`LoadDate`) and refreshes Overview/Statistics like `rejectToggleSessionEnable` does (daily.cpp ~802–824). Unlike session toggling, scoring is **not** blocked in clinical mode (test `testScoringAllowedInClinicalMode` on `Daily`-free logic: `ManualScoring::addEdit` works with `clinicalMode() == true`).
- [ ] **Step 4: Run** → PASS + full suite; build all three dirs.
- [ ] **Step 5: Commit** `Daily: manual scoring mode, menus and sidebar`.

### Task 7: Drawing the edits

**Files:**
- Create: `oscar/Graphs/gManualScoringLayer.{h,cpp}`; Modify `oscar/daily.cpp` (add the layer to every Daily graph), `oscar/oscar.pro`
- Test: `oscar/tests/scoringmodetests.cpp`

**Interfaces:**
- Produces: `class gManualScoringLayer : public Layer { void setResult(const ManualScoring::Result&, QList<QPair<qint64,qint64>> excludedSpans); bool drawsMarkers; }` — excluded spans: grey hatch (`Qt::BDiagPattern`, alpha ~60) on every graph; markers (only on Flow Rate and Event Flags): Added — dashed outline in the channel colour + "ручн."/"manual" text (`tr("manual")`); Removed — pale overlay + strike line; Retyped — new colour + "was %1" (channel short label).
- [ ] **Step 1: Failing test** `testLayerItems`: `gManualScoringLayer::items()` (a pure helper returning the list of drawn items with kind/time/label for the visible range) for a Result with one of each origin and one exclude.
- [ ] **Step 2: Run** → FAIL.  **Step 3: Implement.**  **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `Graphs: draw manual scoring`.

### Task 8: Marks on other pages and the PDF

**Files:** `oscar/statistics.cpp` (footnote "* %n night(s) corrected by hand" under the table when the shown period contains edited nights), `oscar/nightsummary.cpp` (AHI tile note "corrected by hand"), `oscar/Graphs/gAHIChart.cpp` or the Overview AHI tooltip ("corrected by hand"), `oscar/pdfreportwriter.cpp` / `oscar/doctorreport.cpp` (line "AHI by the device: %1" when the period has edits).
- [ ] **Step 1: Failing tests**: `testStatisticsFootnote`, `testNightTileMark`, `testPdfShowsDeviceAhi` (in their existing test classes).
- [ ] **Step 2–4:** run → FAIL, implement, run → PASS + full suite.
- [ ] **Step 5: Commit** `Manual scoring: marks on other pages and the PDF`.

### Task 9: Explanations, translation, app check, handoff

- [ ] **Step 1:** Glossary entries: `ui.daily.scoringButton` (caution: none), a stage-1 term `manual_scoring` ("Manual scoring", summary/details/no norm) linked from the sidebar block and the AHI line; `UiCoverageTests` covers `scoringButton` (add to the code-made list).
- [ ] **Step 2:** `lupdate`, Russian for every new string, `validate_ts.py` → `TOTAL 0`, `lrelease`.
- [ ] **Step 3:** Full suite, build all three dirs.
- [ ] **Step 4: App check** (background only): Daily of a dev night, toggle «Разметка» via `app_click`, screenshot shows the banner and the sidebar block; ask the owner to drag on the flow graph and right-click an event.
- [ ] **Step 5:** Handoff section 4.9; commits `Translations: manual scoring` and `Handoff: manual scoring (fork-only, not for MR)`.
