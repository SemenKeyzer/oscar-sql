/* Channel Store Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CHANNELSTORETESTS_H
#define CHANNELSTORETESTS_H

#include "tests/AutoTest.h"

class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for what a profile keeps of its channels' names and options.
class ChannelStoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testOnlyTheUsersNamesAreKept();
    void testRegisteredOptionsWin();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(ChannelStoreTests)

#endif // CHANNELSTORETESTS_H
