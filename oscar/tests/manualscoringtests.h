/* Manual Scoring Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MANUALSCORINGTESTS_H
#define MANUALSCORINGTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the doctor's manual scoring of respiratory events.
class ManualScoringTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    // the calculation
    void testNoEditsNoChange();
    void testAdd();
    void testRemove();
    void testRetype();
    void testLatestEditWins();
    void testExclude();
    void testExcludeOverlapAndClip();
    void testExcludeWholeNight();
    void testAddedInsideExcludeNotCounted();
    // storage
    void testStoreAndLoadEdits();
    void testRemoveEdit();
    void testSummaryRoundTrip();
    void testSessionDeleteCascades();
    void testMigration21To22();
    void cleanupTestCase();
private:
    qint64 sessionRow(qint64 deviceSessionId);
    class QTemporaryDir *m_tempDir = nullptr;
    class QCoreApplication *m_app = nullptr;
    qint64 m_machineRow = 0;
};
DECLARE_TEST(ManualScoringTests)

#endif // MANUALSCORINGTESTS_H
