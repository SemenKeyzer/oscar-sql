/* Sleep Analysis Signal Utility Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISSIGNALTESTS_H
#define ANALYSISSIGNALTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for analysis::signal_utils and the analysis parameter hashes.
class AnalysisSignalTests : public QObject
{
    Q_OBJECT
private slots:
    void testOneHzHoldsValuesUntilNextSample();
    void testOneHzGapBetweenListsIsNoData();
    void testOneHzRejectsOutOfRangeAndZero();
    void testOneHzFixedRateList();
    void testMedianFilterDropsSpike();
    void testMovingAverageIgnoresNoData();
    void testPercentileMatchesSortedRank();
    void testTrailingPercentileMatchesNaive();
    void testDecimateMean();
    void testHashesAreStableAndPerStage();
};
DECLARE_TEST(AnalysisSignalTests)

#endif // ANALYSISSIGNALTESTS_H
