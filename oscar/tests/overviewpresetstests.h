/* Overview Graph Presets Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWPRESETSTESTS_H
#define OVERVIEWPRESETSTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the Overview's ready-made graph sets.
class OverviewPresetsTests : public QObject
{
    Q_OBJECT
private slots:
    void testKeys();
    void testPresetGraphs();
    void testVisibility();
    void testLeavingPresetRestoresOwnChoice();
    void testSavingUnderPresetKeepsOwnChoice();
    void testReloadKeepsPreset();
};
DECLARE_TEST(OverviewPresetsTests)

#endif // OVERVIEWPRESETSTESTS_H
