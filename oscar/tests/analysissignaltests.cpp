/* Sleep Analysis Signal Utility Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysissignaltests.h"

#include <QRandomGenerator>
#include <algorithm>

#include "SleepLib/analysis/analysis_params.h"
#include "SleepLib/analysis/signal_utils.h"

using namespace analysis;

static TimedSamples samples(std::initializer_list<qint64> t, std::initializer_list<float> v, qint64 end)
{
    TimedSamples s;
    s.t = QVector<qint64>(t);
    s.v = QVector<float>(v);
    s.end = end;
    return s;
}

void AnalysisSignalTests::testOneHzHoldsValuesUntilNextSample()
{
    // "Store on change": 96 from 0 s, 94 from 3.5 s, list ends at 6 s.
    const Grid g = toOneHz({ samples({ 0, 3500 }, { 96, 94 }, 6000) }, 50, 100);
    QCOMPARE(g.start, qint64(0));
    QCOMPARE(g.size(), 6);
    QCOMPARE(g.v[0], 96.0f);
    QCOMPARE(g.v[2], 96.0f);
    QCOMPARE(g.v[3], 94.0f);   // cell centre 3.5 s: the new value
    QCOMPARE(g.v[5], 94.0f);
    QCOMPARE(g.at(5999), 94.0f);
    QVERIFY(!hasData(g.at(6000)));
}

void AnalysisSignalTests::testOneHzGapBetweenListsIsNoData()
{
    const Grid g = toOneHz({ samples({ 0 }, { 95 }, 3000), samples({ 10000 }, { 93 }, 12000) }, 50, 100);
    QCOMPARE(g.size(), 12);
    QCOMPARE(g.v[2], 95.0f);
    for (int i = 3; i < 10; ++i) QVERIFY2(!hasData(g.v[i]), qPrintable(QString::number(i)));
    QCOMPARE(g.v[10], 93.0f);
    QCOMPARE(g.v[11], 93.0f);
}

void AnalysisSignalTests::testOneHzRejectsOutOfRangeAndZero()
{
    // 0 is the loaders' gap marker; 40 is below the valid SpO2 range.
    const Grid g = toOneHz({ samples({ 0, 2000, 4000, 6000 }, { 97, 0, 40, 96 }, 8000) }, 50, 100);
    QCOMPARE(g.v[1], 97.0f);
    QVERIFY(!hasData(g.v[2]));
    QVERIFY(!hasData(g.v[5]));
    QCOMPARE(g.v[6], 96.0f);
}

void AnalysisSignalTests::testOneHzFixedRateList()
{
    // A 4 s waveform (Viatom): each sample covers four cells.
    const Grid g = toOneHz({ samples({ 1000, 5000, 9000 }, { 90, 91, 92 }, 13000) }, 50, 100);
    QCOMPARE(g.start, qint64(1000));
    QCOMPARE(g.size(), 12);
    QCOMPARE(g.v[0], 90.0f);
    QCOMPARE(g.v[3], 90.0f);
    QCOMPARE(g.v[4], 91.0f);
    QCOMPARE(g.v[11], 92.0f);
}

void AnalysisSignalTests::testMedianFilterDropsSpike()
{
    const QVector<float> in { 96, 96, 70, 96, 96, kNoData, 95 };
    const QVector<float> out = medianFilter(in, 5);
    QCOMPARE(out[2], 96.0f);
    QVERIFY(!hasData(out[5]));
    QCOMPARE(out[6], 96.0f);   // window {96, NaN, 95}: upper median of the valid ones
}

void AnalysisSignalTests::testMovingAverageIgnoresNoData()
{
    const QVector<float> in { 1, 2, kNoData, 4, 5 };
    const QVector<float> out = movingAverage(in, 3);
    QCOMPARE(out[0], 1.5f);
    QCOMPARE(out[1], 1.5f);   // (1 + 2) / 2: the NaN neighbour is skipped
    QVERIFY(!hasData(out[2]));
    QCOMPARE(out[3], 4.5f);
    QCOMPARE(out[4], 4.5f);
}

void AnalysisSignalTests::testPercentileMatchesSortedRank()
{
    QRandomGenerator rng(1234);
    QVector<float> v;
    for (int i = 0; i < 501; ++i) v.append(float(rng.bounded(1000)));
    v.append(kNoData);
    QVector<float> sorted = v;
    sorted.removeLast();
    std::sort(sorted.begin(), sorted.end());
    for (double p : { 0.0, 10.0, 50.0, 70.0, 90.0, 100.0 }) {
        const int idx = int(std::lround(p / 100.0 * (sorted.size() - 1)));
        QCOMPARE(percentile(v, p), sorted[idx]);
    }
    QVERIFY(!hasData(percentile({ kNoData }, 50)));
}

void AnalysisSignalTests::testTrailingPercentileMatchesNaive()
{
    QRandomGenerator rng(99);
    QVector<float> v;
    for (int i = 0; i < 300; ++i) v.append(rng.bounded(10) == 0 ? kNoData : float(rng.bounded(100)));
    const QVector<float> out = trailingPercentile(v, 40, 70, 10);
    for (int i = 0; i < v.size(); ++i) {
        QVector<float> window;
        for (int j = qMax(0, i - 40); j < i; ++j) if (hasData(v[j])) window.append(v[j]);
        if (window.size() < 10) {
            QVERIFY2(!hasData(out[i]), qPrintable(QString::number(i)));
        } else {
            QCOMPARE(out[i], percentile(window, 70));
        }
    }
}

void AnalysisSignalTests::testDecimateMean()
{
    const QVector<float> out = decimateMean({ 1, 3, 5, 7, 9 }, 2);
    QCOMPARE(out, QVector<float>({ 2, 6, 9 }));
    QCOMPARE(decimateMean({ 1, 2 }, 1), QVector<float>({ 1, 2 }));
}

void AnalysisSignalTests::testHashesAreStableAndPerStage()
{
    const AnalysisParams base;
    QCOMPARE(base.oxiHash(), AnalysisParams().oxiHash());
    QCOMPARE(base.oxiHash().size(), 16);
    QVERIFY(base.oxiHash() != base.flowHash());

    AnalysisParams oxi = base;
    oxi.oxi.desatMinDrop = 4;
    QVERIFY(oxi.oxiHash() != base.oxiHash());
    QCOMPARE(oxi.flowHash(), base.flowHash());
    QCOMPARE(oxi.dayHash(), base.dayHash());

    AnalysisParams flow = base;
    flow.flow.classifyApneas = false;
    QVERIFY(flow.flowHash() != base.flowHash());
    QCOMPARE(flow.oxiHash(), base.oxiHash());

    AnalysisParams day = base;
    day.day.rule = HypopneaRule::Cms4;
    QVERIFY(day.dayHash() != base.dayHash());
    QCOMPARE(day.flowHash(), base.flowHash());

    AnalysisParams off = base;
    off.enabled = false;   // switching the analysis off is not a parameter change
    QCOMPARE(off.oxiHash(), base.oxiHash());
}
