/* Help Links Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPLINKSTESTS_H
#define HELPLINKSTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the explanations on OSCAR's HTML pages and graphs.
class HelpLinksTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testHelpLinkDoesNotNavigate();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(HelpLinksTests)

#endif // HELPLINKSTESTS_H
