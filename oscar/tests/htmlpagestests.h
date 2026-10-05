/* HTML Pages Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HTMLPAGESTESTS_H
#define HTMLPAGESTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for laying HTML over printer pages.
class HtmlPagesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testShortHtml();
    void testLongTable();
    void testAfterAnotherPage();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(HtmlPagesTests)

#endif // HTMLPAGESTESTS_H
