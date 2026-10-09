# Daily sidebar: tab buttons, «Ручной анализ», three-part AHI frame — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> Fork-only document, not for the upstream MR.

**Goal:** Daily's left panel shows its pages as two rows of lit buttons, manual scoring gets its own «Ручной анализ» page, and the AHI frame shows the device's AHI large with OSCAR's analysis and manual scoring beneath.

**Architecture:** A small reusable `TabButtons` widget drives an existing `QTabWidget` (hides its tab bar, one checkable button per page in given rows, follows `currentChanged`, hides a page's button with the page). Daily inserts it above `ui->tabWidget`, adds a `MyTextBrowser` page for manual analysis fed by a static HTML builder, and builds the AHI frame from a static helper so both are unit-testable.

**Tech Stack:** Qt 6.11, qmake, C++17 `-Werror`, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-10-daily-sidebar-design.md`

## Global Constraints

- Rows: 1 «Подробности · События · Анализ OSCAR · Ручной анализ»; 2 «Примечания · Закладки · Поиск» (the page texts as translated; the Analysis page's button text «Анализ OSCAR», source `tr("OSCAR analysis")`; new page source `tr("Manual analysis")` → «Ручной анализ»).
- Button look: `OverviewPresets::buttonStyle(QStringLiteral("QToolButton"))`; exactly one checked.
- «Анализ OSCAR» page hidden with the analysis (`analysis::analysisShown()`); hiding it while current selects «Подробности».
- AHI frame (variant A): large = `Day::deviceAHI()` (RDI name/mode as now); below on one line «анализ OSCAR — X | ручная разметка — Y», «—» where no value; no analysis part when the analysis is hidden; frame colour as now.
- Manual page: «AHI после ручной разметки: Y (по аппарату X)» on top, then the existing manual scoring block (`Daily::getManualScoring`); without edits the hint «Ручной разметки этой ночи нет — включите «Разметку» внизу, чтобы исправить события.» (source `tr("No manual scoring this night: switch Scoring on below to correct the events.")`). The block leaves «Подробности».
- Links on the manual page go to `Daily::Link_clicked` (jump, undo, clear) as now.
- Every new string translated; `validate_ts.py` TOTAL 0; glossary coverage passes (new buttons get `helpKey`s; reuse the page entries).
- Build `../build-test`, `../build`, `../build-nobt` (check exit codes); `../tools/runtests.sh` green. Launch the app only with `open -n`.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A night without CPAP data / a "brick" device — the frame keeps its existing message, no broken figures.
2. RDI mode — the large figure and its name follow RDI, the manual figure too.
3. Scoring changes (add, undo, clear) refresh both the frame and the manual page.
4. The Events tab "Manual scoring" node and Link_clicked's `event=` jump (which selects the Events page) still work with the hidden tab bar.
5. The Analysis page was current when the profile closed and the analysis is off at the next start — Daily opens on «Подробности».

---

### Task 1: `TabButtons`

**Files:** Create `oscar/tabbuttons.{h,cpp}` (add to `oscar/oscar.pro`; re-run qmake in the three build dirs). Test: `oscar/tests/tabbuttonstests.{h,cpp}` (new test class, `DECLARE_TEST`, add to the test sources in `oscar.pro`).

**Interfaces — Produces:**
- `class TabButtons : public QWidget` — `TabButtons(QTabWidget *tabs, const QList<QList<QWidget *>> &rows, QWidget *parent = nullptr);` hides `tabs->tabBar()`, creates one checkable `QToolButton` per page (text = `tabs->tabText(index)`, objectName `"tabButton_" + page->objectName()`), a `QButtonGroup` (exclusive), clicking sets the current page, `currentChanged` checks the right button.
- `void setPageVisible(QWidget *page, bool visible);` — `tabs->setTabVisible` and the button's visibility; when the hidden page is current, selects the first visible page of the first row.
- `QAbstractButton *button(QWidget *page) const;`

- [ ] **Step 1: Failing tests** (`TabButtonsTests`): `testButtonsFollowPages` (3 pages in rows {{a,b},{c}}: 3 buttons, tab bar hidden, clicking c's button → current c; `setCurrentWidget(b)` → b's button checked, only one checked; button text = tab text); `testHidePage` (current b, `setPageVisible(b,false)` → button hidden, current a; `setPageVisible(b,true)` → button visible).
- [ ] **Step 2:** qmake + build (exit 0), run → FAIL (compile).
- [ ] **Step 3:** implement.
- [ ] **Step 4:** suite green.
- [ ] **Step 5:** commit "TabButtons: a tab widget's pages as rows of buttons".

### Task 2: AHI frame and the manual page HTML

**Files:** `oscar/daily.{h,cpp}`. Test: `oscar/tests/helplinkstests.cpp` (already exercises Daily statics) or a new `DailySidebarTests` beside it.

**Interfaces — Produces:**
- `static QString Daily::ahiFiguresHtml(const QString &name, double deviceAhi, double analysisAhi, bool analysisShown, double manualAhi);` — the frame rows: large `name deviceAhi` (2 decimals), then «analysis OSCAR — X | manual scoring — Y» line; NaN → «—»; analysis part omitted when `!analysisShown`. (Background colour and COLOR_Text as in `getAHI`.)
- `static QString Daily::manualAnalysisHtml(const QString &scoringBlock, double correctedAhi, double deviceAhi, const QString &name);` — empty block → the hint; else «AHI after manual scoring: Y (device X)» + block.

- [ ] **Step 1: Failing tests:** `testAhiFigures`: (`"AHI"`, 3.71, 3.0, true, 3.86) contains "3.71", "OSCAR analysis — 3.0", "manual scoring — 3.86"; with analysis NaN → "OSCAR analysis — —"; with manual NaN → "manual scoring — —"; with `analysisShown=false` → no "OSCAR analysis". `testManualAnalysisHtml`: empty block → hint text; block "X" → contains "AHI after manual scoring: 3.86 (device 3.71)" and "X".
- [ ] **Step 2:** build (exit 0), run → FAIL.
- [ ] **Step 3:** implement; `getAHI` uses `ahiFiguresHtml` (device = `day->deviceAHI()` for the large figure; manual = the corrected AHI when `day->hasManualScoring()`, else NaN; analysis = `figures.analysisAhi` when `hasAnalysisAhi`, else NaN; `analysisShown()`), dropping the old «device X · corrected by hand» row and the separate «OSCAR's analysis: X» row; keep the OAHI/CAHI row and the brick message.
- [ ] **Step 4:** suite green.
- [ ] **Step 5:** commit "Daily: the AHI frame shows device, OSCAR analysis and manual scoring".

### Task 3: Wire it into Daily

**Files:** `oscar/daily.{h,cpp}`, `oscar/uiglossary.cpp`, `Translations/Russkiy.ru.ts`.

**Interfaces — Consumes:** Task 1 `TabButtons`, Task 2 helpers.

- [ ] **Step 1: Failing test:** `testManualPageLinks` (helplinkstests): a `MyTextBrowser` showing `manualAnalysisHtml(block with Daily::scoringLink("undo", 7))` → clicking the link emits `anchorClicked` with a URL `parseScoringLink` reads as undo/7 (pins that the manual page keeps the working link format).
- [ ] **Step 2:** build (exit 0), run → FAIL.
- [ ] **Step 3:** in the Daily constructor: create the manual page (`MyTextBrowser`, objectName `manualAnalysis`, `setOpenLinks(false)`, `anchorClicked` → `Link_clicked`) inserted after the Analysis page with title `tr("Manual analysis")`; set the Analysis page title to `tr("OSCAR analysis")`; create `TabButtons` with the rows of Global Constraints and insert it above `ui->tabWidget`; `applyAnalysisShown()` uses `TabButtons::setPageVisible(m_analysisTab, shown)`; the left sidebar no longer appends `getManualScoring(day)` after the AHI frame; the manual page gets `manualAnalysisHtml(getManualScoring(day), …)` on every load and scoring change. Glossary: helpKeys for the buttons reuse the pages' entries (add `ui.daily.manualAnalysis` entry).
- [ ] **Step 4:** lupdate, Russian («Ручной анализ», «Анализ OSCAR», hint, «AHI после ручной разметки: %1 (по аппарату %2)», «анализ OSCAR — %1», «ручная разметка — %1»), `validate_ts.py` TOTAL 0, lrelease; three builds (exit 0); suite green.
- [ ] **Step 5:** commit "Daily sidebar: pages as buttons, the Manual analysis page".

### Task 4: App check

- [ ] `open -n ../build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev --profile Папа`; owner checks buttons, frame, manual page (jump/undo), analysis on/off hides its button.
- [ ] Handoff note with `git add -f`; commit.
