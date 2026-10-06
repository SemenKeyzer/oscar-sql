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
    // manual scoring on real sessions and days
    void testDayCountsEdits();
    void testAddedTypeAbsentFromDevice();
    void testAhiHoursSubtractsExcluded();
    void testDisabledSessionEditsIgnored();
    void testNotFoundAfterRebuild();
    void testClearDayRestoresDevice();
    void testEditsSurviveReopen();
    void testAhiSameEverywhere();
    void testScoringAllowedInClinicalMode();
    void testPdfShowsDeviceAhi();
    void testStatisticsFootnote();
    // final review
    void testScoringFollowsTimeCorrection();
    void testExcludeSkipsMaskOff();
    void testScoringWithPartialEvents();
    void testFullyExcludedNightRdi();
    void testRxCacheKeepsAhiHours();
    void testUndoStretchAcrossSessions();
    void testDailySummaryFollowsScoring();
    void initTestCase();
    void cleanupTestCase();
    void testAnalysisChannelsAreComputed();
    void testComputedChannelsAreNotReportedByDevice();
    void testAnalysisChannelsAreNotInAhi();
    void testStageOneWritesFlowChannelsAndStamp();
    void testStageOneOximetry();
    void testStageOneSkipsSessionWithoutEvents();
    void testStageOneOffDoesNothing();
    void testStoreChannelEventsLeavesWaveform();
    void testPartialSessionIsNeverStoredInFull();
    void testStampIsStoredAsText();
    void testDailyRowRoundTrip();
    void testDailyRowReplacesSameDay();
    void testDailyRowRangeAndRemove();
    void testMigrationAddsAnalysisDaily();
    void testMigrationV21KeepsRows();
    void testToDailyRowCarriesGlasgow();
    void testGlasgowFigures();
    void testGlasgowPeriodSkipsEmptyNights();
    void testStatisticsRowKeys();
    void testPdfStatisticsHaveNoHelpLinks();
    void testRdiRowExplainsRdi();
    void testFlRunsSkipOldNights();
    void testOldStampHasNoLimitedBreaths();
    void testDayAnalysisScoresAndStores();
    void testDayAnalysisLoadsOnlyWhatItNeeds();
    void testAnalysisSettingsRoundTrip();
    void testAnalysisServiceKeepsDaysCurrent();
    void testFlagsGraphsSplitDeviceAndAnalysis();
    void testAnalysisReportQuery();
    void testSettingsPeriodCountsCpapHoursOnly();
    void testSettingsComparisonRowsTrimmedToDates();
    void testDoctorReportCountsCpapHoursOnly();
    void testDoctorReportSettingsSince();
    void testDoctorReportAnalysisOnCpapNightsOnly();
    void testNightSummaryTimeAtMaximum();
    void testPeriodHtmlSections();
    void testPeriodHtmlPersonalData();
    void testPeriodHtmlClampsToData();
    void testWriterSummaryAndStatistics();
    void testWriterRefusesEmptyPeriod();
    void testWriterRestoresAndCleansUp();
    void testPdfReportOptionsSurviveProfileSave();
    void testPeriodHtmlSerialNumbers();
    void testOximetryRows();

private:
    class QCoreApplication *m_app = nullptr;
    class QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    qint64 m_profileId = 0;
    qint64 m_machineRow = 0;
};
DECLARE_TEST(AnalysisIntegrationTests)

#endif // ANALYSISINTEGRATIONTESTS_H
