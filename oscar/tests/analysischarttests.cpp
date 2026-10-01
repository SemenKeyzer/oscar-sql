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
    QCOMPARE(shares.first().name, QStringLiteral("< 80%"));
    QCOMPARE(shares.last().name, QStringLiteral(">= 94%"));
    QCOMPARE(shares.last().seconds, 3000);
    QCOMPARE(shares[4].name, QStringLiteral("90-94%"));
    QCOMPARE(shares[4].seconds, 500);
    QCOMPARE(shares[2].name, QStringLiteral("85-88%"));
    QCOMPARE(shares[2].seconds, 100);
    double total = 0;
    for (const auto &s : shares) total += s.percent;
    QCOMPARE(qRound(total * 10), 1000);
    QCOMPARE(qRound(shares.last().percent * 100), 8333);
}

void AnalysisChartTests::testProblemZoneShares()
{
    const QPair<double, double> p = gAnalysisChart::problemZoneShares(600, 120, 3600);
    QCOMPARE(qRound(p.first * 100), 1667);      // all zones: 600 of 3600 s
    QCOMPARE(qRound(p.second * 100), 333);      // marked: 120 of 3600 s
    QCOMPARE(gAnalysisChart::problemZoneShares(600, 120, 0).first, 0.0);
}
