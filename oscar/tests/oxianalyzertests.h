/* Sleep Analysis Oximetry Analyzer Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIANALYZERTESTS_H
#define OXIANALYZERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for analysis::analyzeOximetry on synthetic SpO2 and pulse.
class OxiAnalyzerTests : public QObject
{
    Q_OBJECT
private slots:
    void testSmallDropIsNotDesaturation();
    void testThreePercentDropCountsForOdi3Only();
    void testFivePercentDropCountsForBoth();
    void testSeriesOfDropsIsCyclic();
    void testSlowDriftIsNotEventButCountsBelow90();
    void testSingleSpikeIsIgnored();
    void testShortSegmentsAreDropped();
    void testTimeBelowThresholdsIsExact();
    void testPulseRiseDetected();
    void testSmallPulseRiseIgnored();
    void testBradycardiaSpan();
    void testDesaturationSeriesMakesOneZone();
    void testNinetySecondsBelow90MakesModerateZone();
    void testFortySecondsBelow85MakesMarkedZone();
    void testCloseZonesMerge();
    void testSingleDesaturationMakesNoZone();
    void testOximetryWithoutPulse();
};
DECLARE_TEST(OxiAnalyzerTests)

#endif // OXIANALYZERTESTS_H
