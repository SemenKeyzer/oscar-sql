/* Help Panel Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPPANELTESTS_H
#define HELPPANELTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the help panel.
class HelpPanelTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testShowsHoveredArticle();
    void testKeepsArticle();
    void testSeeAlso();
    void testSearch();
    void testPanelWorksWithHoverOff();
    void testOpenRequestedShowsPanel();
    void testHiddenPanelRendersOnShow();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(HelpPanelTests)

#endif // HELPPANELTESTS_H
