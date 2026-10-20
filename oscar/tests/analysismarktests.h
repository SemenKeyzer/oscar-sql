/* Analysis mark Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QApplication;

class AnalysisMarkTests : public QObject
{
    Q_OBJECT
private slots:
    void testManualMark();
    void testForPrint();
    void testHeadingRole();
    void testOldNamesGone();
    void testBarMarkIsText();
    void initTestCase();
    void testPill();
    void testPrintPill();
    void testHeadingRow();
    void testTitle();
    void testTreeHeading();
    void testChannelBaseName();
    void cleanupTestCase();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(AnalysisMarkTests)
