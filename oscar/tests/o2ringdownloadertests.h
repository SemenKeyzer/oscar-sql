/* O2Ring Downloader Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RINGDOWNLOADERTESTS_H
#define O2RINGDOWNLOADERTESTS_H

#include "tests/AutoTest.h"

//! \brief The O2Ring downloader against a simulated ring: commands, pieces, retries, failures.
class O2RingDownloaderTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDownloadTwoFiles();
    void testRetryAfterLostAnswer();
    void testLateDuplicate();
    void testLateFirstPiece();
    void testLateWithoutSeqEcho();
    void testLinkLostMidFile();
    void testWrongSize();
    void testSetClock();
    void testInfoTimeout();
    void testRingWithoutSeqEcho();
private:
    class QCoreApplication *m_app = nullptr;
};
DECLARE_TEST(O2RingDownloaderTests)

#endif // O2RINGDOWNLOADERTESTS_H
