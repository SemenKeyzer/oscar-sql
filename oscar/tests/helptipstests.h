/* Hover Explanations Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPTIPSTESTS_H
#define HELPTIPSTESTS_H

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the hover explanations: the switch, tagged widgets and HTML terms.
class HelpTipsTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testAttachShowsTooltip();
    void testDisabled();
    void testTerm();
    void testTermEscapes();
    void testAttachAllByObjectName();
    void testLabelTakesBuddyKey();
    void testLabelTakesNeighbourKey();
    void testMenuHoverFollowsAction();
    void testMenuHoverSkipsDynamicItems();
    void testSubmenuHoverExplainsMenu();
    void testMenuTooltipUsesActiveAction();
private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(HelpTipsTests)

#endif // HELPTIPSTESTS_H
