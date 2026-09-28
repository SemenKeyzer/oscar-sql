/* Sleep Analysis Flow Feature Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "flowfeaturetests.h"

#include <cmath>

#include "SleepLib/analysis/flow_analyzer.h"
#include "tests/analysis_synth.h"

using namespace analysis;
using synth::SynthBreath;

namespace {

FlowResult analyze(const FlowChunk &chunk)
{
    return analyzeFlow({ chunk }, {}, nullptr, nullptr, FlowParams());
}

QVector<float> sampled(double (*shape)(double), int n = 50)
{
    QVector<float> v;
    for (int i = 0; i < n; ++i) v.append(float(shape(double(i) / (n - 1))));
    return v;
}

double sec(qint64 t) { return (t - synth::kStart) / 1000.0; }

} // namespace

void FlowFeatureTests::testShapeScores()
{
    const float sine = flowLimitationScore(sampled(synth::sineShape));
    const float flat = flowLimitationScore(sampled(synth::flatShape));
    const float m = flowLimitationScore(sampled(synth::mShape));
    const float chair = flowLimitationScore(sampled(synth::chairShape));
    QVERIFY2(sine < 0.2f, qPrintable(QString::number(sine)));
    QVERIFY2(flat >= 0.5f, qPrintable(QString::number(flat)));
    QVERIFY2(m >= 0.5f, qPrintable(QString::number(m)));
    QVERIFY2(chair >= 0.5f, qPrintable(QString::number(chair)));
    QVERIFY(!hasData(flowLimitationScore({ 1, 2 })));
}

void FlowFeatureTests::testNormalBreathingIsNotLimited()
{
    const FlowResult r = analyze(synth::breathSequence(25, synth::repeat(100, SynthBreath())));
    QVERIFY2(r.flBreaths >= 90, qPrintable(QString::number(r.flBreaths)));
    QVERIFY2(r.flSum / r.flBreaths < 0.2, qPrintable(QString::number(r.flSum / r.flBreaths)));
    QVERIFY(r.flowLimitation.isEmpty());
    QVERIFY(r.reras.isEmpty());
}

void FlowFeatureTests::testFlatBreathingIsLimited()
{
    QVector<SynthBreath> seq = synth::repeat(30, SynthBreath());
    seq += synth::repeat(50, SynthBreath { 4, 1, synth::flatShape });
    seq += synth::repeat(30, SynthBreath());
    const FlowResult r = analyze(synth::breathSequence(25, seq));
    QVERIFY2(r.flowLimitationSeconds() >= 180, qPrintable(QString::number(r.flowLimitationSeconds())));
    QVERIFY(r.flowLimitationSeconds() <= 210);
    QVERIFY(!r.flowLimitation.isEmpty());
    QVERIFY(sec(r.flowLimitation.first().start) >= 115);
}

void FlowFeatureTests::testLowRateHasNoFlowLimitation()
{
    const FlowResult r = analyze(synth::breathSequence(5, synth::repeat(100, SynthBreath { 4, 1, synth::flatShape })));
    QVERIFY(r.analyzed);
    QVERIFY(!r.flScored);
    QCOMPARE(r.flBreaths, 0);
    QVERIFY(r.flowLimitation.isEmpty());
}

void FlowFeatureTests::testReraAfterLimitedShrinkingBreaths()
{
    QVector<SynthBreath> seq = synth::repeat(60, SynthBreath());
    for (double a : { 0.95, 0.9, 0.85, 0.8 }) seq.append(SynthBreath { 4, a, synth::flatShape });
    seq.append(SynthBreath { 4, 1.6, synth::sineShape });
    seq += synth::repeat(40, SynthBreath());
    const FlowResult r = analyze(synth::breathSequence(25, seq));
    QVERIFY2(r.reras.size() == 1, qPrintable(QString::number(r.reras.size())));
    QVERIFY2(qAbs(sec(r.reras.first().start) - 240) <= 1, qPrintable(QString::number(sec(r.reras.first().start))));
    QVERIFY2(qAbs(sec(r.reras.first().end) - 256) <= 1, qPrintable(QString::number(sec(r.reras.first().end))));
    QVERIFY(r.events.isEmpty());
}

void FlowFeatureTests::testNoReraWithoutRecoveryBreath()
{
    QVector<SynthBreath> seq = synth::repeat(60, SynthBreath());
    for (double a : { 0.95, 0.9, 0.85, 0.8 }) seq.append(SynthBreath { 4, a, synth::flatShape });
    seq += synth::repeat(40, SynthBreath());
    QVERIFY(analyze(synth::breathSequence(25, seq)).reras.isEmpty());
}

void FlowFeatureTests::testPeriodicBreathingDetected()
{
    QVector<SynthBreath> seq = synth::repeat(75, SynthBreath());
    for (int k = 0; k < 300; ++k) {
        const double t = 4.0 * k;
        seq.append(SynthBreath { 4, 0.55 + 0.45 * std::sin(2 * 3.14159265358979 * t / 60), synth::sineShape });
    }
    seq += synth::repeat(75, SynthBreath());
    const FlowResult r = analyze(synth::breathSequence(25, seq));
    QVERIFY2(r.periodic.size() == 1, qPrintable(QString::number(r.periodic.size())));
    const Span &pb = r.periodic.first();
    QVERIFY2(qAbs(pb.value - 60) <= 5, qPrintable(QString::number(pb.value)));
    QVERIFY2(sec(pb.start) <= 400 && sec(pb.end) >= 1400, qPrintable(QString("%1..%2").arg(sec(pb.start)).arg(sec(pb.end))));
}

void FlowFeatureTests::testSteadyBreathingIsNotPeriodic()
{
    const FlowResult r = analyze(synth::breathSequence(25, synth::repeat(450, SynthBreath())));
    QVERIFY(r.periodic.isEmpty());
}
