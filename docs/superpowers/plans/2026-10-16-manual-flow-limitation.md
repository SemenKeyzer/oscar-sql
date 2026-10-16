# Manual flow limitation stretches — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** In scoring mode the doctor marks flow-limited stretches on the flow graph; the Manual analysis page shows their share of the night and minutes.

**Architecture:** A new edit kind `Kind::Mark` (stored as text `mark` in `manual_scoring`) with a new span channel `CPAP_ManualFlowLimit`. `ManualScoring::apply` collects mark edits, merges them, clips them to the sessions and subtracts excluded spans (`Result::flowLimitedMs`, `flowLimitedSpans`, `markEdits`). The scoring menus, the scoring layer, edge dragging and the edits list treat a mark like an excluded stretch with its own colour and label.

**Tech Stack:** Qt 6.11, qmake, C++17 `-Werror`, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-16-manual-flow-limitation-design.md`

## Global Constraints

- Menu item `tr("Flow limitation")` → «Ограничение потока», key **F** (Russian layout: «А», U+0410), after the event types, before «Артефакт».
- Stretch colour: `QColor(0x2e, 0x8b, 0x57, 70)` fill, label `tr("FL %1")` with m:ss → «ОП %1».
- Minutes = merged mark stretches clipped to the sessions minus the excluded spans; percent = of `Day::ahiHours()`.
- Manual page line: `tr("Flow limitation (manual scoring): %1% of the night, %2 min")` → «Ограничение потока (ручная разметка): %1 % ночи, %2 мин»; only when there is a stretch.
- Edits list text: `tr("flow limitation %1")` (m:ss) → «ограничение потока %1».
- Does not change the AHI, counts, device or analysis flags. No schema version change.
- Every new string translated (`validate_ts.py` TOTAL 0); glossary coverage passes.
- Build `../build-test`, `../build`, `../build-nobt` (exit codes); `../tools/runtests.sh` green; app only via `open -n`.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A mark stretch across two sessions or partly outside any session — counted once, only inside the sessions.
2. A mark inside an artifact (excluded) stretch — those minutes do not count; undoing the artifact brings them back.
3. Repeat type with «Ограничение потока» chosen — the next selections become marks, not events.
4. Dragging a mark's edge onto another mark — merged in the total, both still undoable.
5. A night with marks but no CPAP hours — no division by zero, no line.

Tests pinning these: 1, 2, 4 → Task 1 `testMarksMergedAndClipped`, `testMarksLessArtifact`; 3 → Task 2 `testRepeatMark`; 5 → Task 3 `testManualFlowLineWithoutHours`.

---

### Task 1: The model

**Files:** `oscar/SleepLib/manual_scoring.{h,cpp}`, `oscar/database/manual_scoring_repository.cpp` (`kindName`/`kindOf`: `Kind::Mark` ↔ `"mark"`), `oscar/SleepLib/schema.cpp` + `machine_common.{h,cpp}` (`CPAP_ManualFlowLimit = 0x1014`, SPAN, MT_CPAP, code "ManualFlowLimit", name `tr("Flow Limitation (manual scoring)")`, label `tr("mFL")`, unit percentage, not in any AHI group). Test: `oscar/tests/manualscoringtests.cpp`.

**Produces:**
- `enum class Kind { Add, Remove, Retype, Exclude, Resize, Mark };`
- `struct MarkEdit { qint64 editId; ChannelID channel; qint64 startMs, endMs; };`
- `Result` gains `QList<MarkEdit> markEdits;` (unmerged), `QList<QPair<qint64,qint64>> flowLimitedSpans;` (merged, clipped to sessions, less excluded), `qint64 flowLimitedMs = 0;`.
- `qint64 ManualScoring::flowLimitedMs(Day *day);` — sum over the day's enabled CPAP sessions' `resultFor`.
- `bool ManualScoring::moveStretch(...)` (existing, used for Exclude edges) accepts a Mark edit too.

- [ ] **Step 1: Failing tests:** `testMarksMergedAndClipped` (marks 100–200 s and 150–300 s and 900–1100 s with one session 0–1000 s → `flowLimitedMs == 300000`, `flowLimitedSpans == {{100,300},{900,1000}}` in ms, `markEdits.size() == 3`; the AHI delta unchanged); `testMarksLessArtifact` (mark 100–300 s, exclude 200–250 s → `flowLimitedMs == 150000`); `testMarkStored` (repository round trip keeps kind Mark and channel).
- [ ] **Step 2:** build → FAIL. **Step 3:** implement. **Step 4:** suite green. **Step 5:** commit "Manual scoring: flow limitation stretches in the model".

### Task 2: Menus, adding, undoing, repeating

**Files:** `oscar/scoringmenus.{h,cpp}`, `oscar/daily.{h,cpp}` (`applyScoring` action `"mark"`: one Mark edit per CPAP session the stretch touches, like `"exclude"`; `onScoringContext`: a click inside a mark stretch offers its own menu). Test: `oscar/tests/scoringmenustests.cpp`.

**Produces:**
- `forRange` item `tr("Flow limitation")`, key F, data `{action: "mark", channel: CPAP_ManualFlowLimit}`, placed after the event types and before «Artifact».
- `static QMenu *ScoringMenus::forMark(qint64 editId, QWidget *parent);` — «Undo flow limitation» (U), data `undo`.
- `repeatableType` returns the channel for `"mark"` too; `repeatChoice(CPAP_ManualFlowLimit)` returns the `"mark"` choice.
- Edits list (`editRows`): a Mark row reads `tr("flow limitation %1")` with m:ss.

- [ ] **Step 1: Failing tests:** `testFlowLimitationItem` (forRange texts end with «Flow limitation», «Artifact»; key `f` and `а` pick it; its data action `mark`); `testRepeatMark` (`repeatableType(mark choice) == CPAP_ManualFlowLimit`, `repeatChoice(CPAP_ManualFlowLimit)` equals the menu's choice); `testMarkMenu` (forMark has «Undo flow limitation» with U and the edit id); `testMarkEditRow` (editRows text «flow limitation 2:10» for a 130 s mark — via the existing editRows test helper).
- [ ] **Step 2:** build → FAIL. **Step 3:** implement. **Step 4:** suite green. **Step 5:** commit "Manual scoring: mark flow limitation from the selection menu".

### Task 3: Drawing, dragging, the Manual page

**Files:** `oscar/Graphs/gManualScoringLayer.{h,cpp}` (an `Item::Marked` drawn with the stretch colour and the label), `oscar/Graphs/scoringresize.{h,cpp}` (a `Target::Marked` whose edges drag like an excluded stretch's), `oscar/daily.cpp` (`manualAnalysisHtml` gets the flow line: new static `Daily::manualFlowHtml(Day *day)`, placed under the AHI line). Tests: `oscar/tests/scoringresizetests.cpp` (or the existing resize tests), `oscar/tests/analysisintegrationtests.cpp`.

- [ ] **Step 1: Failing tests:** `testMarkedItems` (layer items for a result with a mark include one `Marked` item with its bounds and label «FL 2:10»); `testDragMarkEdge` (hit at a mark's edge gives a Marked target; dragging it moves that edit's bound); `testManualFlowLine` (a day with a 54-min mark over 7.5 AHI hours → «Flow limitation (manual scoring): 12% of the night, 54 min»); `testManualFlowLineWithoutHours` (no CPAP hours → empty).
- [ ] **Step 2:** build → FAIL. **Step 3:** implement. **Step 4:** suite green. **Step 5:** commit "Manual scoring: flow limitation stretches on the graph and the Manual page".

### Task 4: Translation, glossary, app check

- [ ] lupdate + Russian (Global Constraints), `validate_ts.py` TOTAL 0, lrelease; glossary entry `scoring_fl` for the term on the Manual page if a term link is used; three builds; suite green; commit "Russian for manual flow limitation".
- [ ] `open -n` the app; the owner marks stretches, drags edges, undoes, repeats, reads the Manual page.
