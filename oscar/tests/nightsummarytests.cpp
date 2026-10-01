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
