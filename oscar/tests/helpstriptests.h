/* Help Strip Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPSTRIPTESTS_H
#define HELPSTRIPTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the explanation strip at the bottom of modal dialogs.
class HelpStripTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testEmptyStateText();
    void testShowsHoveredEntry();
    void testKeepsEntryAfterLeave();
    void testStripIgnoresOtherWindows();
    void testStripHiddenWhenOff();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(HelpStripTests)

#endif // HELPSTRIPTESTS_H
