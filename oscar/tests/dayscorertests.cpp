/* Sleep Analysis Day Scorer Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dayscorertests.h"

#include "SleepLib/analysis/day_scorer.h"
#include "tests/analysis_synth.h"

using namespace analysis;

namespace {

constexpr int kNight = 3600;   // seconds

qint64 at(double seconds) { return synth::kStart + qint64(seconds * 1000); }

// One analysed CPAP session over the whole hour, SpO2 96 % and pulse 60 throughout.
DayInput night(bool withOximetry = true)
{
    DayInput in;
    CpapSession s;
    s.span = Span { at(0), at(kNight), 0 };
    s.analyzed = true;
    s.sampleRateHz = 25;
    s.flScored = true;
    s.flowSeconds = kNight;
    in.cpap.append(s);
    if (withOximetry) {
        in.spo2 = synth::flat(kNight, 96);
        in.pulse = synth::flat(kNight, 60);
        in.oxiFromSeparateDevice = true;
    }
    return in;
}

DayEvent dayEvent(double from, double to, RespEvent type, float value = 0)
{
    return DayEvent { at(from), at(to), type, 0, value, 0 };
}

AnalysisParams rule(HypopneaRule r)
{
    AnalysisParams p;
    p.day.rule = r;
    return p;
}

// The nadir of the only desaturation the SpO2 grid holds.
qint64 onlyNadir(const Grid &spo2)
{
    const OxiResult o = analyzeOximetry(spo2, Grid(), OxiParams());
    return o.desaturations.size() == 1 ? o.desaturations[0].nadirTime : 0;
}

} // namespace

void DayScorerTests::testAasm3ConfirmsWithDesaturation()
{
    DayInput in = night();
    in.candidates.append(dayEvent(1000, 1020, RespEvent::Hypopnea, 0.35f));
    synth::dip(in.spo2, 1015, 4, 5, 10, 5);   // nadir about 20 s after the end

    DayResult r = scoreDay(in, rule(HypopneaRule::Aasm3));
    QCOMPARE(r.hypopneas.size(), 1);
    QCOMPARE(r.hypopneasAasm3, 1);
    QCOMPARE(r.hypopneasCms4, 1);
    QCOMPARE(r.hypopneasFlowOnly, 0);   // a 35 % reduction is not enough by flow alone
    QCOMPARE(r.unconfirmable, 0);
    QCOMPARE(r.hypopneas[0].start, at(1000));

    QCOMPARE(scoreDay(in, rule(HypopneaRule::FlowOnly)).hypopneas.size(), 0);
    QCOMPARE(scoreDay(in, rule(HypopneaRule::Auto)).hypopneas.size(), 1);
    QCOMPARE(scoreDay(in, rule(HypopneaRule::Cms4)).hypopneas.size(), 1);
}

void DayScorerTests::testCms4NeedsFourPoints()
{
    DayInput in = night();
    in.candidates.append(dayEvent(1000, 1020, RespEvent::Hypopnea, 0.4f));
    synth::dip(in.spo2, 1010, 3, 3, 20, 3);

    const DayResult r = scoreDay(in, rule(HypopneaRule::Cms4));
    QCOMPARE(r.hypopneas.size(), 0);
    QCOMPARE(r.hypopneasAasm3, 1);
    QCOMPARE(r.hypopneasCms4, 0);
    QCOMPARE(scoreDay(in, rule(HypopneaRule::Aasm3)).hypopneas.size(), 1);
}

void DayScorerTests::testFlowOnlyUsesReduction()
{
    DayInput in = night(false);
    in.candidates.append(dayEvent(1000, 1020, RespEvent::Hypopnea, 0.55f));
    in.candidates.append(dayEvent(2000, 2020, RespEvent::Hypopnea, 0.35f));

    DayResult r = scoreDay(in, rule(HypopneaRule::FlowOnly));
    QCOMPARE(r.hypopneas.size(), 1);
    QCOMPARE(r.hypopneas[0].start, at(1000));
    QCOMPARE(r.unconfirmable, 0);

    // without an oximeter every rule falls back on the flow; the others note it
    r = scoreDay(in, rule(HypopneaRule::Auto));
    QCOMPARE(r.hypopneas.size(), 1);
    QCOMPARE(r.unconfirmable, 2);
    QCOMPARE(r.hypopneasAasm3, 1);
    QCOMPARE(r.hypopneasCms4, 1);
    QCOMPARE(r.hypopneasFlowOnly, 1);
    QVERIFY(!r.hasOximetry);
}

void DayScorerTests::testCandidateWithoutSpo2()
{
    DayInput in = night();
    synth::fill(in.spo2, 1800, kNight, kNoData);   // the oximeter came off half way
    in.candidates.append(dayEvent(1000, 1020, RespEvent::Hypopnea, 0.6f));   // covered, no desaturation
    in.candidates.append(dayEvent(2500, 2520, RespEvent::Hypopnea, 0.6f));   // not covered

    const DayResult r = scoreDay(in, rule(HypopneaRule::Aasm3));
    QCOMPARE(r.hypopneas.size(), 1);
    QCOMPARE(r.hypopneas[0].start, at(2500));   // by flow only
    QCOMPARE(r.unconfirmable, 1);
    QCOMPARE(r.hypopneasFlowOnly, 2);
}

void DayScorerTests::testPulseRiseAsArousal()
{
    DayInput in = night();
    in.candidates.append(dayEvent(1000, 1020, RespEvent::Hypopnea, 0.35f));
    synth::ramp(in.pulse, 1018, 1023, 60, 72);   // a rise of 12 bpm at the end ...
    synth::fill(in.pulse, 1023, 1033, 72);       // ... held for 10 s
    synth::ramp(in.pulse, 1033, 1038, 72, 60);

    QCOMPARE(scoreDay(in, rule(HypopneaRule::Aasm3)).hypopneas.size(), 0);
    AnalysisParams p = rule(HypopneaRule::Aasm3);
    p.day.pulseRiseAsArousal = true;
    const DayResult r = scoreDay(in, p);
    QCOMPARE(r.oxi.pulseRises.size(), 1);
    QCOMPARE(r.hypopneas.size(), 1);
}

void DayScorerTests::testLinkWindowEdges()
{
    DayInput in = night();
    synth::dip(in.spo2, 1500, 4, 5, 10, 5);
    const qint64 nadir = onlyNadir(in.spo2);
    QVERIFY(nadir > 0);

    // the nadir 29 s after the end: linked; 31 s after: not
    const double nadirSec = (nadir - synth::kStart) / 1000.0;
    DayInput inside = in, outside = in;
    inside.candidates.append(dayEvent(nadirSec - 49, nadirSec - 29, RespEvent::Hypopnea, 0.35f));
    outside.candidates.append(dayEvent(nadirSec - 51, nadirSec - 31, RespEvent::Hypopnea, 0.35f));
    QCOMPARE(scoreDay(inside, rule(HypopneaRule::Aasm3)).hypopneas.size(), 1);
    QCOMPARE(scoreDay(outside, rule(HypopneaRule::Aasm3)).hypopneas.size(), 0);

    AnalysisParams wider = rule(HypopneaRule::Aasm3);
    wider.day.linkWindowSec = 40;
    QCOMPARE(scoreDay(outside, wider).hypopneas.size(), 1);
}

void DayScorerTests::testHypopneaClassification()
{
    DayInput in = night(false);
    in.candidates = { dayEvent(500, 520, RespEvent::Hypopnea, 0.6f), dayEvent(1000, 1020, RespEvent::Hypopnea, 0.6f),
                      dayEvent(1500, 1520, RespEvent::Hypopnea, 0.6f), dayEvent(2000, 2020, RespEvent::Hypopnea, 0.6f) };
    in.periodic.append(Span { at(900), at(1600), 60 });
    for (int k = 0; k < 5; ++k) {
        in.flScores.append(TimedValue { at(502 + 4 * k), 0.8f });    // limited breaths
        in.flScores.append(TimedValue { at(1002 + 4 * k), 0.1f });   // normal breaths, periodic breathing
        in.flScores.append(TimedValue { at(2002 + 4 * k), 0.1f });   // normal breaths, no periodic breathing
    }
    // the event at 1500 has no scored breaths

    const DayResult r = scoreDay(in, rule(HypopneaRule::FlowOnly));
    QCOMPARE(r.hypopneas.size(), 4);
    QCOMPARE(r.hypopneas[0].type, RespEvent::ObstructiveHypopnea);
    QCOMPARE(r.hypopneas[1].type, RespEvent::CentralHypopnea);
    QCOMPARE(r.hypopneas[2].type, RespEvent::Hypopnea);
    QCOMPARE(r.hypopneas[3].type, RespEvent::Hypopnea);
    QCOMPARE(r.count(RespEvent::ObstructiveHypopnea), 1);
    QCOMPARE(r.count(RespEvent::CentralHypopnea), 1);
    QCOMPARE(r.count(RespEvent::Hypopnea), 2);
}

void DayScorerTests::testLinkedAreaAndUnexplained()
{
    DayInput in = night();
    in.apneas.append(dayEvent(1000, 1020, RespEvent::ObstructiveApnea));
    synth::dip(in.spo2, 1015, 5, 5, 10, 5);   // after the apnea
    synth::dip(in.spo2, 2500, 4, 5, 10, 5);   // unexplained

    const DayResult r = scoreDay(in, AnalysisParams());
    QCOMPARE(r.oxi.desaturations.size(), 2);
    QCOMPARE(r.linkedDesaturations, 1);
    QCOMPARE(r.linkedDesatArea, r.oxi.desaturations[0].area);
    QVERIFY(r.linkedDesatArea > 0);
    QCOMPARE(r.unexplained, QVector<int>({ 1 }));

    // a desaturation during unscoreable time is not "unexplained"
    in.unscoreable.append(Span { at(2400), at(2700), 0 });
    QVERIFY(scoreDay(in, AnalysisParams()).unexplained.isEmpty());

    // the device's events link separately
    in.deviceEvents.append(dayEvent(2480, 2500, RespEvent::Hypopnea));
    QCOMPARE(scoreDay(in, AnalysisParams()).deviceLinkedDesaturations, 1);
}

void DayScorerTests::testNoLinksWithoutCpap()
{
    DayInput in;
    in.spo2 = synth::flat(kNight, 96);
    synth::dip(in.spo2, 1500, 5, 5, 10, 5);

    const DayResult r = scoreDay(in, AnalysisParams());
    QVERIFY(!r.hasCpap);
    QVERIFY(!r.hasFlow);
    QVERIFY(r.hasOximetry);
    QCOMPARE(r.oxi.desaturations.size(), 1);
    QCOMPARE(r.linkedDesaturations, 0);
    QVERIFY(r.unexplained.isEmpty());
    QVERIFY(!r.hasComparison);
    QVERIFY(!r.hasOffsetHint);
}

void DayScorerTests::testPulseResponse()
{
    DayInput in = night();
    in.apneas.append(dayEvent(1000, 1020, RespEvent::ObstructiveApnea));
    synth::ramp(in.pulse, 1020, 1025, 60, 75);
    synth::fill(in.pulse, 1025, 1035, 75);
    synth::ramp(in.pulse, 1035, 1040, 75, 60);

    const DayResult r = scoreDay(in, AnalysisParams());
    QVERIFY(r.hasPulse);
    QCOMPARE(r.dhrEvents, 1);
    QVERIFY(qAbs(r.dhrSum - 15) <= 1);
    QCOMPARE(r.eventsWithPulseRise, 1);

    // no pulse before the event: no ΔHR
    synth::fill(in.pulse, 980, 1000, kNoData);
    QCOMPARE(scoreDay(in, AnalysisParams()).dhrEvents, 0);
}

void DayScorerTests::testLimitOxiToCpap()
{
    DayInput in = night();
    in.cpap[0].span = Span { at(1000), at(2000), 0 };

    DayResult r = scoreDay(in, AnalysisParams());
    QCOMPARE(r.oxiScope, QStringLiteral("night"));
    QCOMPARE(r.oxi.spo2Seconds, kNight);

    AnalysisParams p;
    p.day.limitOxiToCpap = true;
    r = scoreDay(in, p);
    QCOMPARE(r.oxiScope, QStringLiteral("cpap"));
    QCOMPARE(r.oxi.spo2Seconds, 1000);
    QCOMPARE(r.oxi.pulseSeconds, 1000);

    // without CPAP that night the option changes nothing
    in.cpap.clear();
    r = scoreDay(in, p);
    QCOMPARE(r.oxiScope, QStringLiteral("night"));
    QCOMPARE(r.oxi.spo2Seconds, kNight);
}

void DayScorerTests::testComparisonWithDevice()
{
    DayInput in = night(false);
    in.apneas.append(dayEvent(1001, 1016, RespEvent::ObstructiveApnea));
    in.candidates.append(dayEvent(2000, 2020, RespEvent::Hypopnea, 0.6f));
    in.reras.append(Span { at(3000), at(3030), 0 });

    DayEvent oa = dayEvent(1000, 1015, RespEvent::ObstructiveApnea);
    oa.stamp = at(1015);
    in.deviceEvents = { oa, dayEvent(1500, 1512, RespEvent::Hypopnea), dayEvent(2002, 2020, RespEvent::CentralApnea),
                        dayEvent(3300, 3312, RespEvent::Hypopnea) };
    in.unscoreable.append(Span { at(3200), at(3400), 0 });   // the last device event falls here

    const DayResult r = scoreDay(in, rule(HypopneaRule::FlowOnly));
    QVERIFY(r.hasComparison);
    QCOMPARE(r.deviceEvents.size(), 3);
    QCOMPARE(r.analysisEvents.size(), 3);
    QCOMPARE(r.match.matched.size(), 2);        // the apneas, and the hypopnea against the CA
    QCOMPARE(r.match.typeMismatch, 1);
    QCOMPARE(r.match.deviceOnly.size(), 1);     // the hypopnea at 1500
    QCOMPARE(r.match.analysisOnly.size(), 1);   // the RERA
    QCOMPARE(r.deviceCount(EventGroup::Apnea), 2);
    QCOMPARE(r.deviceCount(EventGroup::Hypopnea), 1);
    QCOMPARE(r.deviceCount(EventGroup::Rera), 0);

    QVERIFY(r.hasApneaOffset);
    QCOMPARE(r.apneaOffsetToEndMs, qint64(-1000));
    QCOMPARE(r.apneaOffsetToStartMs, qint64(14000));
}

void DayScorerTests::testTotalsFromSessions()
{
    DayInput in = night(false);
    CpapSession second;
    second.span = Span { at(kNight + 600), at(2 * kNight), 0 };
    second.analyzed = true;
    second.sampleRateHz = 5;
    second.flowSeconds = 2000;
    second.unscoreableSeconds = 1000;
    in.cpap[0].unscoreableSeconds = 100;
    in.cpap[0].flSum = 30;
    in.cpap[0].flBreaths = 600;
    in.cpap.append(second);
    CpapSession summaryOnly;
    summaryOnly.span = Span { at(3 * kNight), at(4 * kNight), 0 };
    in.cpap.append(summaryOnly);
    in.flowLimitation = { Span { at(100), at(160), 0 }, Span { at(400), at(430), 0 } };
    in.periodic = { Span { at(1000), at(1600), 60 } };

    const DayResult r = scoreDay(in, AnalysisParams());
    QVERIFY(r.hasFlow);
    QCOMPARE(r.flowSeconds, kNight + 2000);
    QCOMPARE(r.unscoreableSeconds, 1100);
    QCOMPARE(r.flowRateHz, 5.0);
    QVERIFY(!r.flScored);
    QCOMPARE(r.flSum, 30.0);
    QCOMPARE(r.flBreaths, 600);
    QCOMPARE(r.flSeconds, 90);
    QCOMPARE(r.pbSeconds, 600);

    DayInput none;
    none.cpap.append(summaryOnly);
    const DayResult n = scoreDay(none, AnalysisParams());
    QVERIFY(n.hasCpap);
    QVERIFY(!n.hasFlow);
    QVERIFY(!n.hasComparison);
}

void DayScorerTests::testOffsetHintFindsLag()
{
    // The oximeter's clock is 2 minutes behind: each nadir shows up 100 s before the end
    // of the apnea that caused it, instead of about 20 s after.
    DayInput in = night();
    const QVector<int> dips { 300, 420, 610, 700, 890, 1100, 1180, 1400, 1520, 1750, 1900, 2150 };
    for (int s : dips) synth::dip(in.spo2, s, 4, 5, 10, 5);
    const OxiResult oxi = analyzeOximetry(in.spo2, Grid(), OxiParams());
    QCOMPARE(oxi.desaturations.size(), dips.size());
    for (const Desaturation &d : oxi.desaturations) {
        const double end = (d.nadirTime - synth::kStart) / 1000.0 + 100;
        in.apneas.append(dayEvent(end - 15, end, RespEvent::ObstructiveApnea));
    }

    const DayResult r = scoreDay(in, AnalysisParams());
    QVERIFY(r.hasOffsetHint);
    QCOMPARE(r.offsetHintMs, qint64(120000));

    // SpO2 from the CPAP itself: one clock, no hint
    in.oxiFromSeparateDevice = false;
    QVERIFY(!scoreDay(in, AnalysisParams()).hasOffsetHint);
}

void DayScorerTests::testNoOffsetHintWhenAligned()
{
    DayInput in = night();
    const QVector<int> dips { 300, 420, 610, 700, 890, 1100, 1180, 1400, 1520, 1750, 1900, 2150 };
    for (int s : dips) synth::dip(in.spo2, s, 4, 5, 10, 5);
    const OxiResult oxi = analyzeOximetry(in.spo2, Grid(), OxiParams());
    for (const Desaturation &d : oxi.desaturations) {
        const double end = (d.nadirTime - synth::kStart) / 1000.0 - 20;   // nadir 20 s after the end
        in.apneas.append(dayEvent(end - 15, end, RespEvent::ObstructiveApnea));
    }
    QVERIFY(!scoreDay(in, AnalysisParams()).hasOffsetHint);

    // too few events to tell
    in.apneas.resize(5);
    QVERIFY(!scoreDay(in, AnalysisParams()).hasOffsetHint);
}

void DayScorerTests::testLongestFlRun()
{
    DayInput in = night(false);
    in.flowLimitation = { Span { at(100), at(130), 0 }, Span { at(400), at(490), 0 } };
    const DayResult r = scoreDay(in, AnalysisParams());
    QCOMPARE(r.flSeconds, 120);
    QCOMPARE(r.flLongestSeconds, 90);
}

// A session recorded below 10 Hz has no Glasgow counts; the night's come from the other one.
void DayScorerTests::testMixedRateNight()
{
    DayInput in = night(false);
    in.cpap[0].flLimitedBreaths = 20;
    in.cpap[0].glasgow.breaths = 100;
    in.cpap[0].glasgow.flagged[GiFlatTop] = 10;
    in.cpap[0].glasgowAdapted.breaths = 90;
    CpapSession slow;
    slow.span = Span { at(kNight + 600), at(2 * kNight), 0 };
    slow.analyzed = true;
    slow.sampleRateHz = 5;
    slow.flowSeconds = 2000;
    in.cpap.append(slow);
    const DayResult r = scoreDay(in, AnalysisParams());
    QCOMPARE(r.glasgow.breaths, 100);
    QCOMPARE(r.glasgow.flagged[GiFlatTop], 10);
    QCOMPARE(r.glasgowAdapted.breaths, 90);
    QCOMPARE(r.flLimitedBreaths, 20);
}
