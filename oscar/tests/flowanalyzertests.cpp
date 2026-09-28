/* Sleep Analysis Flow Analyzer Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "flowanalyzertests.h"

#include "SleepLib/analysis/flow_analyzer.h"
#include "tests/analysis_synth.h"

using namespace analysis;

namespace {

using Segments = QVector<QPair<double, double>>;

FlowResult analyze(const FlowChunk &chunk, const QVector<Span> &excluded = {})
{
    return analyzeFlow({ chunk }, excluded, nullptr, nullptr, FlowParams());
}

double sec(qint64 t) { return (t - synth::kStart) / 1000.0; }

QString describe(const FlowResult &r)
{
    QStringList parts;
    for (const FlowEvent &e : r.events) {
        parts << QString("%1[%2..%3 red %4]").arg(e.apnea ? "A" : "H").arg(sec(e.start)).arg(sec(e.end)).arg(e.reduction);
    }
    return parts.join(' ');
}

} // namespace

void FlowAnalyzerTests::testBreathSegmentation()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 600, 1.0 } }));
    QVERIFY(r.analyzed);
    QVERIFY(r.flScored);
    QVERIFY2(qAbs(r.breaths.size() - 150) <= 3, qPrintable(QString::number(r.breaths.size())));
    const Breath &b = r.breaths[r.breaths.size() / 2];
    QVERIFY2(qAbs(b.amplitude - 2.0f) < 0.2f, qPrintable(QString::number(b.amplitude)));
    QVERIFY(qAbs((b.end - b.start) - 4000) <= 100);
    QVERIFY(qAbs((b.inspEnd - b.start) - 2000) <= 100);
    QVERIFY(b.vi > 0);
    QVERIFY(r.events.isEmpty());
}

void FlowAnalyzerTests::testApneaDetectedWithinOneSecond()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 16, 0.03 }, { 200, 1.0 } }));
    QVERIFY2(r.events.size() == 1, qPrintable(describe(r)));
    const FlowEvent &e = r.events.first();
    QVERIFY(e.apnea);
    QVERIFY2(qAbs(sec(e.start) - 400) <= 1 && qAbs(sec(e.end) - 416) <= 1, qPrintable(describe(r)));
    QVERIFY(e.reduction >= 0.9f);
}

void FlowAnalyzerTests::testHalfReductionIsCandidate()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 20, 0.5 }, { 200, 1.0 } }));
    QVERIFY2(r.events.size() == 1, qPrintable(describe(r)));
    const FlowEvent &e = r.events.first();
    QVERIFY(!e.apnea);
    QVERIFY2(qAbs(e.reduction - 0.5f) <= 0.1f, qPrintable(describe(r)));
    QVERIFY2(qAbs(sec(e.start) - 400) <= 1 && qAbs(sec(e.end) - 420) <= 1, qPrintable(describe(r)));
}

void FlowAnalyzerTests::testQuarterReductionIsNoEvent()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 20, 0.75 }, { 200, 1.0 } }));
    QVERIFY2(r.events.isEmpty(), qPrintable(describe(r)));
}

void FlowAnalyzerTests::testShortApneaIgnored()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 8, 0.03 }, { 200, 1.0 } }));
    QVERIFY2(r.events.isEmpty(), qPrintable(describe(r)));
}

void FlowAnalyzerTests::testMaskOffIsUnscoreable()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 300, 0.0 }, { 200, 1.0 } }));
    QVERIFY2(r.events.isEmpty(), qPrintable(describe(r)));
    qint64 covered = 0;
    for (const Span &s : r.unscoreable) {
        covered += qMax<qint64>(0, qMin(s.end, synth::kStart + 700000) - qMax(s.start, synth::kStart + 400000));
    }
    QVERIFY2(covered >= 280000, qPrintable(QString::number(covered)));
    QVERIFY(r.flowSeconds <= 900 - 280);
}

void FlowAnalyzerTests::testExcludedSpanIsUnscoreable()
{
    const Span leak { synth::kStart + 395000, synth::kStart + 425000, 0 };
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 16, 0.03 }, { 200, 1.0 } }), { leak });
    QVERIFY2(r.events.isEmpty(), qPrintable(describe(r)));
    QVERIFY(r.unscoreableSeconds >= 30);
}

void FlowAnalyzerTests::testDriftingOffsetIgnored()
{
    const FlowResult r = analyze(synth::breathing(25, 4, { { 400, 1.0 }, { 16, 0.03 }, { 200, 1.0 } }, 0.3));
    QVERIFY2(r.events.size() == 1 && r.events.first().apnea, qPrintable(describe(r)));
    QVERIFY2(qAbs(r.breaths.size() - 150) <= 6, qPrintable(QString::number(r.breaths.size())));
}

void FlowAnalyzerTests::testLowRateStillFindsApneas()
{
    const FlowResult r = analyze(synth::breathing(5, 4, { { 400, 1.0 }, { 16, 0.03 }, { 200, 1.0 } }));
    QVERIFY(r.analyzed);
    QVERIFY(!r.flScored);
    QVERIFY2(r.events.size() == 1 && r.events.first().apnea, qPrintable(describe(r)));
    QVERIFY2(qAbs(sec(r.events.first().start) - 400) <= 1, qPrintable(describe(r)));
}

void FlowAnalyzerTests::testFastRateIsDecimated()
{
    const FlowResult r = analyze(synth::breathing(100, 4, { { 400, 1.0 }, { 16, 0.03 }, { 200, 1.0 } }));
    QCOMPARE(r.sampleRateHz, 100.0);
    QVERIFY2(r.events.size() == 1 && r.events.first().apnea, qPrintable(describe(r)));
}

void FlowAnalyzerTests::testBelowFourHzIsNotAnalyzed()
{
    const FlowResult r = analyze(synth::breathing(2, 4, { { 600, 1.0 } }));
    QVERIFY(!r.analyzed);
    QCOMPARE(r.sampleRateHz, 2.0);
    QVERIFY(r.breaths.isEmpty());
}
