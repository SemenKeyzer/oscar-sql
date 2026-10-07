# Manual scoring, part 2 — plan

Owner's go: "делаем все, сделай всю работу последовательно и самостоятельно" (2026-10-07).
Builds on `docs/superpowers/specs/2026-10-07-manual-scoring-design.md`. Ideas A–D come from a
screenshot of the doctor's polysomnography software (events as labelled boxes on the flow, a
right-click menu with letter keys, "change type / delete all episodes of a type", stepping through
events of one type in a fixed window).

1. Per-hour indices: `Day::perHour(code)` — scored channels and RERA over `ahiHours()`, others over
   CPAP hours; Daily indices and Overview CPH charts use it; a scored type added by hand that the
   device never recorded is listed (`getSortedMachineChannels`). Tests: AnalysisIntegrationTests.
2. Sidebar totals and edit rows skip disabled sessions. Test: editRows.
3. Queries: a per-profile cache of the sessions that have manual scoring; LoadFromDatabase and
   StoreToDatabase query only those. Test: ManualScoringTests (cache follows store/remove).
4. Letter keys in the scoring menus (O C A H X R T, also on the Russian layout: Щ С Ф Р Ч К Е),
   shown as the action's shortcut text; `ScoringMenus::actionForKey(menu, text)`. Test: ScoringMenusTests.
5. Bulk: "Change type of all <type> (N)" and "Remove all <type> (N)" in the event menu, with a
   confirmation; `ManualScoring::bulkEdits(result, from, to)` returns the edits and the edit ids to
   undo (added events). Tests: ManualScoringTests.
6. Navigation in scoring mode: a bar with the type ([count]), ◀ ▶ and the window (1/3/5 min);
   `ManualScoring::nextEvent(events, type, center, forward)`. Tests: ManualScoringTests.
7. Boxes on the flow graph when 20 min or less is shown: every counted scored event as a translucent
   box with "OA 11.8 s"; `gManualScoringLayer::items` gains Kind::Event. Test: ScoringModeTests.
8. Glossary entries for the new controls, Russian, three builds, suite, final review by a fresh reviewer.
