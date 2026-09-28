/* Löwenstein Prisma Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PRISMATESTS_H
#define PRISMATESTS_H

#include "AutoTest.h"
#include "../SleepLib/loader_plugins/prisma_loader.h"

class PrismaTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testLineApapPressureRange();
    void testLineTubeType();
    void testHypopneaDuringLeakIsImported();
    void testSoftPapLabelsShowTheLevel();
};

DECLARE_TEST(PrismaTests)

#endif // PRISMATESTS_H
