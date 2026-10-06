# Hover Explanations and Help Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hovering a figure or term on the home page, the Daily sidebar and graphs, the Overview graphs, Statistics and the doctor-summary window shows a short explanation; a help panel shows the full article; one View-menu switch turns the hover explanations off.

**Architecture:** A glossary table (`glossary.*`, strings via `QT_TRANSLATE_NOOP`) is the single source of articles. `HelpTips` (one per app) owns the on/off setting, an app-wide event filter for widgets tagged `helpKey`, the `help:` link wrapper for HTML pages and the `hovered(key)` signal. `HelpPanel` (a dock) shows the hovered article, "see also" and a search.

**Tech Stack:** Qt 6.11, C++17, qmake, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-07-hover-help-design.md`

## Global Constraints

- Repo `/Users/semyk/Downloads/Oscar_Project/oscar-sql`, branch `master`; build dirs `../build`, `../build-test`, `../build-nobt` (`-Werror`); re-run qmake after adding files; after changing a header's struct layout delete the dependent `.o` files in all three dirs (qmake dependency tracking has missed this before).
- Tests: `/Users/semyk/Downloads/Oscar_Project/tools/runtests.sh [Class…]` → `0 unexpected`.
- Glossary source strings in English, context `"Glossary"`; Russian in `Translations/Russkiy.ru.ts`; 0 unfinished; `validate_ts.py` → `TOTAL 0`.
- `summary` ≤ 300 characters (source and translation).
- Setting key `STR_AS_HoverHelp = "HoverHelp"`, default `true`; menu "View → Hover explanations" (checkable) and "View → Help panel" (the dock's toggle action).
- Turning hover explanations off removes only the new explanations; existing Qt tooltips and graph tooltips behave as today.
- Experimental measures carry the caveat "Experimental measure of OSCAR's analysis, not a medical norm."
- Real data only via `--datadir /Users/semyk/Documents/OSCAR20_Data_dev`; commit trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; fork-only docs with `git add -f`, own commit `(fork-only, not for MR)`.

## Review Focus

1. A `help:` link clicked on a page that already handles its own links (Statistics records, Daily "analysis=differences", home "daily=…") — the page must not navigate away or lose its other links (Task 4 test `testHelpLinkDoesNotNavigate`).
2. Hover explanations turned off while the help panel is open — the panel keeps working from its own search and "see also", pages show plain text (Task 3 test `testPanelWorksWithHoverOff`).
3. A channel with no glossary article (one of ~250 device channels) — tooltip falls back to the channel's description, never empty (Task 1 test `testChannelFallback`).
4. Statistics rows whose label is formatted (`Used Days %1%2 hrs/day`, AHI/RDI switch) — the help key must not depend on the formatted text (Task 4 test `testStatisticsRowKeys`).
5. HTML-escaping: article text with `<`, `%`, quotes inside a `title=` attribute — the page HTML stays valid (Task 2 test `testTermEscapes`).

---

### Task 1: Glossary

**Files:**
- Create: `oscar/glossary.h`, `oscar/glossary.cpp`; Modify: `oscar/oscar.pro`
- Test: `oscar/tests/glossarytests.{h,cpp}` (class `GlossaryTests`)

**Interfaces:**
- Produces:
```cpp
struct GlossaryEntry {
    QString key, term, expansion, summary, details, norm;
    bool experimental = false;
    QStringList seeAlso;   // keys
    QStringList channels;  // channel codes (schema code strings) this article explains
};
namespace Glossary {
const GlossaryEntry *find(const QString &key);   // translated; nullptr if unknown
QList<GlossaryEntry> all();
QString tooltip(const QString &key);            // "<b>term</b> — expansion<br>summary<br><i>Norm: …</i>"; "" if unknown
QString panel(const QString &key);              // headings "What it is", "How to read it", "Norm / guide", "Caveat", "See also" (links help:key)
QString keyForChannel(ChannelID code);          // via entry.channels vs schema::channel[code].code(); "" if none
QString channelTooltip(ChannelID code);         // tooltip(keyForChannel) or "<b>fullname</b><br>description"
QStringList search(const QString &text);        // keys; case-insensitive, ё=е, matches term/expansion/summary/details
}
```

- [ ] **Step 1: Failing tests** in `GlossaryTests`:
  - `testEntriesComplete` — every entry: non-empty key, term, summary, details; `summary.size() <= 300`; keys unique; every `seeAlso` key exists; `all().size() >= 75`.
  - `testRequiredKeys` — `find()` non-null for every key listed in Step 3.
  - `testTooltipAndPanel` — `tooltip("ahi")` contains "AHI" and the norm; `panel("glasgow")` contains the caveat and a `help:glasgow_adapted` link; `tooltip("nope")` is empty.
  - `testChannelFallback` — `keyForChannel(CPAP_Obstructive) == "oai"`; for a channel with no article (`CPAP_Test1`) `channelTooltip` contains its description and is non-empty.
  - `testSearch` — `search("утеч")` (with the Russian translator installed if available, else `search("leak")`) contains `"leak"`; `search("ё")`/`"е"` equivalence on a term containing "е".
Run `tools/runtests.sh GlossaryTests` → FAIL.

- [ ] **Step 2: Implement** the API over one static table of `QT_TRANSLATE_NOOP("Glossary", …)` strings; translate on access.

- [ ] **Step 3: Write the articles** (English source; content from OSCAR's docs, AASM definitions and our analysis; norms only where generally accepted, else "guide"). Keys (with channel codes where they explain a channel):
  - therapy: `usage` (hours), `compliance`, `sessions`, `mask_off`, `ahi`, `rdi`, `oai` [Obstructive], `cai` [ClearAirway], `uai` [Apnea], `all_apnea` [AllApnea], `hi` [Hypopnea], `oh` [ObstructiveHypopnea], `ch` [CentralHypopnea], `rera` [RERA], `fl_device` [FlowLimit], `flg` [FLG], `csr` [CSR, PB], `large_leak` [LargeLeak], `leak` [Leak], `leak_total` [LeakTotal], `leak_redline`, `pressure` [Pressure], `pressure_set` [PressureSet], `epap` [EPAP, EPAPSet], `ipap` [IPAP, IPAPSet], `ps` [PS], `pressure_max_time`, `mode`, `relief` (EPR / softPAP / IPR), `ramp`, `apap_range`, `resp_rate` [RespRate], `tidal_volume` [TidalVolume], `minute_vent` [MinuteVent], `ti_te` [Ti, Te], `ie_ratio` [IE], `snore` [Snore], `flow_rate` [FlowRate], `mask_pressure` [MaskPressure], `event_flags`, `sensawake` [SensAwake], `user_flags` [UserFlag1, UserFlag2];
  - statistics: `median`, `p95`, `maximum`, `wavg`, `nights_with_data`, `compliance_pct`, `period`;
  - oximetry: `spo2` [SPO2], `t90`, `odi3`, `odi4`, `spo2_nadir`, `pulse` [Pulse], `pulse_change` [PulseChange], `perfusion` [Perf. Index], `plethy` [Plethy], `spo2_drop` [SPO2Drop];
  - analysis (experimental): `second_opinion`, `an_ahi`, `hypopnea_rule`, `agreement`, `hypoxic_burden`, `oxi_zones`, `unexplained_desat`, `pulse_response`, `unscoreable` [AnUnscoreable], `an_flags`, `fl_score` [AnFLScore], `fl_time` [AnFlowLimitation], `fl_longest`, `fl_breaths`, `glasgow` [AnGlasgowIndex], `glasgow_adapted` [AnGlasgowAdapted], `gi_skew`, `gi_spike`, `gi_flattop`, `gi_topheavy`, `gi_multipeak`, `gi_nopause`, `gi_inspirrate`, `gi_multibreath`, `gi_ampvar`;
  - comparison: `best_value`, `few_nights`.
Run `tools/runtests.sh GlossaryTests` → PASS. Commit `"Glossary: explanations of OSCAR's figures and terms"`.

### Task 2: HelpTips — setting, widgets, HTML terms

**Files:** Create `oscar/helptips.{h,cpp}`; Modify `oscar/SleepLib/appsettings.{h,cpp}`, `oscar/mainwindow.{cpp,ui}` (View menu action), `oscar/oscar.pro`; Test `oscar/tests/helptipstests.{h,cpp}` (class `HelpTipsTests`)

**Interfaces:**
- Consumes: `Glossary::tooltip`.
- Produces:
```cpp
class HelpTips : public QObject {
    Q_OBJECT
public:
    static HelpTips *instance();
    bool enabled() const;            // AppSetting->hoverHelp()
    void setEnabled(bool on);        // saves the setting, emits enabledChanged
    static void attach(QWidget *w, const QString &key);   // property "helpKey"
    static QString term(const QString &text, const QString &key);   // <a href='help:key' title='…' style='color:inherit;text-decoration:none'>text</a>; text as is when disabled or key unknown
    static QString keyOf(const QUrl &url);   // "help:key" -> key; "" otherwise
    void hover(const QString &key);  // emits hovered(key) when non-empty
signals:
    void hovered(const QString &key);
    void openRequested(const QString &key);   // a help: link was clicked
    void enabledChanged(bool on);
protected:
    bool eventFilter(QObject *o, QEvent *e) override;   // on qApp: ToolTip -> QToolTip::showText(Glossary::tooltip); Enter -> hover
};
```
  `AppWideSetting::hoverHelp()` / `setHoverHelp(bool)`, key `"HoverHelp"`, default true. Menu action `actionHoverExplanations` ("Hover explanations", checkable) in View.

- [ ] **Step 1: Failing tests** `testAttachShowsTooltip` (send a `QHelpEvent` to a widget with key `"ahi"` → `QToolTip::text()` contains "AHI"; `hovered` spy gets `"ahi"` on `QEvent::Enter`), `testDisabled` (setEnabled(false) → no tooltip text from the filter, `term()` returns plain text), `testTerm` (link form, `keyOf(QUrl("help:leak")) == "leak"`), `testTermEscapes` (a title made from an article with `<`, `"`, `%` is escaped: parsing the result with `QTextDocument` keeps one anchor with the full text). Run → FAIL.
- [ ] **Step 2: Implement**; install the filter on `qApp` in `MainWindow` construction; wire the View action both ways (`enabledChanged`).
- [ ] **Step 3: Run** → PASS; suite green. Commit `"Hover explanations: the switch, widgets and HTML terms"`.

### Task 3: Help panel

**Files:** Create `oscar/helppanel.{h,cpp}`; Modify `oscar/mainwindow.{h,cpp,ui}`; Test `oscar/tests/helppaneltests.{h,cpp}` (class `HelpPanelTests`)

**Interfaces:**
- Consumes: `Glossary::panel/search/find`, `HelpTips::hovered/openRequested`.
- Produces: `class HelpPanel : public QDockWidget { explicit HelpPanel(QWidget *parent); void show(const QString &key); QString currentKey() const; }`; object name `"helpPanel"`; MainWindow adds it on the right, hidden by default; `toggleViewAction()` in View as "Help panel"; dock state saved with the window state.

- [ ] **Step 1: Failing tests** `testShowsHoveredArticle` (emit `hovered("odi3")` → `currentKey() == "odi3"`, browser text contains "ODI"), `testKeepsArticle` (hovered("") does not clear), `testSeeAlso` (anchor `help:glasgow_adapted` in the panel opens that article), `testSearch` (typing "glasgow" lists the two Glasgow articles; selecting one shows it), `testPanelWorksWithHoverOff` (HelpTips disabled → search and see-also still work), `testOpenRequestedShowsPanel` (openRequested("leak") makes the dock visible on "leak"). Run → FAIL.
- [ ] **Step 2: Implement**: dock with a search `QLineEdit`, result `QListWidget` (shown while searching), `QTextBrowser` (openLinks false; `help:` links → show); empty state "Hover over a figure or a term".
- [ ] **Step 3: Run** → PASS; suite green. Commit `"Help panel: the full explanation, see also and search"`.

### Task 4: HTML pages — home page, Daily sidebar, Statistics, doctor summary window

**Files:** Modify `oscar/nightsummary.cpp`, `oscar/daily.cpp`, `oscar/analysispanel.cpp`, `oscar/statistics.{h,cpp}`, `oscar/doctorreport.cpp` (window view only), `oscar/mytextbrowser.{h,cpp}`, `oscar/mainwindow.cpp`; Tests: `nightsummarytests`, `analysispaneltests`, `analysisintegrationtests` (Statistics), new `oscar/tests/helplinkstests.{h,cpp}`

**Interfaces:**
- Consumes: `HelpTips::term/keyOf/hover`, `Glossary::keyForChannel`.
- Produces: `MyTextBrowser` connects its own `highlighted(QUrl)` to `HelpTips::hover(keyOf(url))` and on a `help:` anchor emits `HelpTips::openRequested` and does not navigate (existing `anchorClicked` handlers return early on `help:`); `static QString Statistics::helpKey(const StatisticsRow &row)` (channel rows → `Glossary::keyForChannel`, `SC_ANALYSIS` rows → the analysis key map: ahi→an_ahi, fl→fl_time, flmin→fl_time, fllong→fl_longest, flbr→fl_breaths, gi→glasgow, gia→glasgow_adapted, hb→hypoxic_burden, zones→oxi_zones, agreement→agreement, odi3/odi4/nadir/below:*/pri/dhr → odi3/odi4/spo2_nadir/t90/pulse_change/pulse_response; non-channel calc rows by calc type: SC_TOTAL_DAYS/SC_DAYS_W_DATA/… → nights_with_data/compliance_pct/usage, SC_MEDIAN_AHI/SC_MEDIAN_HOURS → median, SC_AHI_RDI/SC_AHI_ONLY → ahi or rdi); `QList<StatisticsRow> Statistics::rowList() const`.

- [ ] **Step 1: Failing tests**:
  - `HelpLinksTests::testHelpLinkDoesNotNavigate` — a `MyTextBrowser` with HTML containing a `help:ahi` term and a normal `daily=…` link: clicking the help anchor emits `openRequested("ahi")`, the document is unchanged, `anchorClicked` handlers see no `help:` URL; hovering emits `hovered("ahi")`.
  - `AnalysisIntegrationTests::testStatisticsRowKeys` — for every row of `Statistics().rowList()` that is not a heading, message, header or space, `helpKey(row)` is non-empty or `Glossary::channelTooltip` of its channel is non-empty; `helpKey` of the `"Used Days %1%2 hrs/day"` row is `compliance_pct` (independent of formatting).
  - `NightSummaryTests::testTilesHaveHelp` — each tile title in the view's HTML is a `help:` term (usage, ahi, leak, pressure/p95, spo2/t90) and the AHI note's flow limitation and Glasgow parts link `fl_time` / `glasgow`.
  - `AnalysisPanelTests::testSidebarTerms` — sidebar HTML has `help:` links for an_ahi, hypopnea_rule, agreement, fl_time, glasgow and each of the 9 `gi_*`.
  Run → FAIL.
- [ ] **Step 2: Implement** the wrapping at the label sites (Statistics `name` in the row loop, event and statistic rows of the Daily sidebar via `Glossary::keyForChannel`, the analysis panel rows, the home tiles, the doctor summary window's tile titles and table headers — not the PDF).
- [ ] **Step 3: Run** → PASS; suite green. Commit `"Hover explanations on the home page, Daily sidebar, Statistics and doctor summary"`.

### Task 5: Graphs — Daily and Overview

**Files:** Modify `oscar/Graphs/gGraphView.cpp` (title / y-axis hover at ~2120-2140), `oscar/daily.cpp`, `oscar/overview.cpp`; Test `oscar/tests/helplinkstests.cpp`

**Interfaces:**
- Consumes: `Glossary::channelTooltip/keyForChannel/tooltip`, `HelpTips`.
- Produces: `gGraph::setHelpKey(const QString &)` / `helpKey()`; on hover over the title/y-axis margin, when HelpTips is enabled and the graph has a help key, the painted tooltip shows `Glossary::tooltip(key)` as plain text (tags stripped) and `HelpTips::hover(key)` is called; otherwise today's text. Daily sets each graph's key from its channel (`keyForChannel`), the analysis graphs to `an_flags`, `fl_score`, `glasgow`; Overview sets keys for its charts (AHI → ahi, usage → usage, leak → leak, pressure → pressure, SpO2 → spo2, analysis charts → an_ahi, odi3, t90, oxi_zones, hypoxic_burden, fl_time, fl_time, glasgow, pulse_change).

- [ ] **Step 1: Failing test** `HelpLinksTests::testGraphKeysComplete` — for every name in Daily's `standardGraphOrder` and the Overview analysis chart codes (`gAnalysisChart::kinds()`), the key resolver (`static QString Daily::helpKeyForGraph(const QString &name)` / `Overview::helpKeyForGraph`) returns a key `Glossary::find` knows, or the graph's channel has a non-empty `channelTooltip`. Run → FAIL.
- [ ] **Step 2: Implement**; keep the existing graph tooltip path when no key or disabled.
- [ ] **Step 3: Run** → PASS; suite green. Commit `"Hover explanations on the Daily and Overview graphs"`.

### Task 6: Translations, check in the app, handoff

- [ ] **Step 1:** `lupdate`; translate every unfinished entry (≈ 80 articles × up to 5 strings, the menu items, panel texts) into Russian — plain words for a patient, norms as in Russian clinical usage (AHI «индекс апноэ-гипопноэ», ODI «индекс десатураций», «ориентир» for non-standard guides); `summary` ≤ 300 chars; checks of Global Constraints = 0; `lrelease`.
- [ ] **Step 2: App check** (dev data, profile «Папа»): hover a home tile, a Statistics row, a Daily graph title, an analysis-panel row — tooltip shown; panel follows; "See also" and search work; turning "Hover explanations" off removes the tooltips and keeps the panel. Screenshots for the owner.
- [ ] **Step 3:** commit `"Translations: hover explanations and help panel"`; handoff section (fork-only commit).
