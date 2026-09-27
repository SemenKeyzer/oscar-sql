/* Contec BLE Import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

class ContecBleImportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testOximetryEventsSplitAtGaps();
    void testDecideRecord();
    void testPredictNight();
    void testToOxiRecords();
    void testCanErase();
    void testEraseSettingDefaultsOff();
    void testImporterImportsNewRecord();
    void testImporterReplacesShorterRecord();
    void testImporterRefusesSecondOximeterOnANight();
    void testImporterKeepsOldSessionWhenReplacementIsRejected();
    void testImporterReportsDatabaseFailure();
    void testImporterReportsMissingSamples();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(ContecBleImportTests)
