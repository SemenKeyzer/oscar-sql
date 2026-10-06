/* Scoring Mode Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SCORINGMODETESTS_H
#define SCORINGMODETESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for what a mouse gesture on a Daily graph means in manual scoring mode.
class ScoringModeTests : public QObject
{
    Q_OBJECT
private slots:
    void testDragSelectsInScoringMode();
    void testDragZoomsOutsideScoringMode();
    void testRightClickRequestsMenu();
    void testOtherGraphsUnaffected();
    void testRangeFromPixels();
};
DECLARE_TEST(ScoringModeTests)

#endif // SCORINGMODETESTS_H
