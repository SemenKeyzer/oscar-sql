/* UI Snapshot Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UISHOTSTESTS_H
#define UISHOTSTESTS_H

#include "tests/AutoTest.h"

class QApplication;
class QTemporaryDir;

//! \brief Dialog layout: checks of the doctor's layout remarks, and pictures of the dialogs
//! without a screen when OSCAR_UI_SHOTS names a folder (local use).
class UiShotsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testPreferencesShots();
    void testPreferencesFrames();
    void cleanupTestCase();
private:
    QApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};
DECLARE_TEST(UiShotsTests)

#endif // UISHOTSTESTS_H
