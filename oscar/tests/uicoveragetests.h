/* UI Coverage Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UICOVERAGETESTS_H
#define UICOVERAGETESTS_H

#include "tests/AutoTest.h"

class QApplication;
class QTemporaryDir;

//! \brief Every menu item, button and setting has an explanation shown on hover.
class UiCoverageTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testMenusCovered();
    void testMainCovered();
    void testDailyCovered();
    void testOverviewCovered();
    void testWelcomeCovered();
    void testPrefsCovered();
    void testOximportCovered();
    void testCodeCreatedControlsCovered();
    void testAlignBarCovered();
    void testHelpPanelCovered();
private:
    QApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;   // a profile for the Bluetooth page
    QString m_previousAppData;
};
DECLARE_TEST(UiCoverageTests)

#endif // UICOVERAGETESTS_H
