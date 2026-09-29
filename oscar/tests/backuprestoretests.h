/* Backup / Restore Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

class BackupRestoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testProfileNameProblem_data();
    void testProfileNameProblem();
    void testReadOnlyScopeBlocksWrites();
    void testPrivacyPackageLeavesOutPersonalData();
    void testRestoreRejectsUnsafeProfileName();
    void testRestoreRebuildsDataFolder();
    void testExtractKeepsEntriesInsideRoot();
    void cleanupTestCase();

private:
    QString makePackage(bool privacy, const QString& subdir);

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    qint64 m_profileId = 0;
};
DECLARE_TEST(BackupRestoreTests)
