/* O2Ring import Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

class O2RingImportTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testImportSavesAndParses();
    void testAlreadyImported();
    void testInvalidFileNotSaved();
    void testSetClockSettingDefault();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(O2RingImportTests)
