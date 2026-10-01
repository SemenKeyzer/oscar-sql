/* Sleep Analysis Overview Chart Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysischarttests.h"
#include "Graphs/gAnalysisCharts.h"
#include "SleepLib/analysis/oxi_analyzer.h"

// Nights differ in length, so each range is a share of the night; the shares make up the bar.
void AnalysisChartTests::testSpo2RangeSharesAddUpToTheNight()
{
    QVector<int> hist(analysis::kSpo2HistMax - analysis::kSpo2HistMin + 1, 0);
    hist[95 - analysis::kSpo2HistMin] = 3000;
    hist[92 - analysis::kSpo2HistMin] = 500;
    hist[87 - analysis::kSpo2HistMin] = 100;
    const QVector<gAnalysisChart::RangeShare> shares =
        gAnalysisChart::spo2RangeShares(hist, 3600, { 94, 90, 88, 85, 80 });
    QCOMPARE(shares.size(), 6);                                   // lowest range first
    QCOMPARE(shares.first().name, QStringLiteral("< 80 %"));
    QCOMPARE(shares.last().name, QStringLiteral("\u2265 94 %"));
    QCOMPARE(shares.last().seconds, 3000);
    QCOMPARE(shares.last().colorIndex, 0);
    QCOMPARE(shares[4].name, QStringLiteral("90\u201393 %"));
    QCOMPARE(shares[4].seconds, 500);
    QCOMPARE(shares[2].name, QStringLiteral("85\u201387 %"));
    QCOMPARE(shares[2].seconds, 100);
    double total = 0;
    for (const auto &s : shares) total += s.percent;
    QCOMPARE(qRound(total * 10), 1000);
    QCOMPARE(qRound(shares.last().percent * 100), 8333);
}

// Readings are whole %, so a range is named by the readings it holds, the same way in the
// Overview and the Daily view.
void AnalysisChartTests::testSpo2RangeLabels()
{
    QCOMPARE(gAnalysisChart::spo2RangeLabel(90, 94), QStringLiteral("90\u201393 %"));
    QCOMPARE(gAnalysisChart::spo2RangeLabel(88, 89), QStringLiteral("88 %"));        // a single reading
    QCOMPARE(gAnalysisChart::spo2RangeLabel(88.5, 90), QStringLiteral("89 %"));      // 89 is the only one inside
    QCOMPARE(gAnalysisChart::spo2RangeLabel(85.5, 88.5), QStringLiteral("86\u201388 %"));
    QCOMPARE(gAnalysisChart::spo2RangeLabel(90.2, 90.5), QStringLiteral("90.2\u2013<90.5 %"));   // none inside
    QCOMPARE(gAnalysisChart::spo2RangeLabel(-1, 85), QStringLiteral("< 85 %"));
    QCOMPARE(gAnalysisChart::spo2RangeLabel(94, 101), QStringLiteral("\u2265 94 %"));
}

// The ranges below the highest threshold go from light to dark as they go lower, whatever
// their number, so the deepest is always the darkest.
void AnalysisChartTests::testSpo2RangeColorsDarkenWithDepth()
{
    for (int count = 2; count <= 7; ++count) {
        int lastLightness = 256;
        for (int i = 1; i < count; ++i) {
            const int l = gAnalysisChart::rangeColor(i, count).lightness();
            QVERIFY2(l < lastLightness, qPrintable(QStringLiteral("count %1 range %2").arg(count).arg(i)));
            lastLightness = l;
        }
        QCOMPARE(gAnalysisChart::rangeColor(count - 1, count), QColor(0xb1, 0x00, 0x26));
    }
}

void AnalysisChartTests::testProblemZoneShares()
{
    const QPair<double, double> p = gAnalysisChart::problemZoneShares(600, 120, 3600);
    QCOMPARE(qRound(p.first * 100), 1667);      // all zones: 600 of 3600 s
    QCOMPARE(qRound(p.second * 100), 333);      // marked: 120 of 3600 s
    QCOMPARE(gAnalysisChart::problemZoneShares(600, 120, 0).first, 0.0);
}
