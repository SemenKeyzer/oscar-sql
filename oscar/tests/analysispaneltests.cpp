/* Sleep Analysis Panel Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysispaneltests.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTreeWidget>

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
