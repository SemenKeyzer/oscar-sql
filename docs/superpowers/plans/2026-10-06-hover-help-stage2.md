# Hover help, stage 2 (menus, buttons, settings, import, oximetry) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every menu item, tab button, setting and oximetry-import control explains on hover what it does, what to choose, and whether it is dangerous, using the stage-1 glossary, tooltip filter and help panel.

**Architecture:** `GlossaryEntry` gains `place/advice/caution`; a second table `uiglossary.cpp` holds `ui.<window>.<objectName>` entries. `HelpTips::attachAll(root, window)` tags widgets by object name; `HelpTips::attachMenus(menuBar)` follows menu highlighting. Modal dialogs get a `HelpStrip` at the bottom. A coverage test built on the `.ui` forms fails when a control has no entry.

**Tech Stack:** Qt 6.11, qmake, C++17 (`-Werror`), QtTest via `tools/runtests.sh`.

**Spec:** `docs/superpowers/specs/2026-10-06-hover-help-stage2-design.md` (stage 1: `docs/superpowers/specs/2026-10-07-hover-help-design.md`)

## Global Constraints

- Keys: `ui.<window>.<objectName>`; windows: `menu`, `main`, `daily`, `overview`, `welcome`, `prefs`, `oximport`, `ble`.
- Table strings: `QT_TRANSLATE_NOOP("Glossary", "…")` written out literally (lupdate does not expand macros); context `Glossary`.
- `summary` ≤ 300 characters; every `ui.*` entry has `term`, `place`, `summary`.
- `caution` exactly on: data purge/delete actions, recalculation/reanalysis, changing the data folder, re-import over existing data, reset of settings to defaults — and nowhere else (the test holds the explicit key list).
- Explanations off (`HelpTips::enabled() == false`) → old Qt tooltips, no strip, no panel following.
- No changes to `.ui` tooltip texts. New `objectName`s only where a control is created in code.
- Russian for every new string; `python3 tools/validate_ts.py` → `TOTAL 0`; `lrelease Translations/Russkiy.ru.ts -qm oscar/translations/Russkiy.ru.qm`.
- Builds `build`, `build-test`, `build-nobt` all green; rerun qmake when files are added; after changing `GlossaryEntry` delete dependent `.o` in all three dirs.
- App check only with `--datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа`, background `app_*` tools only (never full-screen control).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; no push without the owner's command.

## Deviations from the spec (decided while planning)

- `oximetry.ui` is not used by any class (dead form); there is no oximetry window to wire. Oximetry is covered by the import wizard and the Bluetooth page.
- The Statistics tab's controls (report mode, dates) live in `mainwindow.ui`, so their keys are `ui.main.*`, not `ui.stats.*`; the left toolbar buttons are `ui.main.*` too.

## Review Focus

1. A control the `.ui` coverage cannot see (created in code, e.g. Overview preset buttons, Daily «Align», Bluetooth page, prefs search, analysis page) without an entry → `testCodeCreatedControlsCovered` (Task 4).
2. Dynamic menu items (recent databases, profile lists, graph lists) must not need entries and must not break hover → coverage skips actions without `objectName`; `testMenuHoverSkipsDynamicItems` (Task 2).
3. A `QLabel` next to a field explains the field, not nothing → `testLabelTakesBuddyKey` (Task 2).
4. Strip of one dialog must not show an entry hovered in the main window behind it → `testStripIgnoresOtherWindows` (Task 3).
5. Turning explanations off while Preferences is open hides the strip → `testStripHiddenWhenOff` (Task 3).

---

### Task 1: Glossary fields and the UI table

**Files:**
- Modify: `oscar/glossary.h`, `oscar/glossary.cpp`
- Create: `oscar/uiglossary.{h,cpp}` — `QList<GlossaryEntry> uiGlossaryEntries();` (untranslated sources; `glossary.cpp` translates and merges them like its own table) and `QStringList Glossary::cautionKeys()`
- Modify: `oscar/oscar.pro` (SOURCES/HEADERS, test build too)
- Test: `oscar/tests/glossarytests.{h,cpp}`

**Interfaces:**
- Produces: `GlossaryEntry::place`, `::advice`, `::caution` (QString); `Glossary::find/all/search/tooltip/panel` cover both tables; `QStringList Glossary::cautionKeys()` (the exact list of keys that must carry a caution, defined next to the table).

- [ ] **Step 1: Failing tests** in `GlossaryTests`:
  - `testUiEntriesComplete`: for every entry with key starting `ui.`: `!term.isEmpty()`, `!place.isEmpty()`, `!summary.isEmpty()`, `summary.size() <= 300`; keys unique across both tables; every `seeAlso` key found.
  - `testCautionOnlyWhereListed`: set of `ui.*` keys with non-empty `caution` == `QSet(cautionKeys())`; `cautionKeys()` non-empty.
  - `testUiTooltipShowsAdviceAndCaution`: for a seed entry with advice and caution (`ui.menu.actionPurge_Current_Selected_Day` or whichever purge action exists in `mainwindow.ui`), `tooltip()` contains the term in `<b>`, the advice text, the caution text and `color:` (red); `panel()` contains `place`.
  - `testSearchFindsUiEntries`: `search("purge")` (English, test has no translator) contains the seed key.
- [ ] **Step 2: Run** `tools/runtests.sh GlossaryTests` → FAIL (no fields / no table).
- [ ] **Step 3: Implement.** Seed `uiglossary.cpp` with 3–5 real entries (one purge action with caution, one preference with advice) — content tasks 5–7 fill the rest. Tooltip for `ui.*`: `<b>term</b> — summary` + `<br><i>advice</i>` + `<br><span style='color:#c0392b'>⚠ caution</span>`. Panel: term heading, `place` line, sections «What it does / Advice / Caution / See also» (English sources, translated). Search also matches `place`. Search result list text in `HelpPanel`: `term — place` when `place` is set.
- [ ] **Step 4: Run** `tools/runtests.sh GlossaryTests HelpPanelTests` → PASS; delete stale `.o` depending on `glossary.h` in all three build dirs and build them.
- [ ] **Step 5: Commit** `Glossary: UI entries with place, advice and caution`.

### Task 2: attachAll, labels and menus

**Files:**
- Modify: `oscar/helptips.{h,cpp}`
- Test: `oscar/tests/helptipstests.{h,cpp}`

**Interfaces:**
- Consumes: `Glossary::find` (Task 1).
- Produces: `int HelpTips::attachAll(QWidget *root, const QString &window)` (returns number tagged); `void HelpTips::attachMenus(QWidget *menuOwner)` (every `QMenu` child, incl. submenus: `hovered(QAction*)` → `hover("ui.menu." + action->objectName())` when the action has an object name and an entry and explanations are on); `static QString HelpTips::keyOf(QWidget *w)` (own key or nearest ancestor's, up to the window — the loop now in `eventFilter`).

- [ ] **Step 1: Failing tests** in `HelpTipsTests` (register entries via the seed keys of Task 1, or a test-only key in the table such as `ui.prefs.<seed>`):
  - `testAttachAllByObjectName`: a widget tree with a `QCheckBox` named as a seed `ui.prefs.*` key and one unknown name; `attachAll(root,"prefs") == 1`; property `helpKey` set only on the known one.
  - `testLabelTakesBuddyKey`: `QLabel` with `setBuddy(spinBox)` (spin box has an entry, label has none) → label gets the spin box key.
  - `testMenuHoverFollowsAction`: `QMainWindow` + `QMenu` with `QAction` named per a seed `ui.menu.*` key; `attachMenus(&win)`; `emit menu->hovered(action)` → `QSignalSpy(hovered)` has the key; with `setEnabled(false)` → no signal.
  - `testMenuHoverSkipsDynamicItems`: action without object name → no signal, no crash.
  - `testMenuTooltipUsesActiveAction`: `QMenu::setActiveAction(action)`, send `QHelpEvent(QEvent::ToolTip)` to the menu → `QToolTip::text()` contains the entry term.
- [ ] **Step 2: Run** `tools/runtests.sh HelpTipsTests` → FAIL (no such members).
- [ ] **Step 3: Implement** in `helptips.cpp`; `eventFilter` uses `keyOf`, and for a `QMenu` target uses `"ui.menu." + activeAction()->objectName()`. Do not overwrite a key already set (stage-1 keys win).
- [ ] **Step 4: Run** `tools/runtests.sh HelpTipsTests HelpLinksTests NightSummaryTests` → PASS.
- [ ] **Step 5: Commit** `Hover help: attach by object name, labels, menus`.

### Task 3: HelpStrip for modal dialogs

**Files:**
- Create: `oscar/helpstrip.{h,cpp}`; Modify: `oscar/oscar.pro`
- Test: `oscar/tests/helpstriptests.{h,cpp}` (register like `helppaneltests`)

**Interfaces:**
- Consumes: `HelpTips::hovered`, `HelpTips::enabledChanged`, `Glossary::panel`.
- Produces: `class HelpStrip : public QTextBrowser { HelpStrip(const QStringList &prefixes, QWidget *parent); QString key() const; }` — height ≈ 4 text lines, objectName `helpStrip`; shows `Glossary::panel(key)` for keys starting with one of `prefixes`; empty text «Hover over a setting to see what it does.»; visible only while explanations are on.

- [ ] **Step 1: Failing tests** `HelpStripTests`:
  - `testShowsHoveredEntry`: strip with `{"ui.prefs."}`; `HelpTips::instance()->hover(seedPrefsKey)` → `key() == seedPrefsKey`, `toPlainText()` contains the term.
  - `testKeepsEntryAfterLeave`: then `hover("")` → key unchanged.
  - `testStripIgnoresOtherWindows`: `hover("ahi")` and `hover(seedMenuKey)` → key unchanged.
  - `testStripHiddenWhenOff`: `setEnabled(false)` → `!isVisibleTo(parent)`; back on → visible.
  - `testEmptyStateText`: new strip shows the empty-state text.
- [ ] **Step 2: Run** `tools/runtests.sh HelpStripTests` → FAIL (no class).
- [ ] **Step 3: Implement**; links `help:` inside the strip switch the strip to that key (openLinks false, `anchorClicked`).
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `Help strip for modal dialogs`.

### Task 4: Wire windows and the coverage test

**Files:**
- Modify: `oscar/mainwindow.cpp` (after `setupUi`: `attachMenus(this)`, `attachAll(this,"main")`; object name `actionRecalculateAnalysis` for the code-made action at ~line 141), `oscar/daily.cpp` (`attachAll(this,"daily")`; `alignButton->setObjectName("alignButton")`), `oscar/overview.cpp` (`attachAll(this,"overview")`; preset buttons `presetButton_<n>` with n = `int(preset)`), `oscar/welcome.cpp` (`attachAll(this,"welcome")`), `oscar/preferencesdialog.cpp` (`m_search` → `searchSettings`; `HelpStrip({"ui.prefs."})` added under the tab widget; `attachAll(this,"prefs")` after the analysis page is built), `oscar/analysisprefs.cpp` (object names for every control: `analysisEnabled`, `hypopneaRule`, `limitOxi`, `pulseArousal`, `classifyApneas`, `classifyThresholds`, `advancedGroup`, `resetDefaults`, and each `number(...)` box named after its parameter), `oscar/oximeterimport.cpp` (`btButton` → `bluetoothImportButton`, `m_btRetryButton` → `bluetoothRetryButton`, `m_btDoneButton` → `bluetoothDoneButton`; `HelpStrip({"ui.oximport.","ui.ble."})`; `attachAll(this,"oximport")`), `oscar/bluetoothoximeterpage.cpp` (`connectButton`, `syncClock`, `eraseAfter`, any other control; `attachAll(this,"ble")`).
- Create test: `oscar/tests/uicoveragetests.{h,cpp}`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: object names above (Tasks 5–7 write entries for them); the coverage helper `QStringList uncovered(QWidget *root, const QString &window, const QStringList &exempt)` inside the test file.

- [ ] **Step 1: Failing tests** `UiCoverageTests`. Helper: after `attachAll`, collect `QAbstractButton`, `QComboBox`, `QAbstractSpinBox`, `QLineEdit`, `QSlider`, `QCalendarWidget`, checkable `QGroupBox`; skip names starting `qt_`, empty names, children of `QDialogButtonBox`, children of `QCalendarWidget`/`QAbstractSpinBox`, and `exempt`; return names whose `helpKey` is empty. One test per window, each on its `.ui` form via `setupUi` on a plain host (`QDialog` / `QWidget` / `QMainWindow`):
  - `testMenusCovered` (`Ui::MainWindow`: every `QAction` with non-empty object name and text, not a separator, has `Glossary::find("ui.menu."+name)`),
  - `testMainCovered` (`Ui::MainWindow` widgets, window `main`),
  - `testDailyCovered`, `testOverviewCovered`, `testWelcomeCovered`,
  - `testPrefsCovered` (`Ui::PreferencesDialog` + `AnalysisPreferencesPage` alone),
  - `testOximportCovered` (`Ui::OximeterImport`; `#ifdef HAVE_BLUETOOTH` + `BluetoothOximeterPage` alone with window `ble`),
  - `testCodeCreatedControlsCovered`: `Glossary::find` for `ui.menu.actionRecalculateAnalysis`, `ui.daily.alignButton`, every `ui.overview.presetButton_<n>`, `ui.prefs.searchSettings`, `ui.oximport.bluetoothImportButton`, `…bluetoothRetryButton`, `…bluetoothDoneButton`.
  Each failure message lists the uncovered names (`QVERIFY2(list.isEmpty(), qPrintable(list.join(", ")))`). Exempt lists are literal in the test with a one-line reason each.
- [ ] **Step 2: Run** `tools/runtests.sh UiCoverageTests` → FAIL listing the missing names (expected red until Tasks 5–7; record the counts in the ledger).
- [ ] **Step 3: Implement the wiring** listed under Files. Build all three dirs.
- [ ] **Step 4: Run** `tools/runtests.sh` → everything except `UiCoverageTests` passes; `UiCoverageTests` fails only on missing entries (not crashes).
- [ ] **Step 5: Commit** `Hover help: wire menus, tabs, settings and oximetry import`.

### Task 5: Entries — menus, main toolbar, Daily, Overview, welcome

**Files:** Modify `oscar/uiglossary.cpp`; `Glossary::cautionKeys()` gets the purge / rebuild / data-folder / re-import actions found in `mainwindow.ui`.

- [ ] **Step 1:** `tools/runtests.sh UiCoverageTests` → note the uncovered names for `menu`, `main`, `daily`, `overview`, `welcome` (the RED for this task).
- [ ] **Step 2:** Write one entry per name: `term` = on-screen text without `&`, `place` like "File menu" / "Daily → bottom bar", `summary` what it does, `advice` where a real choice exists, `caution` only for `cautionKeys()`, `seeAlso` to stage-1 keys where natural (e.g. Overview range → `period`).
- [ ] **Step 3: Run** `tools/runtests.sh UiCoverageTests GlossaryTests` → `testMenusCovered`, `testMainCovered`, `testDailyCovered`, `testOverviewCovered`, `testWelcomeCovered` PASS; `testCodeCreatedControlsCovered` passes for its menu/daily/overview keys.
- [ ] **Step 4: Commit** `Glossary: menus and tab controls`.

### Task 6: Entries — Preferences (incl. Analysis page)

**Files:** Modify `oscar/uiglossary.cpp`.

- [ ] **Step 1:** Note the uncovered `prefs` names (RED).
- [ ] **Step 2:** Entries as in Task 5. Advice names the owner's devices where it matters (Prisma 20A, Resvent): day-split time, ignore-short-sessions, leak redline, combine close sessions, clinical mode, cache/compression options. `caution` for settings changes that trigger a re-import or a reanalysis prompt and for any "reset to defaults" button.
- [ ] **Step 3: Run** `tools/runtests.sh UiCoverageTests GlossaryTests` → `testPrefsCovered` PASS, `ui.prefs.searchSettings` found.
- [ ] **Step 4: Commit** `Glossary: settings`.

### Task 7: Entries — oximetry import and Bluetooth page

**Files:** Modify `oscar/uiglossary.cpp`.

- [ ] **Step 1:** Note the uncovered `oximport` / `ble` names (RED; `ble` only in the BT build).
- [ ] **Step 2:** Entries; Contec advice: close the phone app, turn on Bluetooth in the oximeter menu; `eraseAfter` gets a caution (erases records on the device) and joins `cautionKeys()`.
- [ ] **Step 3: Run** `tools/runtests.sh UiCoverageTests GlossaryTests` in `build-test` (BT) and `build-nobt` → all `UiCoverageTests` PASS.
- [ ] **Step 4: Commit** `Glossary: oximetry import`.

### Task 8: Translation, app check, handoff

**Files:** `Translations/Russkiy.ru.ts`, `oscar/translations/Russkiy.ru.qm`, handoff doc (fork-only, `git add -f`).

- [ ] **Step 1:** `lupdate` the project; fill every new `Glossary`/`HelpStrip`/`HelpPanel` string in Russian by script; `python3 tools/validate_ts.py` → `TOTAL 0`; `lrelease`.
- [ ] **Step 2:** Full suite `tools/runtests.sh` → all pass; build all three dirs.
- [ ] **Step 3: App check** (background `app_*` only): open Settings and Oximetry import via `app_menu`; `app_screenshot` shows the strip with the empty-state text in Russian; `app_ax_find` confirms controls exist. Ask the owner to hover two settings and two menu items and report.
- [ ] **Step 4:** Handoff section «Пояснения, этап 2» in the handoff doc; commit `Translations: hover explanations stage 2` and the handoff commit "(fork-only, not for MR)".
