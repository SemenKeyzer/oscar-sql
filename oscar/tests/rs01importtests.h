/* Contec RS01 Import Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01IMPORTTESTS_H
#define RS01IMPORTTESTS_H

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for importing a Contec RS01 recording into an oximeter session.
class RS01ImportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testNasalFlowChannel();
    void testImportBuildsSession();
    void testImportTwiceAlreadyThere();
    void testSameFolderNameNextYear();
    void testImportFromRenamedCopy();
    void testCopyLeavesSourceUntouched();
    void testHdWithoutSegmentsFails();
    void testSessionIdStable();
    void testWritableWarningText();
    void testPageListsRecordings();
    void testRealNightAnalysedIfGiven();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(RS01ImportTests)

#endif // RS01IMPORTTESTS_H
