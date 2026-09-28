/* Sleep Analysis Flow Analyzer Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef FLOWANALYZERTESTS_H
#define FLOWANALYZERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for analysis::analyzeFlow on synthetic breathing.
class FlowAnalyzerTests : public QObject
{
    Q_OBJECT
private slots:
    void testBreathSegmentation();
    void testApneaDetectedWithinOneSecond();
    void testHalfReductionIsCandidate();
    void testQuarterReductionIsNoEvent();
    void testShortApneaIgnored();
    void testMaskOffIsUnscoreable();
    void testExcludedSpanIsUnscoreable();
    void testDriftingOffsetIgnored();
    void testLowRateStillFindsApneas();
    void testFastRateIsDecimated();
    void testBelowFourHzIsNotAnalyzed();
};
DECLARE_TEST(FlowAnalyzerTests)

#endif // FLOWANALYZERTESTS_H
