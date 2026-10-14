/* O2Ring wizard page Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QApplication;
class QTemporaryDir;

class O2RingPageTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testRecordsListed();
    void testDownloadImportsChecked();
    void testSetClockFollowsSetting();
    void testCancelRemovesPart();
    void testLinkLostOffersRetry();
    void cleanupTestCase();
private:
    QApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(O2RingPageTests)
