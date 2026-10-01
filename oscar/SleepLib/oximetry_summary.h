/* Oximetry night summary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETRY_SUMMARY_H
#define OXIMETRY_SUMMARY_H

#include <QString>
#include "SleepLib/machine_common.h"

class Day;
struct AnalysisDailyData;

//! \brief One night's oximetry in figures, for the start screen.
//!
//! From OSCAR's analysis when the night has been analysed (the figures the Daily
//! analysis panel and the Overview show), otherwise from the samples with the classic
//! SpO2 drop count. Times and shares count only the time with valid readings.
struct OximetryNight {
    bool valid = false;
    bool fromAnalysis = false;   //!< figures from OSCAR's analysis, else the classic count
    QString device;              //!< brand and model of the oximetry source
    double hours = 0;            //!< time with valid SpO2 readings (pulse when there is no SpO2)
    bool spotChecks = false;     //!< spot checks (Apple Health): only the counts below apply
    int spotSpo2 = 0, spotPulse = 0;
    double spo2Avg = 0, spo2Min = 0;
    double minutesBelow90 = 0, percentBelow90 = 0;
    //! analysis: desaturations of >= 3 % (ODI 3 %); classic: OSCAR's SpO2 drop events
    int desaturations = 0;
    int desaturations4 = 0;      //!< analysis: desaturations of >= 4 %
    int zones = 0;               //!< analysis: problem zones
    double zoneMinutes = 0;
    double dropPercent = 0, dropSeconds = 0;   //!< classic: the SpO2 drop thresholds
    double pulseAvg = 0, pulseMin = 0, pulseMax = 0;
};

//! \brief Summarises the oximetry of \a day recorded by its \a source machine type
//! (MT_OXIMETER, or MT_CPAP for a device with a built-in oximeter), from the samples,
//! with the classic SpO2 drop count. Loads only the SpO2 and pulse of sessions whose
//! events are not in memory.
OximetryNight summarizeOximetry(Day *day, MachineType source);

//! \brief The same summary from a night's stored analysis (invalid without oximetry).
OximetryNight summarizeOximetry(const AnalysisDailyData &row);

#endif // OXIMETRY_SUMMARY_H
