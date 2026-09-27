/* Contec BLE Download Session Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QCoreApplication;

class ContecBleDownloaderTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testDownloadsAllRecords();
    void testSkipsUnwantedRecord();
    void testRetriesCorruptedPacket();
    void testTimeoutFails();
    void testRejectedCommandFails();
    void testUnsupportedModels();
    void testEraseNeedsPermission();
    void testEraseWithPermission();
    void testSetClock();
    void testEncryptedSession();
    void testStorageFallsBackToPrepare();
    void testNoRecords();
    void testLinkLostFails();
    void testExtraPacketWhileDeliveringIsIgnored();
    void cleanupTestCase();
private:
    QCoreApplication *m_app = nullptr;
};
DECLARE_TEST(ContecBleDownloaderTests)
