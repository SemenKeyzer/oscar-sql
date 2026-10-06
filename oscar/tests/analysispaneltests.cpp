/* Sleep Analysis Panel Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysispaneltests.h"

#include <cmath>

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QCheckBox>

#include "analysispanel.h"
#include "analysisprefs.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include "statistics.h"
#include "SleepLib/analysis/day_scorer.h"
#include "tests/analysis_synth.h"

using namespace analysis;

namespace {

qint64 at(double seconds) { return synth::kStart + qint64(seconds * 1000); }

DayEvent ev(double from, double to, RespEvent type, float value = 0)
{
    return DayEvent { at(from), at(to), type, 0, value, 0 };
}

// An hour of CPAP with apneas, a hypopnea, device events and an oximeter whose clock
// is two minutes behind (so there is an offset hint).
DayResult cpapNight()
{
    DayInput in;
    CpapSession s;
    s.span = Span { at(0), at(3600), 0 };
    s.analyzed = true;
    s.sampleRateHz = 25;
    s.flScored = true;
    s.flowSeconds = 3600;
    s.unscoreableSeconds = 120;
    in.cpap.append(s);
    in.spo2 = synth::flat(3600, 96);
    in.pulse = synth::flat(3600, 60);
    in.oxiFromSeparateDevice = true;
    const QVector<int> dips { 300, 420, 610, 700, 890, 1100, 1180, 1400, 1520, 1750, 1900, 2150 };
    for (int d : dips) synth::dip(in.spo2, d, 4, 5, 10, 5);
    for (const Desaturation &d : analyzeOximetry(in.spo2, Grid(), OxiParams()).desaturations) {
        const double end = (d.nadirTime - synth::kStart) / 1000.0 + 100;
        in.apneas.append(ev(end - 15, end, RespEvent::ObstructiveApnea));
    }
    in.candidates.append(ev(3000, 3020, RespEvent::Hypopnea, 0.6f));
    in.deviceEvents = { ev(in.apneas[0].start / 1000.0 - synth::kStart / 1000.0, in.apneas[0].end / 1000.0 - synth::kStart / 1000.0,
                           RespEvent::ObstructiveApnea),
                        ev(3300, 3312, RespEvent::Hypopnea) };
    return scoreDay(in, AnalysisParams());
}

DayResult oximetryNight()
{
    DayInput in;
    in.spo2 = synth::flat(3600, 95);
    synth::fill(in.spo2, 1000, 1100, 84);   // a marked problem zone
    in.pulse = synth::flat(3600, 58);
    return scoreDay(in, AnalysisParams());
}

} // namespace

void AnalysisPanelTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void AnalysisPanelTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void AnalysisPanelTests::testFormatting()
{
    QCOMPARE(AnalysisPanel::duration(125000), QStringLiteral("2m 05s"));
    QCOMPARE(AnalysisPanel::duration(3720000), QStringLiteral("1h 02m"));
    QCOMPARE(AnalysisPanel::offset(120000), QStringLiteral("+0:02:00"));
    QCOMPARE(AnalysisPanel::offset(-5430000), QStringLiteral("-1:30:30"));
}

void AnalysisPanelTests::testSidebarForCpapNight()
{
    const DayResult r = cpapNight();
    QVERIFY(r.hasFlow && r.hasOximetry && r.hasOffsetHint);
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QStringLiteral("CMS50F"), { 94, 90 });
    QVERIFY(html.contains(AnalysisPanel::disclaimer().toHtmlEscaped()));
    QVERIFY(html.contains(QStringLiteral("AHI")));
    QVERIFY(html.contains(QStringLiteral("Hypopnea index by rule")));
    QVERIFY(html.contains(QStringLiteral("href='analysis=differences'")));
    QVERIFY(html.contains(QStringLiteral("below 94 %")));
    QVERIFY(html.contains(QStringLiteral("below 90 %")));
    QVERIFY(html.contains(QStringLiteral("Hypoxic burden (approx.)")));
    QVERIFY(html.contains(QStringLiteral("CMS50F")));
    QVERIFY(html.contains(QStringLiteral("href='align=oximeter'")));
    QVERIFY(html.contains(AnalysisPanel::offset(r.offsetHintMs)));
    QVERIFY(html.contains(QStringLiteral("Unscoreable time")));
}

void AnalysisPanelTests::testSidebarForOximetryOnlyNight()
{
    const DayResult r = oximetryNight();
    QVERIFY(!r.hasCpap && r.hasOximetry);
    QCOMPARE(r.oxi.zones.size(), 1);
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QStringLiteral("CMS50F"), { 94, 90, 85 });
    QVERIFY(!html.contains(QStringLiteral("AHI")));                    // no CPAP rows ...
    QVERIFY(!html.contains(QStringLiteral("analysis=differences")));   // ... nor comparison
    QVERIFY(!html.contains(QStringLiteral("Hypoxic burden")));
    QVERIFY(html.contains(QStringLiteral("Problem zones")));
    QVERIFY(html.contains(QStringLiteral("below 85 %")));
    QVERIFY(html.contains(QStringLiteral("&lt; 85 %")));
    QVERIFY(html.contains(QStringLiteral("Pulse")));
}

void AnalysisPanelTests::testTabListsDifferences()
{
    AnalysisTab tab;
    tab.setResult(cpapNight());
    QTreeWidget *tree = tab.findChild<QTreeWidget *>();
    QVERIFY(tree != nullptr);
    QStringList groups;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) groups << tree->topLevelItem(i)->text(0);
    QVERIFY(groups.filter(QStringLiteral("Device only")).size() == 1);
    QVERIFY(groups.filter(QStringLiteral("Analysis only")).size() == 1);
    QVERIFY(groups.filter(QStringLiteral("Different type")).size() == 1);

    // a click shows the event with a minute around it
    QSignalSpy spy(&tab, &AnalysisTab::showRange);
    QTreeWidgetItem *deviceOnly = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0).startsWith(QStringLiteral("Device only"))) deviceOnly = tree->topLevelItem(i);
    }
    QVERIFY(deviceOnly && deviceOnly->childCount() == 1);   // the hypopnea at 3300 s
    emit tree->itemClicked(deviceOnly->child(0), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toLongLong(), at(3300) - 60000);
    QCOMPARE(spy.at(0).at(1).toLongLong(), at(3312) + 60000);

    // a click on a group does nothing
    emit tree->itemClicked(deviceOnly, 0);
    QCOMPARE(spy.count(), 1);

    tab.setResult(oximetryNight());
    QCOMPARE(tree->topLevelItemCount(), 1);   // the problem zone
    tab.clear();
    QCOMPARE(tree->topLevelItemCount(), 0);
}

// Previous / Next step through the differences from the device in time order, across
// their groups, showing each in the graphs as a click would.
void AnalysisPanelTests::testTabStepsThroughDifferences()
{
    AnalysisTab tab;
    QPushButton *prev = nullptr, *next = nullptr;
    for (QPushButton *b : tab.findChildren<QPushButton *>()) {
        if (b->text().contains(QStringLiteral("Previous"))) prev = b;
        if (b->text().contains(QStringLiteral("Next"))) next = b;
    }
    QVERIFY(prev && next);
    QVERIFY(!next->isEnabled());                       // nothing yet
    tab.setResult(cpapNight());
    QTreeWidget *tree = tab.findChild<QTreeWidget *>();
    int differences = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QString title = tree->topLevelItem(i)->text(0);
        if (title.startsWith(QStringLiteral("Device only")) || title.startsWith(QStringLiteral("Analysis only"))
                || title.startsWith(QStringLiteral("Different type"))) {
            differences += tree->topLevelItem(i)->childCount();
        }
    }
    QVERIFY(differences >= 2);
    QVERIFY(next->isEnabled());

    QSignalSpy spy(&tab, &AnalysisTab::showRange);
    QList<qint64> starts;
    for (int i = 0; i < differences; ++i) {
        next->click();
        QCOMPARE(spy.count(), i + 1);
        starts << spy.last().at(0).toLongLong();
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toLongLong() - starts.last() >= 60000, true);
    }
    for (int i = 1; i < starts.size(); ++i) QVERIFY(starts[i - 1] <= starts[i]);   // in time order
    QVERIFY(!next->isEnabled());                       // at the last one
    prev->click();
    QCOMPARE(spy.last().at(0).toLongLong(), starts[differences - 2]);
    QVERIFY(next->isEnabled());

    tab.clear();
    QVERIFY(!prev->isEnabled() && !next->isEnabled());
}

void AnalysisPanelTests::testStatisticsFigures()
{
    // A 1-hour night with 5 obstructive apneas and a 2-hour night with one: the period's
    // index is 6 events over 3 hours, not the mean of 5 and 0.5.
    AnalysisDailyData a, b;
    a.hasFlow = b.hasFlow = true;
    a.flowSeconds = 3600;
    b.flowSeconds = 7200;
    a.nObstructiveApnea = 5;
    b.nObstructiveApnea = 1;
    b.nHypopnea = 3;
    a.flBreaths = 100;
    a.flSeconds = 360;           // 10 % of night a; night b has no flow limitation scored
    a.hasComparison = b.hasComparison = true;
    a.cmpMatched = 4;
    a.cmpDeviceOnly = 1;
    b.cmpAnalysisOnly = 3;
    a.hasOximetry = true;
    a.oxiSeconds = 3600;
    a.nDesat3 = 6;
    a.nDesat4 = 3;
    a.spo2Nadir = 86;
    a.spo2Hist = QVector<int>(51, 0);
    a.spo2Hist[96 - 50] = 3240;
    a.spo2Hist[89 - 50] = 360;   // 10 % below 90
    const QList<AnalysisDailyData> rows { a, b };

    QCOMPARE(analysisFigure(QStringLiteral("oai"), rows), QStringLiteral("2.00"));
    QCOMPARE(analysisFigure(QStringLiteral("ahi"), rows), QStringLiteral("3.00"));
    QCOMPARE(analysisFigure(QStringLiteral("hi"), rows), QStringLiteral("1.00"));
    QCOMPARE(analysisFigure(QStringLiteral("fl"), rows), QStringLiteral("10.00"));
    QCOMPARE(analysisFigure(QStringLiteral("agreement"), rows), QStringLiteral("50.00"));   // 4 of 8
    QCOMPARE(analysisFigure(QStringLiteral("odi3"), rows), QStringLiteral("6.00"));
    QCOMPARE(analysisFigure(QStringLiteral("odi4"), rows), QStringLiteral("3.00"));
    QCOMPARE(analysisFigure(QStringLiteral("below:90"), rows), QStringLiteral("10.00"));
    QCOMPARE(analysisFigure(QStringLiteral("below:97"), rows), QStringLiteral("100.00"));
    QCOMPARE(analysisFigure(QStringLiteral("nadir"), rows), QStringLiteral("86"));
    QCOMPARE(analysisFigure(QStringLiteral("pri"), rows), QStringLiteral("-"));   // no pulse
    QCOMPARE(analysisFigure(QStringLiteral("ahi"), {}), QStringLiteral("-"));
}

void AnalysisPanelTests::testStatisticsFigureValues()
{
    // the numbers behind the Statistics figures, for tables that compare them
    AnalysisDailyData a, b;
    a.hasFlow = b.hasFlow = true;
    a.flowSeconds = 3600;
    b.flowSeconds = 7200;
    a.nObstructiveApnea = 5;
    b.nObstructiveApnea = 1;
    a.hasOximetry = true;
    a.oxiSeconds = 3600;
    a.nDesat3 = 6;
    a.spo2Nadir = 86;
    a.spo2Hist = QVector<int>(51, 0);
    a.spo2Hist[96 - 50] = 3240;
    a.spo2Hist[89 - 50] = 360;   // 10 % below 90
    const QList<AnalysisDailyData> rows { a, b };

    QCOMPARE(analysisFigureValue(QStringLiteral("ahi"), rows), 2.0);    // 6 events over 3 hours
    QCOMPARE(analysisFigureValue(QStringLiteral("odi3"), rows), 6.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("below:90"), rows), 10.0);
    QCOMPARE(analysisFigureValue(QStringLiteral("nadir"), rows), 86.0);
    QVERIFY(std::isnan(analysisFigureValue(QStringLiteral("pri"), rows)));   // no pulse
    QVERIFY(std::isnan(analysisFigureValue(QStringLiteral("ahi"), {})));

    // the text figures are these numbers, formatted
    QCOMPARE(analysisFigure(QStringLiteral("ahi"), rows), QStringLiteral("2.00"));
    QCOMPARE(analysisFigure(QStringLiteral("nadir"), rows), QStringLiteral("86"));
    QCOMPARE(analysisFigure(QStringLiteral("pri"), rows), QStringLiteral("-"));
}

void AnalysisPanelTests::testPreferencesPage()
{
    AnalysisPreferencesPage page;
    // a fresh page shows the defaults
    const AnalysisParams defaults;
    QCOMPARE(page.params().flowHash(), defaults.flowHash());
    QCOMPARE(page.params().oxiHash(), defaults.oxiHash());
    QCOMPARE(page.params().dayHash(), defaults.dayHash());

    AnalysisParams p;
    p.enabled = false;
    p.day.rule = HypopneaRule::Cms4;
    p.day.pulseRiseAsArousal = true;
    p.flow.hypopneaReduction = 0.35;   // shown as 35 %
    p.flow.flThreshold = 0.45;
    p.oxi.zoneMinDesats = 5;
    page.load(p, { 92, 88 });
    const AnalysisParams back = page.params();
    QCOMPARE(back.enabled, false);
    QVERIFY(back.day.rule == HypopneaRule::Cms4);
    QCOMPARE(back.flowHash(), p.flowHash());
    QCOMPARE(back.oxiHash(), p.oxiHash());
    QCOMPARE(back.dayHash(), p.dayHash());
    QCOMPARE(page.spo2Thresholds(), QList<double>({ 92, 88 }));

    // the description follows the selected rule
    QComboBox *rule = page.findChild<QComboBox *>();
    QVERIFY(rule != nullptr);
    rule->setCurrentIndex(rule->findData(int(HypopneaRule::FlowOnly)));
    bool shown = false;
    for (QLabel *label : page.findChildren<QLabel *>()) {
        shown = shown || label->text() == AnalysisPreferencesPage::ruleDescription(HypopneaRule::FlowOnly);
    }
    QVERIFY(shown);
    QVERIFY(page.params().day.rule == HypopneaRule::FlowOnly);

    // thresholds as typed: cleaned up, highest first, at most six
    QLineEdit *thresholds = page.findChild<QLineEdit *>();
    QVERIFY(thresholds != nullptr);
    thresholds->setText(QStringLiteral("85, 94, x, 120, 90, 88, 80, 75, 70"));
    QCOMPARE(page.spo2Thresholds(), QList<double>({ 94, 90, 88, 85, 80, 75 }));
}

// What the flow graph marks: each difference from the device with its kind, its span and a
// short label, in time order. Matched events of the same group are no difference.
void AnalysisPanelTests::testDifferenceSpans()
{
    DayResult r;
    r.hasComparison = true;
    r.deviceEvents = { ev(10, 20, RespEvent::ObstructiveApnea), ev(30, 40, RespEvent::Hypopnea),
                       ev(50, 60, RespEvent::ObstructiveApnea), ev(80, 90, RespEvent::ObstructiveApnea) };
    r.analysisEvents = { ev(11, 21, RespEvent::ObstructiveApnea), ev(51, 58, RespEvent::ObstructiveHypopnea),
                         ev(70, 75, RespEvent::Hypopnea), ev(81, 92, RespEvent::CentralApnea) };
    r.match.matched = { { 0, 0 }, { 2, 1 }, { 3, 3 } };   // OA-aOA, OA-aOH (other group), OA-aCA (same group)
    r.match.deviceOnly = { 1 };
    r.match.analysisOnly = { 2 };
    r.match.typeMismatch = 1;

    const QVector<DifferenceSpan> d = AnalysisPanel::differences(r);
    QCOMPARE(d.size(), 3);
    QCOMPARE(int(d[0].kind), int(DifferenceSpan::DeviceOnly));
    QCOMPARE(d[0].start, at(30));
    QCOMPARE(d[0].end, at(40));
    QCOMPARE(d[0].label(), QStringLiteral("H"));
    QCOMPARE(int(d[1].kind), int(DifferenceSpan::DifferentType));
    QCOMPARE(d[1].start, at(50));                      // the union of both events
    QCOMPARE(d[1].end, at(60));
    QCOMPARE(d[1].label(), QStringLiteral("OA \u2194 aOH"));
    QCOMPARE(int(d[2].kind), int(DifferenceSpan::AnalysisOnly));
    QCOMPARE(d[2].start, at(70));
    QCOMPARE(d[2].label(), QStringLiteral("aH"));

    r.hasComparison = false;
    QVERIFY(AnalysisPanel::differences(r).isEmpty());
}

// The flow graph draws the difference the tab is showing stronger: stepping tells it which
// one (its own span, without the context around it), and a problem zone clears it. The
// check box switches the marks on the flow graph on and off.
void AnalysisPanelTests::testTabTellsTheFlowGraphWhichDifference()
{
    AnalysisTab tab;
    const DayResult night = cpapNight();
    tab.setResult(night);
    const QVector<DifferenceSpan> diffs = AnalysisPanel::differences(night);
    QVERIFY(!diffs.isEmpty());
    QSignalSpy shown(&tab, &AnalysisTab::differenceShown);
    tab.stepDifference(1);
    QCOMPARE(shown.count(), 1);
    QCOMPARE(shown.last().at(0).toLongLong(), diffs.first().start);
    QCOMPARE(shown.last().at(1).toLongLong(), diffs.first().end);

    QCheckBox *onFlow = tab.findChild<QCheckBox *>();
    QVERIFY(onFlow != nullptr);
    QVERIFY(onFlow->isChecked());
    QSignalSpy toggled(&tab, &AnalysisTab::showOnFlowChanged);
    onFlow->click();
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.last().at(0).toBool(), false);
}


void AnalysisPanelTests::testFlowLimitationLine()
{
    DayResult r = cpapNight();
    r.flScored = true;
    r.flowSeconds = 27000;
    r.flSeconds = 6300;
    r.flLongestSeconds = 720;
    r.flBreaths = 1000;
    r.flLimitedBreaths = 310;
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QString(), {});
    QVERIFY(html.contains(AnalysisPanel::tr("Flow limitation: %1, longest run %2; %3% of breaths")
                              .arg(AnalysisPanel::duration(6300000), AnalysisPanel::duration(720000), QStringLiteral("31"))));
}

void AnalysisPanelTests::testGlasgowRows()
{
    DayResult r = cpapNight();
    r.flScored = true;
    r.glasgow.breaths = 1000;
    r.glasgow.flagged[GiFlatTop] = 400;
    r.glasgow.flagged[GiTopHeavy] = 300;
    r.glasgowAdapted.breaths = 1000;
    r.glasgowAdapted.flagged[GiFlatTop] = 150;
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QString(), {});
    QVERIFY(html.contains(AnalysisPanel::tr("Glasgow Index")));
    QVERIFY(html.contains(QStringLiteral("0.40")));   // original: flat top only
    QVERIFY(html.contains(QStringLiteral("0.15")));   // adapted
    for (const QString &name : { AnalysisPanel::tr("Skew"), AnalysisPanel::tr("Spike"), AnalysisPanel::tr("Flat top"),
                                 AnalysisPanel::tr("Top heavy (not in the sum)"), AnalysisPanel::tr("Double peak"),
                                 AnalysisPanel::tr("No pause"), AnalysisPanel::tr("Inspiration rate"),
                                 AnalysisPanel::tr("Double inspiration"), AnalysisPanel::tr("Variable amplitude") })
        QVERIFY2(html.contains(name), qPrintable(name));
    QVERIFY(html.contains(AnalysisPanel::tr("Author's scale: 0–0.2 clean breathing, about 3 serious problems. Experimental, not reviewed by physicians.")));
}

void AnalysisPanelTests::testGlasgowDash()
{
    DayResult r = cpapNight();
    r.flScored = false;
    r.glasgow = r.glasgowAdapted = GlasgowCounts();
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QString(), {});
    QVERIFY(html.contains(AnalysisPanel::tr("Glasgow Index")));
    QVERIFY(html.contains(AnalysisPanel::tr("Flow limitation and cardiogenic oscillations need 10 Hz: not scored.")));
    QVERIFY(!html.contains(AnalysisPanel::tr("Flat top")));
    QVERIFY(html.contains(AnalysisPanel::tr("Not computed: the flow is recorded below 10 Hz.")));
    QVERIFY(!html.contains(AnalysisPanel::tr("Author's scale: 0–0.2 clean breathing, about 3 serious problems. Experimental, not reviewed by physicians.")));
}

void AnalysisPanelTests::testFlowLimitationLineOldStamp()
{
    DayResult r = cpapNight();
    r.flScored = true;
    r.flowSeconds = 27000;
    r.flSeconds = 6300;
    r.flLongestSeconds = 720;
    r.flBreaths = 1000;
    r.flLimitedBreaths = -1;   // not known
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QString(), {});
    QVERIFY(html.contains(AnalysisPanel::tr("Flow limitation: %1, longest run %2")
                              .arg(AnalysisPanel::duration(6300000), AnalysisPanel::duration(720000))));
    QVERIFY(!html.contains(QStringLiteral("-0%")));
}

void AnalysisPanelTests::testGlasgowLessReliableBelowTwentyHz()
{
    DayResult r = cpapNight();
    r.flScored = true;
    r.glasgow.breaths = r.glasgowAdapted.breaths = 1000;
    const QString note = AnalysisPanel::tr("Less reliable here: the flow is recorded at %1 Hz, the method was made for 25 Hz.");
    r.flowRateHz = 25;
    QVERIFY(!AnalysisPanel::sidebarHtml(nullptr, r, QString(), {}).contains(note.arg(25)));
    r.flowRateHz = 10;
    QVERIFY(AnalysisPanel::sidebarHtml(nullptr, r, QString(), {}).contains(note.arg(10)));
}

void AnalysisPanelTests::testSidebarTerms()
{
    DayResult r = cpapNight();
    r.flScored = true;
    r.flBreaths = 1000;
    r.flLimitedBreaths = 300;
    r.glasgow.breaths = r.glasgowAdapted.breaths = 1000;
    const QString html = AnalysisPanel::sidebarHtml(nullptr, r, QStringLiteral("CMS50F"), { 90 });
    for (const char *key : { "an_ahi", "hypopnea_rule", "agreement", "fl_time", "glasgow", "glasgow_adapted", "gi_skew",
                             "gi_spike", "gi_flattop", "gi_topheavy", "gi_multipeak", "gi_nopause", "gi_inspirrate",
                             "gi_multibreath", "gi_ampvar", "odi3", "hypoxic_burden" }) {
        QVERIFY2(html.contains(QStringLiteral("help:%1'").arg(QLatin1String(key))), key);
    }
}
