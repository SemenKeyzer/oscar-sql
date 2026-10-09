/* TabButtons Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TABBUTTONSTESTS_H
#define TABBUTTONSTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the rows of buttons that stand in for a tab bar (Daily's left panel).
class TabButtonsTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testButtonsFollowPages();
    void testHidePage();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(TabButtonsTests)

#endif // TABBUTTONSTESTS_H
