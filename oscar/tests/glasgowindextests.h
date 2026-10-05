/* Glasgow Index Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GLASGOWINDEXTESTS_H
#define GLASGOWINDEXTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the Glasgow Index: the port of FlowLimits.js and the adapted variant.
class GlasgowIndexTests : public QObject
{
    Q_OBJECT
private slots:
    void testMatchesFlowLimitsJs_data();
    void testMatchesFlowLimitsJs();
    void testEachComponent();
    void testSampleRateIndependent();
    void testShortChunks();
    void testCountsText();
    void testAdaptedMatchesOriginalAt30();
    void testAdaptedIgnoresWeakAmplitude();
    void testAdaptedSkipsBlocked();
    void testSeries();
    void testAdaptedSampleRateIndependent();
    void testAdaptedIgnoresSlowOnset();
    void testAdaptedPauseWithOffset();
};
DECLARE_TEST(GlasgowIndexTests)

#endif // GLASGOWINDEXTESTS_H
