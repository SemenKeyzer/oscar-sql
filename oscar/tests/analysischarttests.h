/* Sleep Analysis Overview Chart Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISCHARTTESTS_H
#define ANALYSISCHARTTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the arithmetic behind the Overview's analysis charts.
class AnalysisChartTests : public QObject
{
    Q_OBJECT

private slots:
    void testSpo2RangeSharesAddUpToTheNight();
    void testSpo2RangeLabels();
    void testSpo2RangeColorsDarkenWithDepth();
    void testProblemZoneShares();
    void testOdiTargetLine();
};

DECLARE_TEST(AnalysisChartTests)

#endif // ANALYSISCHARTTESTS_H
