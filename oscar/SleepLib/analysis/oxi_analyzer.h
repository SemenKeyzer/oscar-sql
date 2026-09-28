/* Sleep Analysis Oximetry Analyzer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_OXI_ANALYZER_H
#define ANALYSIS_OXI_ANALYZER_H

#include <QVector>

#include "analysis_params.h"
#include "signal_utils.h"

namespace analysis {

//! A time span, as OSCAR stores span events: the event's time is \a end, its duration
//! end - start; \a value is an extra quantity (EventList data2).
struct Span {
    qint64 start = 0;
    qint64 end = 0;
    float value = 0;
};

//! A fall of SpO2 from a peak to a nadir and back (spec §3.2.1).
struct Desaturation {
    qint64 start = 0;       //!< first second at least 1 point below the peak
    qint64 nadirTime = 0;
    qint64 end = 0;         //!< first second back near the peak (or 2/3 recovered)
    float peak = 0;
    float nadir = 0;
    double area = 0;        //!< sum of (peak - SpO2) over the event, %·s
    float depth() const { return peak - nadir; }
};

//! A rise of the pulse rate over its recent baseline (spec §3.2.4).
struct PulseRise {
    qint64 start = 0;
    qint64 end = 0;
    float amplitude = 0;    //!< highest pulse in the rise minus the baseline, bpm
};

//! A stretch of the night worth looking at (spec §3.2.8).
struct ProblemZone {
    qint64 start = 0;
    qint64 end = 0;
    int severity = 1;       //!< 1 moderate, 2 marked
    float minSpo2 = 0;
    int desaturations = 0;  //!< with the nadir inside the zone
    int lowSeconds = 0;     //!< below OxiParams::zoneLowPct
    int criticalSeconds = 0;//!< below OxiParams::zoneCriticalPct
    float meanPulse = kNoData;
    int pulseRises = 0;     //!< ending inside the zone
};

//! SpO2 50-100 % and pulse 30-220 bpm histograms: seconds per integer value.
constexpr int kSpo2HistMin = 50, kSpo2HistMax = 100;
constexpr int kPulseHistMin = 30, kPulseHistMax = 220;

//! Everything the oximetry analysis of one session finds.
struct OxiResult {
    bool hasSpo2 = false;
    bool hasPulse = false;

    QVector<Desaturation> desaturations;
    QVector<Span> cyclic;           //!< runs of desaturations (value: their number)
    QVector<PulseRise> pulseRises;
    QVector<Span> bradycardia;
    QVector<Span> tachycardia;
    QVector<ProblemZone> zones;

    QVector<int> spo2Hist;          //!< [value - kSpo2HistMin], seconds
    QVector<int> pulseHist;         //!< [value - kPulseHistMin], seconds

    int spo2Seconds = 0;
    double spo2Sum = 0;
    float spo2Median = kNoData;
    float spo2Nadir = kNoData;
    int pulseSeconds = 0;
    double pulseSum = 0;
    double pulseSqSum = 0;
    float pulseMin = kNoData;
    float pulseMax = kNoData;

    int countDesaturations(double minDepth) const;
    double desaturationArea() const;           //!< %·s, all desaturations
    int cyclicSeconds() const;
    int zoneSeconds(int minSeverity = 1) const;
    int bradySeconds() const;
    int tachySeconds() const;
    //! Seconds of SpO2 below \a threshold (%), from the histogram.
    int spo2SecondsBelow(double threshold) const;
};

//! SpO2 ready for scoring (spec §3.1.3-5): out-of-range values dropped, spikes of
//! >= 10 points that return within 5 s removed, 5 s median, and stretches of data
//! shorter than 60 s (between gaps of >= 10 s) dropped.
Grid cleanSpo2(const Grid &raw);
//! The pulse counterpart of cleanSpo2() (no spike rule).
Grid cleanPulse(const Grid &raw);

//! Analyses one session's SpO2 and pulse, both on 1 Hz grids (NaN = no data).
OxiResult analyzeOximetry(const Grid &spo2, const Grid &pulse, const OxiParams &params);

} // namespace analysis

#endif // ANALYSIS_OXI_ANALYZER_H
