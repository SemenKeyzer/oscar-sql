/* Sleep Analysis Apnea Classifier Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "apneaclassifiertests.h"

#include <cmath>

#include "SleepLib/analysis/apnea_classifier.h"
#include "tests/analysis_synth.h"

using namespace analysis;
using synth::SynthBreath;

namespace {

constexpr double kPi = 3.14159265358979323846;

QVector<float> tone(double fs, double seconds, double hz, double amplitude)
{
    QVector<float> v;
    for (int i = 0; i < int(seconds * fs); ++i) v.append(float(amplitude * std::sin(2 * kPi * hz * i / fs)));
    return v;
}

// 100 normal breaths, a 16 s apnea (breathing at 3 %), then 50 normal breaths:
// the apnea is at 400-416 s.
QVector<SynthBreath> withApnea(const QVector<SynthBreath> &lastBefore = {}, const QVector<SynthBreath> &firstAfter = {})
{
    QVector<SynthBreath> seq = synth::repeat(100 - lastBefore.size(), SynthBreath());
    seq += lastBefore;
    seq += synth::repeat(4, SynthBreath { 4, 0.03, synth::sineShape });
    seq += firstAfter;
    seq += synth::repeat(50, SynthBreath());
    return seq;
}

FlowResult analyze(const FlowChunk &chunk, const Grid *obstruct = nullptr, bool classify = true)
{
    FlowParams p;
    p.classifyApneas = classify;
    return analyzeFlow({ chunk }, {}, nullptr, obstruct, p);
}

const FlowEvent *onlyApnea(const FlowResult &r)
{
    return (r.events.size() == 1 && r.events.first().apnea) ? &r.events.first() : nullptr;
}

} // namespace

void ApneaClassifierTests::testOscillationShareOnPureOscillation()
{
    const float share = cardiogenicOscillationShare(tone(25, 16, 1.2, 0.05), 25, 2.0f, kNoData);
    QVERIFY2(share >= 0.9f, qPrintable(QString::number(share)));
}

void ApneaClassifierTests::testOscillationShareOnRipple()
{
    // the residual breathing of an apnea, too slow and too small to count
    const float share = cardiogenicOscillationShare(tone(25, 16, 0.25, 0.03), 25, 2.0f, kNoData);
    QCOMPARE(share, 0.0f);
}

void ApneaClassifierTests::testOscillationShareNeedsPulseMatch()
{
    const QVector<float> flow = tone(25, 16, 1.2, 0.05);
    QVERIFY(cardiogenicOscillationShare(flow, 25, 2.0f, 72) >= 0.9f);    // 1.2 Hz
    QCOMPARE(cardiogenicOscillationShare(flow, 25, 2.0f, 100), 0.0f);    // 1.67 Hz
}

void ApneaClassifierTests::testSlowRecordingCannotTell()
{
    QCOMPARE(cardiogenicOscillationShare(tone(5, 16, 1.2, 0.05), 5, 2.0f, kNoData), -1.0f);
    QCOMPARE(cardiogenicOscillationShare(tone(25, 6, 1.2, 0.05), 25, 2.0f, kNoData), -1.0f);
}

void ApneaClassifierTests::testClassForScore()
{
    QCOMPARE(classForScore(1.0f), ApneaClass::Obstructive);
    QCOMPARE(classForScore(0.75f), ApneaClass::Obstructive);
    QCOMPARE(classForScore(0.5f), ApneaClass::Unclassified);
    QCOMPARE(classForScore(-0.5f), ApneaClass::Unclassified);
    QCOMPARE(classForScore(-0.75f), ApneaClass::Central);
}

void ApneaClassifierTests::testCentralApneaWithOscillations()
{
    FlowChunk chunk = synth::breathSequence(25, withApnea());
    synth::addOscillation(chunk, 400, 416, 1.2, 0.05);
    const FlowResult r = analyze(chunk);
    const FlowEvent *a = onlyApnea(r);
    QVERIFY(a);
    QCOMPARE(a->cls, ApneaClass::Central);
}

void ApneaClassifierTests::testObstructiveApneaAfterLimitedBreaths()
{
    const FlowResult r = analyze(synth::breathSequence(25, withApnea(
        synth::repeat(3, SynthBreath { 4, 1, synth::flatShape }),
        { SynthBreath { 4, 2.5, synth::sineShape } })));   // >= 2x the last breath before
    const FlowEvent *a = onlyApnea(r);
    QVERIFY(a);
    QVERIFY2(a->cls == ApneaClass::Obstructive, qPrintable(QString::number(a->classScore)));
}

void ApneaClassifierTests::testApneaWithoutEvidenceIsUnclassified()
{
    const FlowResult r = analyze(synth::breathSequence(25, withApnea()));
    const FlowEvent *a = onlyApnea(r);
    QVERIFY(a);
    QCOMPARE(a->cls, ApneaClass::Unclassified);
    QCOMPARE(a->classScore, 0.0f);
}

void ApneaClassifierTests::testObstructLevelDecides()
{
    const FlowChunk chunk = synth::breathSequence(25, withApnea());
    Grid level = synth::flat(700, 80);
    const FlowResult high = analyze(chunk, &level);
    QVERIFY(onlyApnea(high));
    QCOMPARE(onlyApnea(high)->cls, ApneaClass::Obstructive);
    synth::fill(level, 0, 700, 10);
    const FlowResult low = analyze(chunk, &level);
    QVERIFY(onlyApnea(low));
    QCOMPARE(onlyApnea(low)->cls, ApneaClass::Central);
}

void ApneaClassifierTests::testClassificationCanBeSwitchedOff()
{
    FlowChunk chunk = synth::breathSequence(25, withApnea());
    synth::addOscillation(chunk, 400, 416, 1.2, 0.05);
    const FlowResult r = analyze(chunk, nullptr, false);
    const FlowEvent *a = onlyApnea(r);
    QVERIFY(a);
    QCOMPARE(a->cls, ApneaClass::Unclassified);
}
