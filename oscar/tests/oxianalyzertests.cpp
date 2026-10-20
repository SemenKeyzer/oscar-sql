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

// The hypoxic burden counts a desaturation's area only in a 60 s window around its nadir (as
// MediOX and Azarbarzin's search window appear to), so a long, shallow sag does not outweigh
// a short deep dip; a short dip keeps all of its area.
void OxiAnalyzerTests::testDesaturationAreaIsWindowed()
{
    Grid longSag = synth::flat(900, 96);
    synth::dip(longSag, 100, 4, 10, 150, 10);   // 4 points down for about 2.5 min
    const OxiResult r = analyze(longSag);
    QCOMPARE(r.desaturations.size(), 1);
    const Desaturation &d = r.desaturations.first();
    QVERIFY2(secondsOf(d.start, d.end) > 120, qPrintable(QString::number(secondsOf(d.start, d.end))));   // the event stays long
    QVERIFY2(d.area <= 4.0 * kBurdenWindowSec + 1e-6, qPrintable(QString::number(d.area)));
    QVERIFY2(d.area > 4.0 * kBurdenWindowSec / 2, qPrintable(QString::number(d.area)));

    Grid shortDip = synth::flat(600, 96);
    synth::dip(shortDip, 100, 5, 5, 20, 5);
    const OxiResult q = analyze(shortDip);
    QCOMPARE(q.desaturations.size(), 1);
    const Desaturation &e = q.desaturations.first();
    QVERIFY2(secondsOf(e.start, e.end) < kBurdenWindowSec, qPrintable(QString::number(secondsOf(e.start, e.end))));
    QVERIFY(e.area > 5.0 * 20);   // at least the hold at full depth
}

namespace {

Grid livePulse(int seconds) { return synth::livePulse(seconds); }

QVector<OxiArtifact> ofReason(const OxiResult &r, OxiArtifactReason reason)
{
    QVector<OxiArtifact> out;
    for (const OxiArtifact &a : r.artifacts) {
        if (a.reason == reason) out.append(a);
    }
    return out;
}

} // namespace

// A fall of 4 points or more within 2 s that comes back within 30 s is the probe, not the blood.
void OxiAnalyzerTests::testFastFallIsArtifact()
{
    Grid s = synth::flat(900, 96);
    synth::fill(s, 300, 310, 91);                       // 96 -> 91 in 1 s, back after 10 s
    const OxiResult r = analyze(s, livePulse(900));
    const QVector<OxiArtifact> a = ofReason(r, OxiArtifactReason::FastFall);
    QCOMPARE(a.size(), 1);
    QVERIFY(secondsOf(s.start, a[0].start) <= 300 && secondsOf(s.start, a[0].end) >= 310);
    QCOMPARE(r.countDesaturations(3), 0);

    Grid slow = synth::flat(900, 96);
    synth::dip(slow, 300, 5, 15, 10, 15);               // the same depth over 15 s: physiological
    const OxiResult q = analyze(slow, livePulse(900));
    QVERIFY(q.artifacts.isEmpty());
    QCOMPARE(q.countDesaturations(3), 1);
}

// A fall that starts with a pulse jump of 30 bpm or more within 2 s is a moving finger.
void OxiAnalyzerTests::testFallWithPulseJumpIsArtifact()
{
    Grid s = synth::flat(900, 96);
    synth::dip(s, 300, 4, 5, 10, 5);
    Grid p = livePulse(900);
    synth::fill(p, 301, 320, 95);                       // 60 -> 95 within 1 s
    const OxiResult r = analyze(s, p);
    QCOMPARE(ofReason(r, OxiArtifactReason::PulseDisturbed).size(), 1);
    QCOMPARE(r.countDesaturations(3), 0);

    Grid calm = livePulse(900);
    synth::ramp(calm, 300, 308, 60, 75);                // 15 bpm over 8 s: an arousal after an apnea
    synth::fill(calm, 308, 330, 75);
    const OxiResult q = analyze(s, calm);
    QVERIFY(ofReason(q, OxiArtifactReason::PulseDisturbed).isEmpty());
    QCOMPARE(q.countDesaturations(3), 1);
}

// Without any pulse there is no pulse to be disturbed.
void OxiAnalyzerTests::testSpo2WithoutPulseHasNoPulseArtifacts()
{
    Grid s = synth::flat(900, 96);
    synth::dip(s, 300, 4, 5, 10, 5);
    synth::dip(s, 500, 5, 6, 12, 6);
    const OxiResult r = analyze(s);
    QVERIFY(ofReason(r, OxiArtifactReason::PulseDisturbed).isEmpty());
    QCOMPARE(r.countDesaturations(3), 2);
}

// SpO2 and pulse both exactly constant for 3 min or more, while the pulse lives elsewhere: frozen.
void OxiAnalyzerTests::testFrozenSignalIsArtifact()
{
    Grid s = synth::flat(1800, 96);
    for (int i = 0; i < 1800; ++i) s.v[i] = (i / 11) % 2 ? 95 : 96;
    synth::fill(s, 600, 840, 95);
    Grid p = livePulse(1800);
    synth::fill(p, 600, 840, 60);
    const QVector<OxiArtifact> a = ofReason(analyze(s, p), OxiArtifactReason::Frozen);
    QCOMPARE(a.size(), 1);
    QCOMPARE(secondsOf(s.start, a[0].start), 600);
    QCOMPARE(secondsOf(s.start, a[0].end), 840);

    Grid steady = synth::flat(1800, 96);                // a steady SpO2 with a living pulse is normal
    QVERIFY(ofReason(analyze(steady, livePulse(1800)), OxiArtifactReason::Frozen).isEmpty());
}

// Putting the probe on: the unsettled start before the first stable minute is left out.
void OxiAnalyzerTests::testProbeOnOffTrimmed()
{
    Grid s = synth::flat(1800, 96);
    for (int i = 0; i < 90; ++i) s.v[i] = (i % 3) ? 99 : 80 + (i % 5);
    const QVector<OxiArtifact> a = ofReason(analyze(s, livePulse(1800)), OxiArtifactReason::ProbeOnOff);
    QCOMPARE(a.size(), 1);
    QCOMPARE(secondsOf(s.start, a[0].start), 0);
    QVERIFY2(qAbs(secondsOf(s.start, a[0].end) - 90) <= 2, qPrintable(QString::number(secondsOf(s.start, a[0].end))));
}

// A record that never settles loses at most 5 min at each end, never the whole of it.
void OxiAnalyzerTests::testProbeOnOffAtMostFiveMinutes()
{
    Grid s = synth::flat(1200, 96);
    for (int i = 0; i < 1200; ++i) s.v[i] = 92 + qAbs(i % 10 - 5);   // a slow 92-97 saw, 1 point a second
    const QVector<OxiArtifact> a = ofReason(analyze(s, livePulse(1200)), OxiArtifactReason::ProbeOnOff);
    QCOMPARE(a.size(), 2);
    QCOMPARE(secondsOf(a[0].start, a[0].end), 300);
    QCOMPARE(secondsOf(a[1].start, a[1].end), 300);
    QCOMPARE(secondsOf(s.start, a[1].end), 1200);
}

// The cleaning OSCAR always did is now reported too.
void OxiAnalyzerTests::testBasicCleaningIsReported()
{
    Grid s = synth::flat(900, 96);
    synth::fill(s, 300, 302, 40);                       // below the valid range
    synth::fill(s, 500, 502, 84);                       // a 12-point spike back within 3 s
    const OxiResult r = analyze(s, livePulse(900));
    QCOMPARE(ofReason(r, OxiArtifactReason::OutOfRange).size(), 1);
    QCOMPARE(ofReason(r, OxiArtifactReason::Spike).size(), 1);
    QVERIFY(r.artifactSeconds() >= 4);
}

// What an artefact covers is not counted: the recorded time and the histogram leave it out.
void OxiAnalyzerTests::testArtifactsLeaveTheFigures()
{
    Grid s = synth::flat(900, 96);
    synth::fill(s, 300, 310, 91);
    const OxiResult r = analyze(s, livePulse(900));
    QVERIFY(r.artifactSeconds() > 0);
    QCOMPARE(r.spo2Seconds, 900 - r.artifactSeconds());
    QCOMPARE(r.spo2Hist[91 - kSpo2HistMin], 0);
}
