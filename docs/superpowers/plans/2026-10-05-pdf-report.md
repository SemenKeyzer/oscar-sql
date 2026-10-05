# Combined PDF Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** «Файл → Создать PDF-отчёт…» writes one PDF: the one-page doctor summary, chosen nights as the Daily tab prints them, Overview graphs of a chosen set, and Statistics for the period.

**Architecture:** `PdfReportOptions` (pure) holds the choice, presets, period, nights and page estimate. `Report::PrintReport` is split so its drawing (`Report::paint`) draws onto a given painter. `paintHtmlPages` lays long HTML over printer pages. `Statistics::periodHtml` builds Statistics for explicit dates and sections. `PdfReportWriter` strings the sections into one `QPrinter`; `PdfReportDialog` is the window.

**Tech Stack:** Qt 6.11 (Widgets, Gui, PrintSupport), qmake, C++17, QtTest via `AutoTest.h`.

**Spec:** `docs/superpowers/specs/2026-10-05-pdf-report-design.md`

## Global Constraints

- Work on `master` of the fork; push only to `origin`, only when the user says so. GitHub Actions are off on the fork — don't re-enable.
- Build dirs `../build`, `../build-test`, `../build-nobt`; after adding files or changing `.ts`: `Q=$(sed -n 's/^# Command: //p' Makefile | head -1); eval "$Q"` then `make -j10`.
- Tests: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Class…]`, 0 unexpected failures (409 pass now).
- Russian translation of every new string; `grep -c 'type="unfinished"'` → 0; `validate_ts.py Translations/Russkiy.ru.ts` → `TOTAL 0`.
- Running OSCAR: only `open -n …/build/OSCAR20.app --args --datadir /Users/semyk/Documents/OSCAR20_Data_dev`, never two instances; quit via «OSCAR20 → Quit OSCAR20». After a rebuild macOS may block start for Documents access — ask the user to allow it.
- Commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; fork-only docs with `git add -f`, own commit ending `(fork-only, not for MR)`.
- Spec values: presets — Brief = summary only; Detailed = summary, daily Last7Nights, overview `Therapy`, statistics with settings changes + oximetry, no devices; Everything = all sections, nights AllNights, overview `All`, all three statistics parts. Period default Last30; periods 7/30/90 nights before the last CPAP night or Custom. A4 portrait, margins 10 mm. Default file name `Отчёт CPAP <profile> <from dd.MM>–<to dd.MM.yyyy>.pdf` in Documents. Profile key `PdfReportOptions`.
- «Файл → Печать» for Daily, Overview and Statistics must behave exactly as before.

## Review Focus

1. Cancel or an error midway through the Daily section — the Daily tab returns to the date it showed before, the half-written file is removed (Task 5: `testWriterRestoresAndCleansUp`).
2. A period with no CPAP nights — message, no file (Task 5 test `testWriterRefusesEmptyPeriod`).
3. Personal data unticked — no name in any section's header, and the user's own «show personal data» setting is unchanged afterwards (Task 3 test `testPeriodHtmlPersonalData`, Task 4 manual check).
4. A Statistics period that starts before the profile's first night or ends after its last — clamps to the data, no empty columns or crash (Task 3 test `testPeriodHtmlClampsToData`).
5. Old «Файл → Печать» of the Daily tab after the split — same pages as before (Task 4 manual check, before/after page count).

---

### Task 1: `PdfReportOptions` — choices, presets, period, nights, page estimate

**Files:** Create `oscar/pdfreportoptions.{h,cpp}`, `oscar/tests/pdfreportoptionstests.{h,cpp}`; modify `oscar/oscar.pro`.

**Interfaces — Produces** (`oscar/pdfreportoptions.h`):
```cpp
struct PdfReportOptions {
    enum PeriodKind { Last7, Last30, Last90, Custom };
    enum NightsKind { LastNight, Last3, Last7Nights, AllNights };
    enum Preset { Brief, Detailed, Everything };
    PeriodKind period = Last30;  QDate from, to;
    bool summary = true;
    bool daily = false;      NightsKind nights = Last7Nights;
    bool overview = false;   OverviewPresets::Preset overviewPreset = OverviewPresets::Therapy;
    bool statistics = false; bool statsSettings = true, statsOximetry = true, statsDevices = false;
    bool personalData = true, serialNumbers = false;

    void apply(Preset preset);
    QPair<QDate, QDate> range(const QDate &lastNight) const;          // Custom: from/to as set
    QList<QDate> nightsToPrint(const QList<QDate> &cpapNights) const; // ascending, inside range()
    bool anySection() const;
    QVariantMap toMap() const;
    static PdfReportOptions fromMap(const QVariantMap &map);          // unknown/missing → defaults
};
int estimatePages(const PdfReportOptions &o, int nights, int dailyGraphs, int overviewGraphs, int statisticsPages);
```
`estimatePages`: `summary` → 1; per night `1 + max(0, ceil((dailyGraphs - 5) / 6.0))`; overview `max(1, ceil(overviewGraphs / 6.0))`; statistics `statisticsPages`.

- [ ] **Step 1: Failing tests** `PdfReportOptionsTests`:
  - `testPresets`: after `apply(Brief)` only `summary`; `Detailed` → summary, daily+Last7Nights, overview+Therapy, statistics, statsSettings, statsOximetry, !statsDevices; `Everything` → all on, AllNights, `OverviewPresets::All`, all three stats parts. Personal-data fields untouched by presets.
  - `testRange`: last night 02.10.2026 → Last7 = 26.09–02.10, Last30 = 03.09–02.10, Last90 = 05.07–02.10; Custom returns from/to.
  - `testNightsToPrint`: nights {20,21,22,23,24,25,26,27,28,29,30.09,01,02.10}, Last30 → LastNight = {02.10}; Last3 = {30.09,01.10,02.10}; AllNights = all 13; nights outside the range are dropped; empty input → empty.
  - `testEstimatePages`: Detailed with 7 nights, 11 daily graphs, 6 overview graphs, 2 stats pages → 1 + 7·2 + 1 + 2 = 18; Brief → 1.
  - `testMapRoundTrip`: Detailed + Custom dates + personalData=false survive `toMap`/`fromMap`; `fromMap({"period": 99})` → defaults.
- [ ] **Step 2:** register files in `oscar.pro` (sources after `preferencessearch.*`, tests after `tests/preferencessearchtests.*`), qmake, run → FAIL (stub returning defaults).
- [ ] **Step 3:** implement per the Interfaces block.
- [ ] **Step 4:** `runtests.sh PdfReportOptionsTests` → PASS, 0 unexpected.
- [ ] **Step 5:** commit `"PDF report: options, presets, period and page estimate"`.

### Task 2: `paintHtmlPages` — HTML over printer pages

**Files:** Create `oscar/htmlpages.{h,cpp}`, `oscar/tests/htmlpagestests.{h,cpp}`; modify `oscar/doctorreport.cpp` (`writePdf` uses it), `oscar/oscar.pro`.

**Interfaces — Produces:**
```cpp
//! Lays \a html out on the printer's pages and draws them from the current page on.
int paintHtmlPages(QPainter &painter, QPrinter &printer, const QString &html, const QFont &font,
                   const QHash<QString, QImage> &images, bool startOnNewPage);
//! Size of a printer page in QTextDocument layout units (qt_defaultDpiY based, as writePdf does).
QSizeF htmlPageSize(QPrinter &printer);
```
Implementation: `QTextDocument` with `setPageSize(htmlPageSize(printer))`, `setDocumentMargin(0)`, images as resources; for each page `i`: if `i > 0 || startOnNewPage` → `printer.newPage()`; `painter.save()`, scale layout units to device pixels (`printer.pageRect(DevicePixel).width() / page.width()`), `translate(0, -i·page.height())`, `drawContents(painter, QRectF(0, i·h, w, h))`, `restore()`. Returns page count.

- [ ] **Step 1: Failing tests** `HtmlPagesTests`: a 3-line HTML → 1 page and a PDF with 1 `/Type /Page`; a 200-row table → `n > 1` and the PDF has exactly `n` pages; `startOnNewPage=true` after a first page → total pages = 1 + n. (Write to `QTemporaryDir`, count with the regex `/Type\s*/Page[^s]` as in `DoctorReportTests`.)
- [ ] **Step 2:** run → FAIL.
- [ ] **Step 3:** implement; switch `DoctorReportPage::writePdf` to `QPrinter` + `QPainter` + `paintHtmlPages` (same font 8.5 pt, same chart image, same `chartSize` from `htmlPageSize`).
- [ ] **Step 4:** `runtests.sh HtmlPagesTests DoctorReportTests` → PASS (incl. `testWritePdf`, `testWritePdfRussian` one page).
- [ ] **Step 5:** commit `"Reports: lay HTML over printer pages; doctor report uses it"`.

### Task 3: Statistics for explicit dates and sections

**Files:** Modify `oscar/statistics.{h,cpp}`; test in `oscar/tests/analysisintegrationtests.{h,cpp}`.

**Interfaces — Produces:**
```cpp
struct StatisticsSections { bool settingsChanges = true, oximetry = true, devices = true,
                            personalData = true, serialNumbers = false; };
QString Statistics::periodHtml(const QDate &from, const QDate &to, const StatisticsSections &s);  // public
DoctorReport Statistics::doctorReport(const QDate &from, const QDate &to, bool personalData, bool serialNumbers); // new overload; the old ones pass AppSetting values
```
Implementation decisions:
- Add private `std::optional<QPair<QDate,QDate>> m_period` and `StatisticsSections m_sections`; private helpers `int reportMode()`, `QDate rangeStart()`, `QDate rangeEnd()`, `QDate reportDate()` that return the override when set, else `p_profile->general->…`. Replace the direct reads at `statistics.cpp` ~398–402, ~1417, ~1572–1574, ~1919–1921 with them (the one at ~2716 belongs to `UpdateRecordsBox` — leave it).
- `periodHtml` sets the override (mode `STAT_MODE_RANGE`, dates clamped to `p_profile->FirstDay()/LastDay()`), builds `generateHeader(false) + GenerateCPAPUsage() + (settingsChanges ? GenerateRXChanges() : "") + (devices ? GenerateMachineList() : "") + generateFooter(true)`, clears the override (RAII guard).
- In `GenerateCPAPUsage` skip rows with `type == MT_OXIMETER` (and their headings) when `!m_sections.oximetry` under the override.
- `GenerateRXChanges` under the override lists only periods overlapping `[from, to]`.
- `getUserInfo()`, the RX table's serial column and `doctorReport`'s patient/serial read `m_sections.personalData/serialNumbers` under the override instead of `AppSetting`.

- [ ] **Step 1: Failing tests** in `AnalysisIntegrationTests` (one CPAP night via `hypopneaSession`, as the doctor-report tests do):
  - `testPeriodHtmlSections`: all sections → html contains `Statistics::tr("Changes to Device Settings")` and `Statistics::tr("Device Information")`; `settingsChanges=false, devices=false` → neither.
  - `testPeriodHtmlPersonalData`: set `p_profile->user` first name «Иван», `AppSetting->setShowPersonalData(true)`; `personalData=false` → html lacks «Иван»; afterwards `AppSetting->showPersonalData()` still true.
  - `testPeriodHtmlClampsToData`: `periodHtml(date.addDays(-400), date.addDays(400), …)` returns non-empty html without crashing and `p_profile->general->statReportMode()` / range unchanged afterwards.
- [ ] **Step 2:** run → FAIL (no member).
- [ ] **Step 3:** implement.
- [ ] **Step 4:** `runtests.sh AnalysisIntegrationTests DoctorReportTests` → PASS.
- [ ] **Step 5:** commit `"Statistics: build for explicit dates and chosen sections"`.

### Task 4: `Report::paint` — drawing split from the print dialog

**Files:** Modify `oscar/reports.{h,cpp}`.

**Interfaces — Produces:**
```cpp
struct PrintTarget { bool personalData = true; bool bookmarks = false; ProgressDialog *progress = nullptr; };
//! Draws \a gv's report from the painter's current page on; false if the printer failed.
static bool Report::paint(QPainter &painter, QPrinter &printer, gGraphView *gv, const QString &name,
                          const QDate &date, const PrintTarget &target);
```
- Move everything after `QPainter painter(printer);` in `PrintReport` into `paint` (window/viewport setup, header, graphs, page loop), except creating and closing the `ProgressDialog` and `painter.end()`. Replace `AppSetting->showPersonalData()` at the header with `target.personalData`; the Daily bookmark decision comes in via `target.bookmarks`; progress calls go through `target.progress` when non-null.
- `PrintReport` keeps: early returns, bookmark question, printer + `QPrintDialog`, `ProgressDialog`, `QPainter`, then `paint(…, {AppSetting->showPersonalData(), print_bookmarks, &progress})`, `painter.end()`.
- Use `date` (not `mainwin->getDaily()->getDate()`) for the Daily day inside `paint`.

- [ ] **Step 1:** before editing, print the Daily tab of 01.10.2026 (profile «Папа») to PDF via «Файл → Печать» → «Сохранить как PDF»; record its page count (`/Type /Page` count).
- [ ] **Step 2:** refactor as above; build all three dirs; full `runtests.sh` → 0 unexpected.
- [ ] **Step 3:** print the same day again: same page count, header and graphs as before (render page 1 with `qlmanage -t` and compare). Overview print likewise once.
- [ ] **Step 4:** commit `"Reports: draw onto a given painter, so other reports can include a day or the overview"`.

### Task 5: `PdfReportWriter` — one PDF from the sections

**Files:** Create `oscar/pdfreportwriter.{h,cpp}`; test `oscar/tests/analysisintegrationtests.{h,cpp}`; modify `oscar/oscar.pro`.

**Interfaces — Consumes:** Tasks 1–4. **Produces:**
```cpp
class PdfReportWriter {
public:
    //! \a daily / \a overview may be null (their sections are then skipped).
    PdfReportWriter(Daily *daily, Overview *overview);
    bool write(const PdfReportOptions &o, const QDate &lastNight, const QString &path,
               ProgressDialog *progress, QString *error);
    bool cancelled() const;
};
```
Decisions:
- `QPrinter(HighResolution)`, PDF, A4 portrait, margins 10 mm, one `QPainter`.
- Summary: `Statistics().doctorReport(from, to, o.personalData, o.serialNumbers)`; if `nights == 0` → error «За эти даты нет ночей с данными CPAP.», no file. Page via `DoctorReportPage::html` + chart + `paintHtmlPages`.
- Daily: CPAP nights from `p_profile->daylist` (days with `hours(MT_CPAP) > 0`) → `o.nightsToPrint`; a guard object stores `daily->getDate()` and calls `daily->LoadDate(saved)` in its destructor; per night `LoadDate`, `newPage`, `Report::paint(…, daily->graphView(), STR_TR_Daily, date, {o.personalData, false, progress})`.
- Overview: guard stores the overview range and current preset and restores both; `setRange(from, to)`, `showPreset(o.overviewPreset)` (needs `Overview::currentPreset()` getter — add), then `Report::paint(…, overview->graphView(), STR_TR_Overview, to, …)`.
- Statistics: `Statistics().periodHtml(from, to, {o.statsSettings, o.statsOximetry, o.statsDevices, o.personalData, o.serialNumbers})` → `paintHtmlPages(…, startOnNewPage = true)`.
- Each section starts on a new page (except the first page of the file). Progress text per section; cancel checked between nights/sections → `painter.end()`, `QFile::remove(path)`, return false with `cancelled() == true`.
- Error writing → `printer.printerState() == QPrinter::Error` or missing file → message «Не удалось записать %1.» (reuse `DoctorReport::tr` string), return false.

- [ ] **Step 1: Failing tests** in `AnalysisIntegrationTests` (CPAP night as before, `PdfReportWriter(nullptr, nullptr)`):
  - `testWriterSummaryAndStatistics`: Brief+statistics on the night → true, PDF with ≥ 2 pages.
  - `testWriterRefusesEmptyPeriod`: Custom range without nights → false, error non-empty, no file.
  - `testWriterRestoresAndCleansUp`: a `ProgressDialog` whose abort is triggered before writing → false, `cancelled()`, no file at `path`.
- [ ] **Step 2:** run → FAIL. **Step 3:** implement. **Step 4:** `runtests.sh AnalysisIntegrationTests` → PASS.
- [ ] **Step 5:** commit `"PDF report: writer stringing the sections into one file"`.

### Task 6: `PdfReportDialog`, menu, remembered choice — and the check on real data

**Files:** Create `oscar/pdfreportdialog.{h,cpp}`; delete `oscar/doctorreportdialog.{h,cpp}` (move `defaultFrom` test to `PdfReportOptionsTests` only if still meaningful — the Custom default is `range(Last30)`); modify `oscar/mainwindow.{ui,h,cpp}` (action `actionDoctor_Report` → `actionPdf_Report`, text «Create PDF Report...»), `oscar/SleepLib/profiles.h` (`STR_US_PdfReportOptions`, `QVariantMap pdfReportOptions()`, setter; drop `DoctorReportFrom` getter/setter but keep the key unread), `oscar/oscar.pro`, `Translations/Russkiy.ru.ts`, tests `oscar/tests/doctorreporttests.{h,cpp}` (remove `testDefaultFrom`).

**Interfaces — Consumes:** Tasks 1, 5.

Dialog per spec §2: period radios + two `QDateEdit` for Custom; preset buttons «Кратко для врача / Подробно / Всё»; section checkboxes with nested controls (daily nights radios with «все ночи периода (N)», overview set combo with `OverviewPresets::title`, three stats checkboxes); «Имя и дата рождения», «Серийные номера»; label «примерно %n страниц» via `estimatePages` (daily graphs = `daily->graphView()->visibleGraphs()`, overview graphs = count of `OverviewPresets::graphNames(set)` present in the overview, All → visible count; statistics pages = 2, 3 with settings changes); «Отмена», «Создать отчёт…» disabled when `!anySection()` or Custom `from > to`. Create: `QFileDialog::getSaveFileName` with the default name, `ProgressDialog`, `PdfReportWriter::write`, «Отчёт сохранён.» with «Открыть» (`QDesktopServices`); save `toMap()` to the profile on success.

- [ ] **Step 1:** implement dialog, menu wiring (`on_actionPdf_Report_triggered`, enabled with an open profile, after «Печать»), profile setting; remove `DoctorReportDialog`. Build all three; full `runtests.sh` → 0 unexpected.
- [ ] **Step 2:** translate all new strings (contexts `PdfReportDialog`, `MainWindow`, `PdfReportWriter`), unfinished 0, `TOTAL 0`; re-qmake `../build`.
- [ ] **Step 3:** check in the app (profile «Папа», after the user allows the start): «Подробно», period 03.09–02.10 → one PDF; page count matches the estimate ±2; first page = doctor summary; 7 nights as the Daily print; Overview «Терапия»; Statistics with settings changes. Afterwards the Daily tab shows the same date as before, the Overview the same range and set. Untick «Имя и дата рождения» → no name in any section; OSCAR's own «Показывать личные данные» unchanged.
- [ ] **Step 4:** commit `"PDF report: File menu window combining summary, nights, overview and statistics"`; then handoff note (fork-only commit).
