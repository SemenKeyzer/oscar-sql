/* Sleep Analysis Event Matcher Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef EVENTMATCHERTESTS_H
#define EVENTMATCHERTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for matching the device's respiratory events with the analysis' ones.
class EventMatcherTests : public QObject
{
    Q_OBJECT
private slots:
    void testOverlappingEventsMatch();
    void testToleranceWidensIntervals();
    void testGreedyTakesLargestOverlap();
    void testEachEventMatchesOnce();
    void testSameGroupBeforeTypeMismatch();
    void testTypeMatrix();
    void testAgreement();
    void testDeviceEventConventions();
};
DECLARE_TEST(EventMatcherTests)

#endif // EVENTMATCHERTESTS_H
