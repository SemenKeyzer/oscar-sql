/* Sleep Analysis Panel Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISPANELTESTS_H
#define ANALYSISPANELTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the Daily view's analysis section and Analysis tab.
class AnalysisPanelTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testFormatting();
    void testSidebarForCpapNight();
    void testSidebarForOximetryOnlyNight();
    void testFlowLimitationLine();
    void testGlasgowRows();
    void testGlasgowDash();
    void testFlowLimitationLineOldStamp();
    void testTabListsDifferences();
    void testTabStepsThroughDifferences();
    void testDifferenceSpans();
    void testTabTellsTheFlowGraphWhichDifference();
    void testStatisticsFigures();
    void testStatisticsFigureValues();
    void testPreferencesPage();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(AnalysisPanelTests)

#endif // ANALYSISPANELTESTS_H
