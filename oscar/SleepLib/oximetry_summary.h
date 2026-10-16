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
#include <limits>
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
    //! analysis: the hypoxic burden (%·min/h) from every desaturation; NaN without the analysis
    double hypoxicBurden = std::numeric_limits<double>::quiet_NaN();
};

//! \brief The hypoxic burden from the oximeter alone: the area of every desaturation below
//! its own baseline (\a desatArea, %·s) per hour of \a seconds with SpO2, in %·min/h, as in
//! Esmaeili et al. 2023 (desaturations found automatically). NaN without time with SpO2.
double hypoxicBurden(double desatArea, int seconds);

//! The grades Braebon gives on its MediOX oximeter's report.
enum class OxiGrade { Normal, Mild, Moderate, Severe };
//! The upper bounds of the normal, mild and moderate grades of the hypoxic burden (%·min/h).
constexpr double kHypoxicGradeBounds[] = { 13, 32, 71 };
//! The same bounds for the ODI (per hour).
constexpr double kOdiGradeBounds[] = { 5, 15, 30 };
//! Normal below bounds[0], mild below bounds[1], moderate up to bounds[2], severe above.
OxiGrade oxiGrade(double value, const double (&bounds)[3]);
inline OxiGrade hypoxicBurdenGrade(double hb) { return oxiGrade(hb, kHypoxicGradeBounds); }
inline OxiGrade odiGrade(double odi) { return oxiGrade(odi, kOdiGradeBounds); }
//! "normal", "mild", "moderate" or "severe", translated.
QString oxiGradeName(OxiGrade grade);
//! Green, yellow, orange or red, as "#rrggbb".
QString oxiGradeColor(OxiGrade grade);
//! The grade's name in bold in its colour, as HTML.
QString oxiGradeHtml(OxiGrade grade);
//! The whole scale ("normal < 5 · mild 5–15 · …") as HTML, with \a current's step in its colour.
QString oxiGradeScaleHtml(const double (&bounds)[3], OxiGrade current);

//! \brief Summarises the oximetry of \a day recorded by its \a source machine type
//! (MT_OXIMETER, or MT_CPAP for a device with a built-in oximeter), from the samples,
//! with the classic SpO2 drop count. Loads only the SpO2 and pulse of sessions whose
//! events are not in memory.
OximetryNight summarizeOximetry(Day *day, MachineType source);

//! \brief The same summary from a night's stored analysis (invalid without oximetry).
OximetryNight summarizeOximetry(const AnalysisDailyData &row);

#endif // OXIMETRY_SUMMARY_H
