/* Sleep Analysis Integration Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSISINTEGRATIONTESTS_H
#define ANALYSISINTEGRATIONTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests of the analysis inside OSCAR's data model: channels, sessions, storage.
class AnalysisIntegrationTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testAnalysisChannelsAreComputed();
    void testComputedChannelsAreNotReportedByDevice();
    void testAnalysisChannelsAreNotInAhi();
};
DECLARE_TEST(AnalysisIntegrationTests)

#endif // ANALYSISINTEGRATIONTESTS_H
