# Manual scoring: dragging event and exclusion edges — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** In manual scoring mode, the edges of event boxes and of excluded stretches on the Daily flow graph can be dragged to change their bounds; the box shows the type at top left and the duration at top right.

**Architecture:** A new edit kind `Resize` (with the device event's original end as `matchEndMs`, new column `match_end_ms`, schema 23) moves a device event's bounds; added events and excluded stretches have their own edit row updated. A pure `ScoringResize` module does the hit test and clamping; `gManualScoringLayer` takes the mouse in scoring mode and draws the live drag; `gGraphView` emits `scoringResized`; `Daily` turns it into an edit.

**Tech Stack:** Qt 6.11, qmake, C++17 with `-Werror`, QtTest (one `test` binary in `../build-test`), SQLite.

**Spec:** `docs/superpowers/specs/2026-10-07-scoring-resize-design.md`

## Global Constraints

- Only in scoring mode, only on the flow graph (`STR_GRAPH_FlowRate`); outside scoring mode graph behaviour is unchanged.
- Event edges grabbable only when boxes show (`gManualScoringLayer::showsBoxes`, range ≤ `kBoxRangeMs` = 20 min); excluded-stretch edges at any zoom.
- Grab distance 4 px (`ScoringResize::kGrabPx = 4`); minimum length 1 s (`ScoringResize::kMinMs = 1000`); an edge stays within its session (event) or the day's sessions (stretch); left edge never passes right edge minus 1 s.
- Removed events are not grabbable; nearest edge wins, a tie goes to the event.
- Esc during a drag cancels it with no edit (and does not leave scoring mode).
- One `Resize` edit per device event: dragging again updates it.
- Edits are stored in device time; results and the UI work in graph time (+ `Session::correctionMs()`), as in parts 1–2.
- Schema 22 → 23 adds `manual_scoring.match_end_ms INTEGER`; backup/restore carry it.
- Edit-list row text: `"%1: %2 → %3 s"` (type label, old and new duration, one decimal); summary adds "bounds changed N".
- Every new string translated in `Translations/Russkiy.ru.ts`; `validate_ts.py` → `TOTAL 0`; `UiCoverageTests`/glossary tests pass.
- Build checks: `make -j10` in `../build-test` must succeed (check its exit code) before running `./test`; then `../tools/runtests.sh`.
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A device event both retyped and resized (either order) — both edits must find it by original type and original end; undoing one keeps the other.
2. A resized event whose new end lands in an excluded stretch (and back out) — counted/not counted follows the new end.
3. An excluded stretch stored in two sessions (crossing a session boundary) — dragging its edge updates every row of that stretch.
4. A night with a time correction — the drag result (graph time) is stored in device time; reopening shows the same bounds.
5. Esc or leaving scoring mode mid-drag — no edit is stored, the box returns to its bounds.

---

### Task 1: Core — the Resize edit in `apply`

**Files:**
- Modify: `oscar/SleepLib/manual_scoring.h`, `oscar/SleepLib/manual_scoring.cpp`
- Test: `oscar/tests/manualscoringtests.{h,cpp}`

**Interfaces:**
- Produces:
  - `enum class Kind { Add, Remove, Retype, Exclude, Resize };`
  - `Edit::matchEndMs` (`qint64`, 0 unless Resize): the device event's original end.
  - `EffectiveEvent` gains `qint64 originalEndMs; double originalDurationSec; qint64 resizeEditId;` (0 when not resized). `endMs`/`durationSec` are the resized values.
  - `struct ExcludeEdit { qint64 editId; qint64 startMs, endMs; };` and `Result::excludeEdits` (each Exclude edit's own span, unmerged).
  - Remove/Retype/Resize match the device event by `originalChannel` and `originalEndMs` (±`kMatchToleranceMs`); Remove/Retype use `e.endMs`, Resize uses `e.matchEndMs`.

- [ ] **Step 1: Failing tests** (`edit()` helper gains an optional `matchEndMs` or set it on the returned Edit)
  - `testResize`: device OA ending at 600 s, 12 s long; Resize edit {channel OA, start 590 s, end 606 s, matchEnd 600 s} → the event's `endMs` = 606 000, `durationSec` = 16.0, `originalEndMs` = 600 000, `originalDurationSec` = 12.0, `resizeEditId` = edit id; delta empty.
  - `testResizeIntoExclude`: Exclude 605–700 s plus the Resize above → OA not counted (delta OA = −1); Resize end back to 604 s → counted.
  - `testResizeAndRetype`: Retype OA→H then Resize (ids ascending), and Resize then Retype → in both, one event, channel H, end 606 s.
  - `testResizeNotFound`: Resize with matchEnd 900 s (no event) → its id in `notFound`.
  - `testExcludeEditsListed`: two overlapping Exclude edits → `excludeEdits` has both with their own spans; `excludedSpans` merged as before.
- [ ] **Step 2: Build and run** — `make -j10` in `../build-test` (exit 0), `./test > run.log 2>&1`; Expected: the five tests FAIL (compile error for `Kind::Resize` counts as the failing step; then fail on values).
- [ ] **Step 3: Implement** in `apply`: keep `originalEndMs`/`originalDurationSec` on every event; match on them; Resize sets `endMs = e.endMs`, `durationSec = (e.endMs - e.startMs)/1000.0`, `resizeEditId`; collect `excludeEdits`. `resultFor` shifts `excludeEdits` and `originalEndMs` by the correction like the other times.
- [ ] **Step 4: Run** — all ManualScoringTests PASS, suite green (`../tools/runtests.sh`).
- [ ] **Step 5: Commit** — "Manual scoring: the Resize edit (new bounds of a device event)".

### Task 2: Storage — schema 23, repository, edit helpers

**Files:**
- Modify: `oscar/database/database_schema.{h,cpp}` (CURRENT_SCHEMA_VERSION 23, `migrateV22ToV23`, column in `createManualScoringTables`), `oscar/database/manual_scoring_repository.{h,cpp}`, `oscar/SleepLib/manual_scoring.{h,cpp}`, backup/restore only if the round-trip test fails.
- Test: `oscar/tests/manualscoringtests.{h,cpp}`, existing schema-version assertions (grep `22` in tests for schema checks and update to 23).

**Interfaces:**
- Consumes: Task 1 `Kind::Resize`, `Edit::matchEndMs`, `EffectiveEvent::{originalEndMs, resizeEditId}`, `Result::excludeEdits`.
- Produces:
  - `static bool ManualScoringRepository::update(qint64 id, qint64 startMs, qint64 endMs);` (device time; drops nothing else)
  - kind name `"resize"`; `add()` writes and `editsForSession()` reads `match_end_ms`.
  - `bool ManualScoring::resizeEvent(Session *s, const EffectiveEvent &ev, qint64 startMs, qint64 endMs);` — graph time in; updates `ev.resizeEditId` if set, else adds a Resize edit with `matchEndMs = ev.originalEndMs` (all shifted to device time); refreshes the session.
  - `bool ManualScoring::updateEdit(Day *day, qint64 id, qint64 startMs, qint64 endMs);` — graph time in; for an Add edit updates it; for an Exclude edit updates every Exclude edit of the day's sessions with the same stored span; refreshes the touched sessions and stores the day summary.

- [ ] **Step 1: Failing tests**
  - `testMigration22To23`: a v22 table without the column → migration adds `match_end_ms`, version 23, rows kept.
  - `testStoreResize`: add a Resize edit with matchEnd → `editsForSession` returns kind Resize and the same `matchEndMs`.
  - `testResizeEventUpdatesSameEdit`: on a real session (existing fixtures in this file) `resizeEvent` twice on the same event → one Resize edit, second bounds stored.
  - `testResizeEventCorrection`: a session with `correctionMs` 60 000 → stored start/end/matchEnd are 60 s earlier than the graph times passed in.
  - `testUpdateExcludeAcrossSessions`: one stretch stored in two sessions → `updateEdit` with one id moves both rows.
  - `testUpdateAdded`: `updateEdit` on an Add edit changes its span; count unchanged.
  - Backup round trip (`backuprestoretests`): a Resize edit's `match_end_ms` survives backup → restore.
- [ ] **Step 2: Build (exit 0) and run** — Expected: the new tests FAIL.
- [ ] **Step 3: Implement** the migration (`ALTER TABLE manual_scoring ADD COLUMN match_end_ms INTEGER` only when missing, in a transaction, `setSchemaVersion(db, 23)`, registered as `{ 22, &migrateV22ToV23 }`), the column in CREATE, the repository and the two helpers.
- [ ] **Step 4: Run** — suite green.
- [ ] **Step 5: Commit** — "Manual scoring: store new bounds (schema 23) and update added events and stretches".

### Task 3: Hit test and clamping — `ScoringResize`

**Files:**
- Create: `oscar/Graphs/scoringresize.{h,cpp}` (add to `oscar/oscar.pro` SOURCES/HEADERS; re-run qmake in all three build dirs)
- Test: `oscar/tests/scoringmodetests.{h,cpp}`

**Interfaces:**
- Consumes: Task 1 `Result`, `EffectiveEvent`, `ExcludeEdit`.
- Produces (namespace `ScoringResize`):
  - `constexpr int kGrabPx = 4; constexpr qint64 kMinMs = 1000;`
  - `struct Target { enum Kind { None, Event, Excluded }; Kind kind = None; int eventIndex = -1; qint64 editId = 0; bool leftEdge = false; qint64 startMs = 0, endMs = 0; };`
  - `Target hit(const ManualScoring::Result &r, qint64 minX, qint64 maxX, int plotWidth, int x, bool boxesShown);` — x in plot pixels; events only when `boxesShown`, never Removed, never device events inside an exclusion (they draw no box); nearest edge within `kGrabPx`; tie → Event.
  - `QPair<qint64, qint64> dragTo(const Target &t, qint64 timeMs, qint64 lowMs, qint64 highMs);` — moves the grabbed edge to `timeMs` clamped to [lowMs, highMs] and to keep length ≥ `kMinMs`.

- [ ] **Step 1: Failing tests** `testResizeHit`, `testResizeHitTieAndRemoved`, `testResizeDragClamp`: 600 px plot over 0–600 s (1 px = 1 s): event 588–600 s → x 600 hits right edge, x 604 hits, x 605 misses; boxes off → event missed, exclusion 100–200 s still hit at x 100 (left edge); Removed event at the same place → not hit; tie between an exclusion edge and an event edge at the same x → Event. `dragTo` right edge to 590 s with start 588 s → end 589 s (1 s minimum); left edge to −50 s with low 0 → start 0; right edge beyond high → high.
- [ ] **Step 2: Build (exit 0), run** — FAIL (undefined).
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — PASS, suite green.
- [ ] **Step 5: Commit** — "Scoring mode: hit test and clamping for dragging edges".

### Task 4: Dragging on the graph, labels, Daily, edit list

**Files:**
- Modify: `oscar/Graphs/gManualScoringLayer.{h,cpp}`, `oscar/Graphs/gGraphView.{h,cpp}`, `oscar/daily.{h,cpp}`, `oscar/scoringmenus.{h,cpp}`
- Test: `oscar/tests/scoringmodetests.{h,cpp}`, `oscar/tests/scoringmenustests.{h,cpp}`

**Interfaces:**
- Consumes: Task 2 `resizeEvent`, `updateEdit`; Task 3 `ScoringResize::hit`, `dragTo`, `Target`.
- Produces:
  - `gManualScoringLayer::Item` gains `QString duration;` — `label` holds only the type (+ " · manual" / " · was CA"), `duration` holds `"16.0 s"` when zoomed; paint draws `label` at top left and `duration` at top right of the box (outside, as now, when the box is narrower than both).
  - `gManualScoringLayer` overrides `mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent` (only when it draws boxes and `graph->graphView()->scoringMode()`): hover over an edge sets `Qt::SizeHorCursor` (unset otherwise); press on an edge starts the drag (consumes the event) and calls `gGraphView::setScoringDrag(true)`; move updates the drawn box via `dragTo`; release, if `scoringDrag()` is still true, emits `gGraphView::scoringResized(target, startMs, endMs)` and clears the drag.
  - `gGraphView`: `void setScoringDrag(bool on); bool scoringDrag() const;` Esc while `scoringDrag()` clears it and redraws instead of asking to leave the mode; `setScoringMode(false)` clears it. Signal `void scoringResized(const ScoringResize::Target &target, qint64 startMs, qint64 endMs);` (register the metatype or pass by value inside the same thread — direct connection).
  - The drag limits: the layer asks `gGraphView` for them via a callback set by Daily: `void setScoringLimits(std::function<QPair<qint64,qint64>(const ScoringResize::Target &)> f);` — Daily returns the event's session span (graph time) or the day's first–last session span.
  - `Daily::onScoringResized(const ScoringResize::Target &t, qint64 startMs, qint64 endMs)`: Event → the `EffectiveEvent` at `t.eventIndex` of `m_scoringDrawn->events`; Added → `updateEdit(day, ev.editId, …)`; Device/Retyped → `resizeEvent(scoringSessionAt(day, ev.originalEndMs), ev, …)`; Excluded → `updateEdit(day, t.editId, …)`; then `scoringChanged()`.
  - `ScoringMenus::editRows`: a Resize edit gives `"%1: %2 → %3 s"` (type label, `originalDurationSec`, `durationSec`, one decimal); `Daily::getManualScoring` summary becomes `tr("added %1, removed %2, type changed %3, bounds changed %4, %5 (%6 stretches)")`.
  - The scoring banner text adds "drag an edge to change it": `tr("Scoring mode: drag across the flow graph to mark a stretch, drag an edge to change it, or right-click an event · Esc to leave")`.

- [ ] **Step 1: Failing tests**
  - `testBoxLabelSplit` (scoringmodetests): `gManualScoringLayer::items` zoomed → a device OA 16 s item has `label` "OA" and `duration` "16.0 s"; added → label "OA · manual".
  - `testScoringDragEsc`: a `gGraphView` in scoring mode with `setScoringDrag(true)`; Esc key press+release → `scoringDrag()` false and `scoringModeExitRequested` not emitted; a second Esc → emitted.
  - `testScoringDragClearedOnModeOff`: `setScoringMode(false)` → `scoringDrag()` false.
  - `testEditRowResize` (scoringmenustests): a day with a Resize edit → row text "OA: 12.0 → 16.0 s".
- [ ] **Step 2: Build (exit 0), run** — FAIL.
- [ ] **Step 3: Implement** the layer mouse handling, the view flag/signal/limits, Daily wiring (connect in the constructor next to `scoringRangeSelected`), the labels, the edit row and the summary/banner strings.
- [ ] **Step 4: Run** — suite green; build `../build` and `../build-nobt` (exit 0).
- [ ] **Step 5: Commit** — "Scoring mode: drag the edges of event boxes and excluded stretches".

### Task 5: Translations, explanations, check in the app

**Files:**
- Modify: `Translations/Russkiy.ru.ts`, `oscar/glossary.cpp` (the `manual_scoring` entry's details mention dragging edges), handoff `docs/superpowers/plans/2026-10-01-handoff.md` §4.9.

- [ ] **Step 1:** `/opt/homebrew/bin/lupdate oscar/oscar.pro -ts Translations/Russkiy.ru.ts`; fill every `type="unfinished"` (Russian: "границы изменены %4", "%1: %2 → %3 с", banner "…, потяните за край, чтобы его изменить, …"); `grep -c 'type="unfinished"'` → 0; `python3 docs/superpowers/tools/validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`; `lrelease … -qm oscar/translations/Russkiy.ru.qm`.
- [ ] **Step 2:** Rebuild all three dirs (exit 0); `../tools/runtests.sh` green.
- [ ] **Step 3:** In the app (`--datadir /Users/semyk/Documents/OSCAR20_Data_dev` only, owner's consent for the 22→23 upgrade prompt): open a night, Разметка, zoom ≤ 20 min, check the ↔ cursor and labels; the drag itself is checked by the owner.
- [ ] **Step 4: Commit** — "Translations and explanations: dragging scoring edges"; handoff doc with `git add -f` "(fork-only, not for MR)".
