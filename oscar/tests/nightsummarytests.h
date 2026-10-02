/* Start Screen Night Summary Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef NIGHTSUMMARYTESTS_H
#define NIGHTSUMMARYTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the start screen's summary of the last night.
class NightSummaryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLevelsAgainstTargets();
    void testTrendFigures();
    void testConcerns();
    void testSpo2AgainstTargets();
    void testKeyFiguresHtml();
    void testActions();
    void testViewShowsTheNight();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(NightSummaryTests)

#endif // NIGHTSUMMARYTESTS_H
