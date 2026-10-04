/* Preferences Search Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PREFERENCESSEARCHTESTS_H
#define PREFERENCESSEARCHTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for finding a setting in the Preferences dialog.
class PreferencesSearchTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testNormalized();
    void testFind();
    void testReveal();

private:
    class QApplication *m_app = nullptr;
};
DECLARE_TEST(PreferencesSearchTests)

#endif // PREFERENCESSEARCHTESTS_H
