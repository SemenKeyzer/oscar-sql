/* Device Settings Comparison Tests Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SETTINGSCOMPARISONTESTS_H
#define SETTINGSCOMPARISONTESTS_H

#include "tests/AutoTest.h"

//! \brief Tests for the Statistics table comparing device settings.
class SettingsComparisonTests : public QObject
{
    Q_OBJECT
private slots:
    void testGroupMergesSameSettings();
    void testGroupKeepsDifferentSettingsApart();
    void testRowFigures();
    void testBest();
    void testDateList();
    void testSettingsLabel();
    void testHtmlMarksBestAndFewNights();
    void testHtmlShowsDashWithoutData();
    void testHtmlDeviceColumn();
    void testHtmlEscapesSettings();
};
DECLARE_TEST(SettingsComparisonTests)

#endif // SETTINGSCOMPARISONTESTS_H
