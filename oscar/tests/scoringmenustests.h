/* Scoring Menus Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SCORINGMENUSTESTS_H
#define SCORINGMENUSTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the menus of the Daily manual scoring mode.
class ScoringMenusTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testRangeMenu();
    void testShortEventWarning();
    void testEventMenu();
    void testUndoOnlyForEdited();
    void testExcludedMenu();
    void testTreeNode();
    void testLetterKeys();
    void testBulkItems();
    void testExcludedText();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(ScoringMenusTests)

#endif // SCORINGMENUSTESTS_H
