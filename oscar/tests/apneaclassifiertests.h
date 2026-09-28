/* Sleep Analysis Apnea Classifier Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef APNEACLASSIFIERTESTS_H
#define APNEACLASSIFIERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the heuristic obstructive/central apnea classification.
class ApneaClassifierTests : public QObject
{
    Q_OBJECT
private slots:
    void testOscillationShareOnPureOscillation();
    void testOscillationShareOnRipple();
    void testOscillationShareNeedsPulseMatch();
    void testSlowRecordingCannotTell();
    void testClassForScore();
    void testCentralApneaWithOscillations();
    void testObstructiveApneaAfterLimitedBreaths();
    void testApneaWithoutEvidenceIsUnclassified();
    void testObstructLevelDecides();
    void testClassificationCanBeSwitchedOff();
};
DECLARE_TEST(ApneaClassifierTests)

#endif // APNEACLASSIFIERTESTS_H
