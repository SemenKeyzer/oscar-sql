/* Sleep Analysis Flow Feature Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef FLOWFEATURETESTS_H
#define FLOWFEATURETESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for flow limitation, flow-based RERA and periodic breathing detection.
class FlowFeatureTests : public QObject
{
    Q_OBJECT
private slots:
    void testShapeScores();
    void testNormalBreathingIsNotLimited();
    void testFlatBreathingIsLimited();
    void testLowRateHasNoFlowLimitation();
    void testReraAfterLimitedShrinkingBreaths();
    void testNoReraWithoutRecoveryBreath();
    void testPeriodicBreathingDetected();
    void testSteadyBreathingIsNotPeriodic();
    void testLimitedBreathCount();
    void testGlasgowNeedsTenHz();
    void testGlasgowCoarseRecording();
};
DECLARE_TEST(FlowFeatureTests)

#endif // FLOWFEATURETESTS_H
