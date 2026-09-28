/* Sleep Analysis Oximetry Analyzer Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oxianalyzertests.h"

#include "SleepLib/analysis/oxi_analyzer.h"
#include "tests/analysis_synth.h"

using namespace analysis;

namespace {

OxiResult analyze(const Grid &spo2, const Grid &pulse = Grid())
{
    return analyzeOximetry(spo2, pulse, OxiParams());
}

int secondsOf(qint64 from, qint64 to) { return int((to - from) / 1000); }

} // namespace

void OxiAnalyzerTests::testSmallDropIsNotDesaturation()
{
    Grid s = synth::flat(600, 96);
    synth::dip(s, 100, 2, 3, 30, 3);
    const OxiResult r = analyze(s);
    QCOMPARE(r.countDesaturations(3), 0);
    QCOMPARE(r.spo2Nadir, 94.0f);
}

void OxiAnalyzerTests::testThreePercentDropCountsForOdi3Only()
{
    Grid s = synth::flat(600, 96);
    synth::dip(s, 100, 3, 3, 20, 3);
    const OxiResult r = analyze(s);
    QCOMPARE(r.desaturations.size(), 1);
    const Desaturation &d = r.desaturations.first();
    QCOMPARE(d.depth(), 3.0f);
    QCOMPARE(r.countDesaturations(3), 1);
    QCOMPARE(r.countDesaturations(4), 0);
    // the fall starts ~101 s, the recovery to 95 % is at ~127 s
    QVERIFY2(qAbs(secondsOf(s.start, d.start) - 101) <= 2, qPrintable(QString::number(secondsOf(s.start, d.start))));
    QVERIFY2(qAbs(secondsOf(s.start, d.end) - 127) <= 2, qPrintable(QString::number(secondsOf(s.start, d.end))));
    QVERIFY(d.area > 0);
}

void OxiAnalyzerTests::testFivePercentDropCountsForBoth()
{
    Grid s = synth::flat(600, 96);
    synth::dip(s, 100, 5, 5, 30, 5);
    const OxiResult r = analyze(s);
    QCOMPARE(r.desaturations.size(), 1);
    QCOMPARE(r.desaturations.first().depth(), 5.0f);
    QCOMPARE(r.countDesaturations(4), 1);
    QVERIFY(r.cyclic.isEmpty());
}

void OxiAnalyzerTests::testSeriesOfDropsIsCyclic()
{
    Grid s = synth::flat(900, 96);
    for (int k = 0; k < 5; ++k) synth::dip(s, 100 + 45 * k, 4, 4, 15, 4);
    const OxiResult r = analyze(s);
    QCOMPARE(r.countDesaturations(3), 5);
    QCOMPARE(r.cyclic.size(), 1);
    QCOMPARE(int(r.cyclic.first().value), 5);
    QCOMPARE(r.cyclic.first().start, r.desaturations.first().start);
    QCOMPARE(r.cyclic.first().end, r.desaturations.last().end);
}

void OxiAnalyzerTests::testSlowDriftIsNotEventButCountsBelow90()
{
    Grid s = synth::flat(1200, 96);
    synth::ramp(s, 300, 900, 96, 89);
    synth::fill(s, 900, 1200, 89);
    const OxiResult r = analyze(s);
    QCOMPARE(r.countDesaturations(3), 0);
    QVERIFY2(r.spo2SecondsBelow(90) >= 300, qPrintable(QString::number(r.spo2SecondsBelow(90))));
    QCOMPARE(r.spo2Nadir, 89.0f);
}

void OxiAnalyzerTests::testSingleSpikeIsIgnored()
{
    Grid s = synth::flat(300, 96);
    s.v[150] = 70;
    const OxiResult r = analyze(s);
    QCOMPARE(r.desaturations.size(), 0);
    QCOMPARE(r.spo2Nadir, 96.0f);
    QCOMPARE(r.spo2Hist[70 - kSpo2HistMin], 0);
}

void OxiAnalyzerTests::testShortSegmentsAreDropped()
{
    // 100 s of data, a 20 s gap, then only 30 s: too short to use.
    Grid s = synth::flat(150, 95);
    synth::fill(s, 100, 120, kNoData);
    const OxiResult r = analyze(s);
    QCOMPARE(r.spo2Seconds, 100);
}

void OxiAnalyzerTests::testTimeBelowThresholdsIsExact()
{
    Grid s = synth::flat(340, 95);
    synth::fill(s, 120, 180, 89);
    synth::fill(s, 180, 220, 84);
    const OxiResult r = analyze(s);
    QCOMPARE(r.spo2Seconds, 340);
    QCOMPARE(r.spo2SecondsBelow(90), 100);
    QCOMPARE(r.spo2SecondsBelow(88), 40);
    QCOMPARE(r.spo2SecondsBelow(85), 40);
    QCOMPARE(r.spo2SecondsBelow(80), 0);
    QCOMPARE(r.spo2SecondsBelow(96), 340);
    QCOMPARE(r.spo2Hist[89 - kSpo2HistMin], 60);
}

void OxiAnalyzerTests::testPulseRiseDetected()
{
    const Grid s = synth::flat(300, 96);
    Grid p = synth::flat(300, 60);
    synth::ramp(p, 120, 125, 60, 70);
    synth::fill(p, 125, 135, 70);
    synth::ramp(p, 135, 140, 70, 60);
    const OxiResult r = analyze(s, p);
    QCOMPARE(r.pulseRises.size(), 1);
    QVERIFY2(qAbs(r.pulseRises.first().amplitude - 10) <= 1, qPrintable(QString::number(r.pulseRises.first().amplitude)));
    QCOMPARE(r.pulseMin, 60.0f);
    QCOMPARE(r.pulseMax, 70.0f);
}

void OxiAnalyzerTests::testSmallPulseRiseIgnored()
{
    const Grid s = synth::flat(300, 96);
    Grid p = synth::flat(300, 60);
    synth::ramp(p, 120, 125, 60, 64);
    synth::fill(p, 125, 135, 64);
    synth::ramp(p, 135, 140, 64, 60);
    QCOMPARE(analyze(s, p).pulseRises.size(), 0);
}

void OxiAnalyzerTests::testBradycardiaSpan()
{
    const Grid s = synth::flat(240, 96);
    Grid p = synth::flat(240, 60);
    synth::fill(p, 100, 140, 35);
    const OxiResult r = analyze(s, p);
    QCOMPARE(r.bradycardia.size(), 1);
    const int len = secondsOf(r.bradycardia.first().start, r.bradycardia.first().end);
    QVERIFY2(len >= 36 && len <= 42, qPrintable(QString::number(len)));
    QVERIFY(r.tachycardia.isEmpty());
}

void OxiAnalyzerTests::testDesaturationSeriesMakesOneZone()
{
    Grid s = synth::flat(1800, 96);
    for (int k = 0; k < 4; ++k) synth::dip(s, 600 + 60 * k, 4, 4, 15, 4);
    const OxiResult r = analyze(s);
    QCOMPARE(r.zones.size(), 1);
    const ProblemZone &z = r.zones.first();
    QCOMPARE(z.severity, 1);
    QCOMPARE(z.desaturations, 4);
    QCOMPARE(z.minSpo2, 92.0f);
    QCOMPARE(z.start, r.desaturations.first().start);
    QCOMPARE(z.end, r.desaturations.last().end);
}

void OxiAnalyzerTests::testNinetySecondsBelow90MakesModerateZone()
{
    Grid s = synth::flat(1800, 96);
    synth::fill(s, 600, 690, 88);
    const OxiResult r = analyze(s);
    QCOMPARE(r.zones.size(), 1);
    const ProblemZone &z = r.zones.first();
    QCOMPARE(z.severity, 1);
    QCOMPARE(z.lowSeconds, 90);
    QVERIFY(secondsOf(z.start, z.end) >= 120);   // widened to the shortest zone
    QVERIFY(z.start <= s.timeAt(600) && z.end >= s.timeAt(690));
}

void OxiAnalyzerTests::testFortySecondsBelow85MakesMarkedZone()
{
    Grid s = synth::flat(1800, 96);
    synth::fill(s, 600, 640, 84);
    const OxiResult r = analyze(s);
    QCOMPARE(r.zones.size(), 1);
    QCOMPARE(r.zones.first().severity, 2);
    QCOMPARE(r.zones.first().criticalSeconds, 40);
    QCOMPARE(r.zoneSeconds(2), r.zoneSeconds(1));
}

void OxiAnalyzerTests::testCloseZonesMerge()
{
    Grid s = synth::flat(2400, 96);
    synth::fill(s, 600, 690, 88);
    synth::fill(s, 750, 840, 88);   // a minute after the first
    const OxiResult r = analyze(s);
    QCOMPARE(r.zones.size(), 1);
    QCOMPARE(r.zones.first().lowSeconds, 180);
}

void OxiAnalyzerTests::testSingleDesaturationMakesNoZone()
{
    Grid s = synth::flat(1800, 96);
    synth::dip(s, 600, 4, 4, 15, 4);
    const OxiResult r = analyze(s);
    QCOMPARE(r.desaturations.size(), 1);
    QVERIFY(r.zones.isEmpty());
}

void OxiAnalyzerTests::testOximetryWithoutPulse()
{
    Grid s = synth::flat(1800, 96);
    synth::fill(s, 600, 640, 84);
    const OxiResult r = analyze(s);
    QVERIFY(r.hasSpo2);
    QVERIFY(!r.hasPulse);
    QCOMPARE(r.zones.size(), 1);
    QVERIFY(!hasData(r.zones.first().meanPulse));
    QCOMPARE(r.pulseSeconds, 0);
}
