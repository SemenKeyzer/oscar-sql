/* Contec RS01 Clock Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01CLOCKTESTS_H
#define RS01CLOCKTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the RS01's HID commands (identify, set clock) and its replies.
class RS01ClockTests : public QObject
{
    Q_OBJECT

private slots:
    void testChecksum();
    void testIdentifyCommand();
    void testClockCommand();
    void testClockYearRange();
    void testReplies();
    void testErrorTexts();
};
DECLARE_TEST(RS01ClockTests)

#endif // RS01CLOCKTESTS_H
