/* Scoring Mode Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmodetests.h"

#include "Graphs/scoringgesture.h"
#include "common_gui.h"

using namespace ScoringGesture;

void ScoringModeTests::testDragSelectsInScoringMode()
{
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::LeftButton, true), Action::SelectRange);
}

void ScoringModeTests::testDragZoomsOutsideScoringMode()
{
    QCOMPARE(actionFor(false, STR_GRAPH_FlowRate, Qt::LeftButton, true), Action::None);
    QCOMPARE(actionFor(false, STR_GRAPH_FlowRate, Qt::RightButton, false), Action::None);
}

void ScoringModeTests::testRightClickRequestsMenu()
{
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::RightButton, false), Action::ContextMenu);
    QCOMPARE(actionFor(true, STR_GRAPH_SleepFlags, Qt::RightButton, false), Action::ContextMenu);
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::LeftButton, false), Action::None);   // a plain click zooms as before
    QCOMPARE(actionFor(true, STR_GRAPH_FlowRate, Qt::RightButton, true), Action::None);   // a right drag pans
}

void ScoringModeTests::testOtherGraphsUnaffected()
{
    QCOMPARE(actionFor(true, STR_GRAPH_Pressure, Qt::LeftButton, true), Action::None);
    QCOMPARE(actionFor(true, STR_GRAPH_Pressure, Qt::RightButton, false), Action::None);
    QCOMPARE(actionFor(true, STR_GRAPH_SleepFlags, Qt::LeftButton, true), Action::None);   // ranges only on the flow
}

void ScoringModeTests::testRangeFromPixels()
{
    // 1000 px for 100 s starting at 10 000 ms; dragged right to left, partly outside the plot
    const QPair<qint64, qint64> r = rangeFor(10000, 110000, 1000, 600, 200);
    QCOMPARE(r.first, qint64(30000));
    QCOMPARE(r.second, qint64(70000));
    const QPair<qint64, qint64> clipped = rangeFor(10000, 110000, 1000, -50, 1200);
    QCOMPARE(clipped.first, qint64(10000));
    QCOMPARE(clipped.second, qint64(110000));
}
