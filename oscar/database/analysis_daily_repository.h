/* Analysis Daily Repository Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_DAILY_REPOSITORY_H
#define ANALYSIS_DAILY_REPOSITORY_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>

#include "SleepLib/analysis/glasgow_counts.h"

/*!
 * \struct AnalysisDailyData
 * \brief One day of OSCAR's own sleep analysis (table analysis_daily, schema v20).
 *
 * Counts and seconds rather than indices, so that any period aggregates exactly (sum of
 * counts over sum of hours). Each group of columns is NULL in the table when it does not
 * apply that day (no flow, no oximetry, no pulse, nothing to compare); the has* flags
 * say which groups are present.
 */
struct AnalysisDailyData
{
    qint64 id = 0;
    qint64 profileId = 0;
    QDate date;
    int algoVersion = 0;
    QString paramsHash;        //!< day-scoring parameters
    QString inputsHash;        //!< the day's sessions, their stage 1 stamps and time corrections
    QDateTime computedAt;

    bool hasFlow = false;      //!< flow was analysed
    int flowSeconds = 0;       //!< scoreable flow time: the analysis' hours
    double flowRateHz = 0;     //!< lowest sample rate of the analysed sessions
    int unscoreableSeconds = 0;//!< flow time that could not be scored
    int nObstructiveApnea = 0, nCentralApnea = 0, nApnea = 0;
    int nObstructiveHypopnea = 0, nCentralHypopnea = 0, nHypopnea = 0;
    int nRera = 0;
    int nUnconfirmable = 0;    //!< candidates without SpO2 coverage, scored by flow only
    int nHypopneaAasm3 = 0, nHypopneaCms4 = 0, nHypopneaFlow = 0;   //!< under each rule
    int flSeconds = 0;
    double flSum = 0;
    int flBreaths = 0;
    bool hasFlRuns = false;    //!< the two below are known (analysed by a version that keeps them)
    int flLimitedBreaths = 0;  //!< scored breaths at or above the flow limitation threshold
    int flLongestSeconds = 0;  //!< the longest flow limitation span
    analysis::GlasgowCounts glasgow, glasgowAdapted;   //!< Glasgow Index counts (empty: none)
    int pbSeconds = 0;
    int hypopneaRule = 0;      //!< analysis::HypopneaRule actually applied

    bool hasComparison = false;
    int devApnea = 0, devHypopnea = 0, devRera = 0;
    int cmpMatched = 0, cmpDeviceOnly = 0, cmpAnalysisOnly = 0, cmpTypeMismatch = 0;

    bool hasOximetry = false;
    int oxiSeconds = 0;
    QString oxiScope;          //!< "night" or "cpap"
    QString oxiSource;         //!< device the SpO2 came from
    bool hasCpap = false;      //!< a CPAP session that night (links to breathing events)
    int nDesat3 = 0, nDesat4 = 0;
    QVector<int> spo2Hist;     //!< seconds per SpO2 % from 50 to 100
    double spo2Sum = 0;
    double spo2Median = 0;
    double spo2Nadir = 0;
    double desatArea = 0;          //!< %·s, all desaturations
    double linkedDesatArea = 0;    //!< %·s, those linked to analysed breathing events
    int nUnexplainedDesat = 0;
    int nCyclic = 0, cyclicSeconds = 0;
    int nZones = 0, zoneSeconds = 0, zoneSevereSeconds = 0;

    bool hasPulse = false;
    int pulseSeconds = 0;
    double pulseSum = 0, pulseSqSum = 0, pulseMin = 0, pulseMax = 0;
    QVector<int> pulseHist;    //!< seconds per bpm from 30 to 220
    int nPulseRise = 0;
    double dhrSum = 0;
    int nDhr = 0;
    int bradySeconds = 0, tachySeconds = 0;

    bool hasOffsetHint = false;
    qint64 oxiOffsetHintMs = 0;
    QString extraJson;
};

/*!
 * \class AnalysisDailyRepository
 * \brief Reads and writes analysis_daily. Derived data: never exported in backups,
 * recalculated after a restore.
 */
class AnalysisDailyRepository
{
  public:
    //! Inserts the row, or replaces the one for the same profile and date.
    bool upsert(const AnalysisDailyData &data);
    //! The row for a day; id 0 when there is none.
    AnalysisDailyData find(qint64 profileId, const QDate &date);
    //! Rows from \a from to \a to (inclusive), in date order.
    QList<AnalysisDailyData> findRange(qint64 profileId, const QDate &from, const QDate &to);
    bool remove(qint64 profileId, const QDate &date);
    bool removeRange(qint64 profileId, const QDate &from, const QDate &to);
};

#endif // ANALYSIS_DAILY_REPOSITORY_H
