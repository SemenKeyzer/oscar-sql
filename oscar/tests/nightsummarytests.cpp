/* Start Screen Night Summary Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "nightsummarytests.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <cmath>

#include "nightsummary.h"
#include "helptips.h"
#include "glossary.h"
#include <QToolTip>
#include <QHelpEvent>

namespace {

// A good CPAP night: 7 h, AHI 1.8, leak well under the red line, 30 nights of history.
NightSummary goodNight()
{
    NightSummary s;
    s.date = QDate(2026, 9, 30);
    s.daysSinceData = 1;
    s.hasCpap = true;
    s.cpapDevice = QStringLiteral("ResMed AirSense 10");
    s.hours = 7.2;
    s.complianceHours = 4;
    s.ahi = 1.8;
    s.hasAnalysisAhi = true;
    s.analysisAhi = 2.1;
    s.hasLeak = true;
    s.leak = 4.2;
    s.leakRedline = 24;
    s.leakUnits = QStringLiteral("L/min");
    s.pressure = QStringLiteral("9.8");
    s.pressureUnits = QStringLiteral("cmH2O");
    s.pressureNote = QStringLiteral("95% of the time under");
    for (int i = 0; i < NightSummary::kTrendNights; ++i) {
        s.usage.append(i == 3 ? 0.0 : i == 10 ? 2.5 : 6.5 + (i % 3) * 0.5);
        s.ahiTrend.append(i == 3 ? std::nan("") : 1.0 + (i % 5) * 0.5);
    }
    return s;
}

} // namespace

void NightSummaryTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void NightSummaryTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

// Each figure is judged against its target: the profile's compliance hours and leak red
// line, and an AHI below 5. A figure without a target (no red line set) is not judged.
void NightSummaryTests::testLevelsAgainstTargets()
{
    NightSummary s = goodNight();
    QCOMPARE(s.usageLevel(), NightSummary::Good);
    QCOMPARE(s.ahiLevel(), NightSummary::Good);
    QCOMPARE(s.leakLevel(), NightSummary::Good);

    s.hours = 3.9;
    s.ahi = 5.0;
    s.leak = 24;
    QCOMPARE(s.usageLevel(), NightSummary::Attention);
    QCOMPARE(s.ahiLevel(), NightSummary::Attention);   // 5 is no longer "below 5"
    QCOMPARE(s.leakLevel(), NightSummary::Attention);

    s.leakRedline = 0;
    QCOMPARE(s.leakLevel(), NightSummary::Unknown);
    s.hasCpap = false;
    QCOMPARE(s.usageLevel(), NightSummary::Unknown);
    QCOMPARE(s.ahiLevel(), NightSummary::Unknown);
}

// The trends count the nights used long enough, and give the median AHI of the nights
// with data (a night without data is not an AHI of 0).
void NightSummaryTests::testTrendFigures()
{
    const NightSummary s = goodNight();
    QCOMPARE(s.compliantNights(), 28);                 // all but the unused night and the 2.5 h one
    NightSummary t;
    t.ahiTrend = { 3, std::nan(""), 1, 2, 10 };
    QCOMPARE(t.medianAhi(), 2.5);
    t.ahiTrend = { std::nan("") };
    QVERIFY(std::isnan(t.medianAhi()));
}

// A night within its targets has nothing to report; otherwise one sentence per figure off
// target, and the analysis' second opinion when it finds an AHI the device did not.
void NightSummaryTests::testConcerns()
{
    NightSummary s = goodNight();
    QVERIFY(s.concerns().isEmpty());

    s.analysisAhi = 6.4;                               // the device says 1.8
    QCOMPARE(s.concerns().size(), 1);
    QVERIFY(s.concerns().first().contains(QStringLiteral("6.4")));

    s.hours = 2.5;
    s.ahi = 12.3;
    s.leak = 30;
    const QStringList c = s.concerns();
    QCOMPARE(c.size(), 3);                             // usage, AHI (the device's), leak
    QVERIFY(c[0].contains(QStringLiteral("2 h 30 min")));
    QVERIFY(c[1].contains(QStringLiteral("12.3")));
    QVERIFY(c[2].contains(QStringLiteral("30.0")));
}

// What to do next comes with a link the main window knows how to follow.
void NightSummaryTests::testActions()
{
    NightSummary s = goodNight();
    QVERIFY(s.actions().isEmpty());
    s.outdatedAnalysis = 3;
    s.hasOffsetHint = true;
    s.offsetHintMs = -125000;
    s.daysSinceData = 9;
    const QStringList a = s.actions();
    QCOMPARE(a.size(), 3);
    QVERIFY(a[0].contains(QStringLiteral("href='analysis=recalculate'")));
    QVERIFY(a[1].contains(QStringLiteral("2 min")));
    QVERIFY(a[1].contains(QStringLiteral("href='daily=2026-09-30'")));
    QVERIFY(a[2].contains(QStringLiteral("href='import=cpap'")));
}

// The view shows a tile per figure the night has (at most five: next to the CPAP's, the
// pulse goes into the SpO2 tile), and keeps the trends and the to-do list only when there
// is something in them.
void NightSummaryTests::testViewShowsTheNight()
{
    NightSummaryView view;
    view.resize(900, 400);
    NightSummary s = goodNight();
    s.oxi.valid = true;
    s.oxi.fromAnalysis = true;
    s.oxi.hours = 6.5;
    s.oxi.spo2Avg = 95.1;
    s.oxi.spo2Min = 86;
    s.oxi.percentBelow90 = 0.4;
    s.oxi.desaturations = 14;
    s.oxi.pulseAvg = 61;
    s.oxi.pulseMin = 52;
    s.oxi.pulseMax = 96;
    view.setSummary(s);
    QCOMPARE(view.findChildren<QFrame *>(QStringLiteral("nsTile")).size(), 5);   // usage, AHI, leak, pressure, SpO2
    auto text = [&view](const QString &name) {
        QStringList out;
        for (QLabel *l : view.findChildren<QLabel *>(name)) out << l->text();
        return out.join(QLatin1Char('\n'));
    };
    QVERIFY(text(QStringLiteral("nsVerdict")).contains(QStringLiteral("Within your targets")));
    QVERIFY(text(QStringLiteral("nsValue")).startsWith(QStringLiteral("7<span")));    // 7 h 12 min, units smaller
    QVERIFY(text(QStringLiteral("nsValue")).contains(QStringLiteral("&nbsp;min</span>")));
    QVERIFY(text(QStringLiteral("nsNote")).contains(QStringLiteral("OSCAR's analysis: 2.1")));
    QVERIFY(text(QStringLiteral("nsNote")).contains(QStringLiteral("ODI 3%: 2.2 per hour")));
    QVERIFY(text(QStringLiteral("nsNote")).contains(QStringLiteral("pulse 61 (52 to 96)")));   // in the SpO2 tile
    QVERIFY(text(QStringLiteral("nsCaption")).contains(QStringLiteral("28 of 30 night")));
    QVERIFY(text(QStringLiteral("nsTitle")).contains(QStringLiteral("href='daily=2026-09-30'")));
    QVERIFY(view.findChild<QLabel *>(QStringLiteral("nsActions"))->isHidden());

    // an oximetry-only night: no CPAP tiles, no verdict, no trends
    NightSummary o;
    o.date = QDate(2026, 9, 30);
    o.daysSinceData = 1;
    o.oxi = s.oxi;
    o.oxi.fromAnalysis = false;
    o.outdatedAnalysis = 2;
    view.setSummary(o);
    QCOMPARE(view.findChildren<QFrame *>(QStringLiteral("nsTile")).size(), 2);   // SpO2, and pulse in a tile of its own
    QVERIFY(text(QStringLiteral("nsNote")).contains(QStringLiteral("SpO2 drops (classic)")));
    QVERIFY(view.findChild<QLabel *>(QStringLiteral("nsVerdict"))->isHidden());
    QVERIFY(view.findChild<QFrame *>(QStringLiteral("nsTrends"))->isHidden());
    QVERIFY(!view.findChild<QLabel *>(QStringLiteral("nsActions"))->isHidden());
}

// SpO2 is judged by the time below 90 % (under 5 % of the night) and by ODI 3 % (under 5 an
// hour, only when the analysis counted it: the classic drops use other thresholds). Spot
// checks and a night without oximetry are not judged.
void NightSummaryTests::testSpo2AgainstTargets()
{
    NightSummary s = goodNight();
    QCOMPARE(s.spo2Level(), NightSummary::Unknown);
    s.oxi.valid = true;
    s.oxi.spo2Avg = 95;
    s.oxi.hours = 8;
    s.oxi.fromAnalysis = true;
    s.oxi.percentBelow90 = 1.2;
    s.oxi.desaturations = 32;                          // 4 an hour
    QCOMPARE(s.spo2Level(), NightSummary::Good);
    QVERIFY(s.concerns().isEmpty());

    s.oxi.desaturations = 40;                          // 5 an hour: no longer below 5
    QCOMPARE(s.spo2Level(), NightSummary::Attention);
    QCOMPARE(s.concerns().size(), 1);
    QVERIFY(s.concerns().first().contains(QStringLiteral("ODI")));

    s.oxi.desaturations = 8;
    s.oxi.percentBelow90 = 5.0;
    QCOMPARE(s.spo2Level(), NightSummary::Attention);
    QCOMPARE(s.concerns().size(), 1);
    QVERIFY(s.concerns().first().contains(QStringLiteral("90%")));

    s.oxi.fromAnalysis = false;                        // classic drops are not ODI 3 %
    s.oxi.desaturations = 400;
    s.oxi.percentBelow90 = 1;
    QCOMPARE(s.spo2Level(), NightSummary::Good);
    s.oxi.spotChecks = true;
    QCOMPARE(s.spo2Level(), NightSummary::Unknown);
}

// The Daily view's sidebar strip: usage, leak, pressure and SpO2 as small tiles, each with
// the start screen's colour for its level; a night without CPAP shows only what it has.
void NightSummaryTests::testKeyFiguresHtml()
{
    NightSummary s = goodNight();
    s.oxi.valid = true;
    s.oxi.spo2Avg = 94;
    s.oxi.hours = 8;
    s.oxi.fromAnalysis = true;
    s.oxi.percentBelow90 = 1.0;
    s.oxi.desaturations = 160;                         // 20 an hour: attention
    const QString html = NightSummaryView::keyFiguresHtml(s);
    const int usage = html.indexOf(QStringLiteral("Usage")), leak = html.indexOf(QStringLiteral("Leak"));
    const int pressure = html.indexOf(QStringLiteral("Pressure")), spo2 = html.indexOf(QStringLiteral("SpO2 below 90%"));
    QVERIFY(usage >= 0 && leak > usage && pressure > leak && spo2 > pressure);
    const QString good = NightSummaryView::levelColor(NightSummary::Good).name();
    const QString attention = NightSummaryView::levelColor(NightSummary::Attention).name();
    QVERIFY(html.contains(good));                      // usage and leak within their targets
    QVERIFY(html.indexOf(attention, spo2) > spo2);     // SpO2 over the ODI target
    QVERIFY(html.contains(QStringLiteral("20.0")));    // ODI 3 % an hour

    s.hasCpap = false;
    s.hasLeak = false;
    s.pressure.clear();
    const QString oxiOnly = NightSummaryView::keyFiguresHtml(s);
    QVERIFY(!oxiOnly.contains(QStringLiteral("Usage")));
    QVERIFY(oxiOnly.contains(QStringLiteral("SpO2 below 90%")));
    QVERIFY(NightSummaryView::keyFiguresHtml(NightSummary()).isEmpty());
}

void NightSummaryTests::testPressureAtMaximum()
{
    // An APAP night that spent 25 minutes of its 7.2 hours at the upper limit of 14
    NightSummary s = goodNight();
    QVERIFY(s.pressureMaxNote().isEmpty());            // no limit known: nothing to say
    s.pressureMax = 14;
    s.secondsAtMax = 1500;
    const QString note = QCoreApplication::translate("NightSummary", "at the maximum %1: %2 (%3%)")
                             .arg(QStringLiteral("14"), QCoreApplication::translate("NightSummary", "%1 min").arg(25),
                                  QStringLiteral("5.8"));
    QCOMPARE(s.pressureMaxNote(), note);

    // over an hour reads in hours and minutes; never at the limit says so too
    s.secondsAtMax = 3900;
    QVERIFY(s.pressureMaxNote().contains(QCoreApplication::translate("NightSummary", "%1 h %2 min").arg(1).arg(5)));
    s.secondsAtMax = 0;
    QVERIFY(s.pressureMaxNote().contains(QCoreApplication::translate("NightSummary", "%1 min").arg(0)));

    // both tiles show it after the percentile note
    s.secondsAtMax = 1500;
    const QString html = NightSummaryView::keyFiguresHtml(s);
    QVERIFY2(html.contains((s.pressureNote + QStringLiteral("; ") + note).toHtmlEscaped()), qPrintable(html));
    NightSummaryView view;
    view.setSummary(s);
    bool shown = false;
    for (QLabel *l : view.findChildren<QLabel *>()) shown = shown || l->text().contains(note.toHtmlEscaped());
    QVERIFY(shown);
}

void NightSummaryTests::testFlowLimitationFromRow()
{
    AnalysisDailyData row;
    row.id = 1;
    row.hasFlow = true;
    row.flowSeconds = 32000;
    row.flSeconds = 1920;
    row.flBreaths = 5000;
    row.glasgow.breaths = 1000;
    row.glasgow.flagged[analysis::GiSkew] = 330;
    row.glasgowAdapted.breaths = 1000;
    row.glasgowAdapted.flagged[analysis::GiSkew] = 290;
    NightSummary s;
    s.takeFlowLimitation(row);
    QVERIFY(s.hasFlowLimitation);
    QCOMPARE(s.flPercent, 6.0);
    QCOMPARE(s.flMinutes, 32.0);
    QVERIFY(s.hasGlasgow);
    QVERIFY(!s.glasgowLessReliable);   // recorded at 0 Hz in this row: not known, not flagged
    QCOMPARE(s.glasgow, 0.33);
    QCOMPARE(s.glasgowAdapted, 0.29);

    row.flowRateHz = 10;
    NightSummary prisma;
    prisma.takeFlowLimitation(row);
    QVERIFY(prisma.glasgowLessReliable);

    row.flBreaths = 0;   // flow limitation not scored (below 10 Hz)
    row.glasgow = row.glasgowAdapted = analysis::GlasgowCounts();
    NightSummary none;
    none.takeFlowLimitation(row);
    QVERIFY(!none.hasFlowLimitation);
    QVERIFY(!none.hasGlasgow);
}

void NightSummaryTests::testFlowLimitationInAhiTile()
{
    NightSummaryView view;
    view.resize(900, 400);
    NightSummary s = goodNight();
    s.hasFlowLimitation = true;
    s.flPercent = 6;
    s.flMinutes = 32;
    s.hasGlasgow = true;
    s.glasgow = 3.28;
    s.glasgowAdapted = 2.9;
    view.setSummary(s);
    QStringList notes;
    for (QLabel *l : view.findChildren<QLabel *>(QStringLiteral("nsNote"))) notes << l->text();
    const QString all = notes.join(QLatin1Char('\n'));
    QVERIFY2(all.contains(NightSummaryView::tr("flow limitation %1% (%2 min)").arg(QStringLiteral("6"), QStringLiteral("32"))), qPrintable(all));
    QVERIFY2(all.contains(NightSummaryView::tr("Glasgow %1 / %2").arg(QStringLiteral("3.3"), QStringLiteral("2.9"))), qPrintable(all));
}

void NightSummaryTests::testGlasgowLessReliableInTile()
{
    NightSummaryView view;
    view.resize(900, 400);
    NightSummary s = goodNight();
    s.hasFlowLimitation = true;
    s.flPercent = 6;
    s.flMinutes = 32;
    s.hasGlasgow = true;
    s.glasgow = 2.1;
    s.glasgowAdapted = 1.9;
    s.glasgowLessReliable = true;
    view.setSummary(s);
    QStringList notes;
    for (QLabel *l : view.findChildren<QLabel *>(QStringLiteral("nsNote"))) notes << l->text();
    QVERIFY2(notes.join(QLatin1Char('\n')).contains(NightSummaryView::tr("(less reliable on this device)")), qPrintable(notes.join(QLatin1Char('|'))));
}

// Each tile explains its figure on hover; the AHI tile's flow limitation and Glasgow parts link
// their own explanations.
void NightSummaryTests::testTilesHaveHelp()
{
    NightSummaryView view;
    view.resize(900, 400);
    NightSummary s = goodNight();
    s.hasFlowLimitation = true;
    s.flPercent = 6;
    s.flMinutes = 32;
    s.hasGlasgow = true;
    s.glasgow = 2.1;
    s.glasgowAdapted = 1.9;
    view.setSummary(s);
    QStringList keys;
    for (QFrame *tile : view.findChildren<QFrame *>(QStringLiteral("nsTile"))) keys << tile->property("helpKey").toString();
    QVERIFY2(keys.contains(QStringLiteral("usage")) && keys.contains(QStringLiteral("ahi")) && keys.contains(QStringLiteral("leak"))
             && keys.contains(QStringLiteral("p95")), qPrintable(keys.join(QLatin1Char(','))));
    QStringList notes;
    for (QLabel *l : view.findChildren<QLabel *>(QStringLiteral("nsNote"))) notes << l->text();
    const QString all = notes.join(QLatin1Char('\n'));
    QVERIFY(all.contains(QStringLiteral("help:fl_time")));
    QVERIFY(all.contains(QStringLiteral("help:glasgow")));
}

// Hovering "Glasgow …" inside the AHI tile explains Glasgow, not AHI.
void NightSummaryTests::testTileLinkKeepsItsOwnTooltip()
{
    NightSummaryView view;
    view.resize(900, 400);
    NightSummary s = goodNight();
    s.hasFlowLimitation = true;
    s.flPercent = 6;
    s.flMinutes = 32;
    s.hasGlasgow = true;
    s.glasgow = 2.1;
    s.glasgowAdapted = 1.9;
    view.setSummary(s);
    QLabel *note = nullptr;
    for (QLabel *l : view.findChildren<QLabel *>(QStringLiteral("nsNote"))) {
        if (l->text().contains(QStringLiteral("help:glasgow"))) note = l;
    }
    QVERIFY(note);
    emit note->linkHovered(QStringLiteral("help:glasgow"));
    QCOMPARE(note->property("helpKey").toString(), QStringLiteral("glasgow"));
    emit note->linkHovered(QString());
    QVERIFY(note->property("helpKey").toString().isEmpty());
}

// The bars of the nights before explain themselves too.
void NightSummaryTests::testTrendCaptionsHaveHelp()
{
    NightSummaryView view;
    view.resize(900, 400);
    view.setSummary(goodNight());
    QStringList keys;
    for (QWidget *w : view.findChildren<QWidget *>()) {
        const QString k = w->property("helpKey").toString();
        if (!k.isEmpty()) keys << k;
    }
    QVERIFY2(keys.contains(QStringLiteral("compliance")) && keys.contains(QStringLiteral("median")), qPrintable(keys.join(QLatin1Char(','))));
}

// Hovering any part of a tile (its caption, figure or note) explains the tile's figure.
void NightSummaryTests::testTileTooltipFromInnerLabel()
{
    NightSummaryView view;
    view.resize(900, 400);
    view.setSummary(goodNight());
    view.show();
    QFrame *leak = nullptr;
    for (QFrame *tile : view.findChildren<QFrame *>(QStringLiteral("nsTile"))) {
        if (tile->property("helpKey").toString() == QLatin1String("leak")) leak = tile;
    }
    QVERIFY(leak);
    QLabel *caption = leak->findChild<QLabel *>(QStringLiteral("nsCaption"));
    QVERIFY(caption);
    QHelpEvent help(QEvent::ToolTip, QPoint(3, 3), caption->mapToGlobal(QPoint(3, 3)));
    QApplication::sendEvent(caption, &help);
    QVERIFY2(QToolTip::text().contains(Glossary::find(QStringLiteral("leak"))->term), qPrintable(QToolTip::text()));
    QToolTip::hideText();
}
