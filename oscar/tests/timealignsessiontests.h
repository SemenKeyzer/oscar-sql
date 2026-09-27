/* Time Alignment Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

class TimeAlignSessionTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testBeginReadsSavedAndOtherCorrections();
    void testBeginRejectsMachineWithoutDatabaseId();
    void testPreviewDoesNotWrite();
    void testCommitReplacesNightRow();
    void testCommitWithoutChangeDoesNotWrite();
    void testZeroCommitRemovesNightRow();
    void testCancelRestoresStoredCorrections();
    void testCommitFailureKeepsPreview();
    void testBeginOnAnotherMachineRestoresFirst();
    void testPreviousNightOffset();
    void testOffsetIsClamped();
    void testSnapStep();
    void testSnapDelta();
    void testFormatOffset();
    void testRepositoryStoreRoundTrip();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
};
DECLARE_TEST(TimeAlignSessionTests)
