/* Sleep Analysis Parameters Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_PARAMS_H
#define ANALYSIS_PARAMS_H

#include <QString>

//! OSCAR's own analysis of a night ("second opinion"), independent of the device's
//! event scoring. See Notes/Developer Notes/SLEEP_ANALYSIS.md.
namespace analysis {

//! Bump whenever an algorithm changes in a way that changes its results: every stored
//! analysis then counts as outdated and is recalculated.
constexpr int kAnalysisAlgoVersion = 1;

//! How a flow reduction candidate becomes a hypopnea (stage 2).
enum class HypopneaRule {
    Auto = 0,       //!< AASM 3 % where SpO2 covers the event, Flow only elsewhere
    Aasm3 = 1,      //!< AASM 2012 rule 1A: >= 3 % desaturation
    Cms4 = 2,       //!< AASM rule 1B (US Medicare): >= 4 % desaturation
    FlowOnly = 3,   //!< flow reduction of at least DayParams::flowOnlyReduction
};

//! Oximetry analysis of one session (stage 1). Percent values are SpO2 points.
struct OxiParams {
    double desatMinDrop = 3;          //!< desaturation depth (points below the peak)
    double desatMinSec = 10;          //!< shortest desaturation
    double desatMaxFallSec = 120;     //!< a slower fall from peak to nadir is drift, not an event
    double desatMaxSec = 180;         //!< longer desaturations are cut at this length
    double pulseRise = 6;             //!< pulse rise above the baseline, beats per minute
    double bradyBpm = 40;             //!< bradycardia: pulse below this ...
    double tachyBpm = 120;            //!< tachycardia: pulse above this ...
    double bradyTachyMinSec = 30;     //!< ... for at least this long
    double zoneLowPct = 90;           //!< problem zones: "low" SpO2 threshold
    double zoneCriticalPct = 85;      //!< problem zones: "critical" SpO2 threshold
    double zoneWindowSec = 300;       //!< sliding window length
    double zoneStepSec = 30;          //!< sliding window step
    double zoneMinSec = 120;          //!< shorter zones are dropped
    double zoneMergeGapSec = 120;     //!< zones closer than this are merged
    double zoneLowSec = 60;           //!< seconds below the low threshold that flag a window
    double zoneCriticalSec = 30;      //!< seconds below the critical threshold that flag a window
    int zoneMinDesats = 3;            //!< desaturation nadirs in a window that flag it
};

//! Flow analysis of one CPAP session (stage 1). Reductions are fractions of the baseline.
struct FlowParams {
    double apneaReduction = 0.9;      //!< >= 90 % reduction for >= minEventSec: apnea
    double hypopneaReduction = 0.3;   //!< >= 30 % reduction: hypopnea candidate
    double minEventSec = 10;
    double maxEventSec = 120;         //!< longer reductions are unscoreable, not events
    double baselineWindowSec = 120;
    double baselinePercentile = 70;
    double flThreshold = 0.5;         //!< flow limitation score of a limited breath
    bool classifyApneas = true;       //!< experimental obstructive/central classification
};

//! Scoring of a whole day (stage 2), on device-corrected times.
struct DayParams {
    HypopneaRule rule = HypopneaRule::Auto;
    double flowOnlyReduction = 0.5;   //!< reduction a candidate needs under Flow only
    double linkWindowSec = 30;        //!< a desaturation links to an event ending up to this earlier
    bool limitOxiToCpap = false;      //!< oximetry metrics only over CPAP time
    bool pulseRiseAsArousal = false;  //!< a pulse rise also confirms a hypopnea (not AASM)
};

//! A snapshot of the profile's analysis settings. Loaders may run on worker threads,
//! so the analysis is always handed a copy taken on the main thread.
struct AnalysisParams {
    bool enabled = true;
    OxiParams oxi;
    FlowParams flow;
    DayParams day;

    //! Hashes of the parameters each stage depends on (with kAnalysisAlgoVersion): a
    //! stored result is outdated when its hash differs. 16 hex digits.
    QString oxiHash() const;
    QString flowHash() const;
    QString dayHash() const;
};

} // namespace analysis

#endif // ANALYSIS_PARAMS_H
