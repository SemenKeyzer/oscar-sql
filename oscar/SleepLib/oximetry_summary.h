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

//! \brief One night's oximetry in figures, as the Daily view's oximeter block counts them.
struct OximetryNight {
    bool valid = false;
    QString device;              //!< brand and model of the oximetry source
    double hours = 0;            //!< recorded time
    bool spotChecks = false;     //!< spot checks (Apple Health): only the counts below apply
    int spotSpo2 = 0, spotPulse = 0;
    double spo2Avg = 0, spo2Min = 0;
    double minutesBelow90 = 0, percentBelow90 = 0;
    int desaturations = 0;       //!< OSCAR's SpO2 drop events, per the profile's thresholds
    double pulseAvg = 0, pulseMin = 0, pulseMax = 0;
};

//! \brief Summarises the oximetry of \a day recorded by its \a source machine type
//! (MT_OXIMETER, or MT_CPAP for a device with a built-in oximeter).
OximetryNight summarizeOximetry(Day *day, MachineType source);

#endif // OXIMETRY_SUMMARY_H
