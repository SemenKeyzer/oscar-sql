/* Sleep Analysis of One Day Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_DAY_ANALYSIS_H
#define ANALYSIS_DAY_ANALYSIS_H

#include <QList>
#include <QSet>
#include <QString>

#include "analysis_params.h"
#include "day_scorer.h"
#include "database/analysis_daily_repository.h"
#include "SleepLib/machine_common.h"

class Day;
class Session;

namespace analysis {

//! The day's enabled sessions the analysis looks at: CPAP and oximetry.
QList<Session *> analysableSessions(Day *day);

//! The event channels day scoring reads (the analysis' stage 1 and hypopnea channels,
//! the device's respiratory events, SpO2 and pulse), so that it can load only these.
QSet<ChannelID> dayScoringChannels();

//! Hash of everything day scoring depends on (spec §4.4): the algorithm version, the
//! day parameters and, per CPAP and oximetry session of the day, its identity, enabled
//! state, time range, time correction, stage 1 stamp and device event counts.
QString dayInputsHash(Day *day, const AnalysisParams &params);

//! Whether the day's stored analysis (\a stored, id 0 when none) is missing or outdated,
//! including stage 1 of any of its sessions. Reads only summaries.
bool dayOutdated(Day *day, const AnalysisParams &params, const AnalysisDailyData &stored);

//! The scorer's input from the day's sessions, on device-corrected times. Their events
//! must be in memory (at least dayScoringChannels()). \a oxiSource receives the name of
//! the device the SpO2 comes from.
DayInput buildDayInput(const QList<Session *> &sessions, QString *oxiSource = nullptr);

//! The analysis_daily row of a scored day.
AnalysisDailyData toDailyRow(const DayResult &result, const AnalysisParams &params, const QString &inputsHash,
                             const QString &oxiSource);

//! What analyzeDay() did.
struct DayAnalysis {
    bool scored = false;        //!< the day was scored (result is valid)
    bool upToDate = false;      //!< nothing to do: the stored analysis is current
    bool stored = false;        //!< hypopnea channels and row written
    DayResult result;
    QString oxiSource;
};

//! Stage 2 of a day. Brings its sessions' stage 1 up to date first (loading their events
//! in full where needed; with \a redoStageOne, even where it is current); then, unless
//! \a onlyIfOutdated and the stored row is current, scores the day and stores its
//! hypopnea channels and its analysis_daily row. Events it had to load are put away
//! again. Does nothing when the analysis is switched off.
DayAnalysis analyzeDay(Day *day, const AnalysisParams &params, bool onlyIfOutdated = true, bool redoStageOne = false);

//! Scores the day without storing anything (for display), loading only the events it
//! needs and putting them away again. Stage 1 must be current.
DayResult scoreStoredDay(Day *day, const AnalysisParams &params, QString *oxiSource = nullptr);

} // namespace analysis

#endif // ANALYSIS_DAY_ANALYSIS_H
