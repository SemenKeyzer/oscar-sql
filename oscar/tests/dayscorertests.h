/* Sleep Analysis Day Scorer Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAYSCORERTESTS_H
#define DAYSCORERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for scoring a day: hypopnea rules, desaturation links, comparison, hint.
class DayScorerTests : public QObject
{
    Q_OBJECT
private slots:
    void testAasm3ConfirmsWithDesaturation();
    void testCms4NeedsFourPoints();
    void testFlowOnlyUsesReduction();
    void testCandidateWithoutSpo2();
    void testPulseRiseAsArousal();
    void testLinkWindowEdges();
    void testHypopneaClassification();
    void testLinkedAreaAndUnexplained();
    void testNoLinksWithoutCpap();
    void testPulseResponse();
    void testLimitOxiToCpap();
    void testComparisonWithDevice();
    void testTotalsFromSessions();
    void testOffsetHintFindsLag();
    void testNoOffsetHintWhenAligned();
};
DECLARE_TEST(DayScorerTests)

#endif // DAYSCORERTESTS_H
