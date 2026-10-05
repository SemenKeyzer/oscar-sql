/* Sleep Analysis Day Scorer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_DAY_SCORER_H
#define ANALYSIS_DAY_SCORER_H

#include <QString>
#include <QVector>

#include "analysis_params.h"
#include "glasgow_counts.h"
#include "event_matcher.h"
#include "oxi_analyzer.h"

namespace analysis {

//! A respiratory event on the day's time line (device-corrected time).
struct DayEvent {
    qint64 start = 0;
    qint64 end = 0;
    RespEvent type = RespEvent::Apnea;
    int source = -1;        //!< index into DayInput::cpap of the session it belongs to
    float value = 0;        //!< hypopnea candidates: the flow reduction, as a fraction
    qint64 stamp = 0;       //!< device events: the time stamp as the device stored it
};

//! One of the day's CPAP sessions, with the flow totals of its stage 1 stamp.
struct CpapSession {
    Span span;              //!< device-corrected
    bool analyzed = false;  //!< its flow was analysed
    double sampleRateHz = 0;
    bool flScored = false;
    int flowSeconds = 0;    //!< scoreable flow time
    int unscoreableSeconds = 0;
    double flSum = 0;
    int flBreaths = 0;
    int flLimitedBreaths = 0;
    GlasgowCounts glasgow, glasgowAdapted;
};

//! A value at a moment (a breath's flow limitation score).
struct TimedValue {
    qint64 t = 0;
    float v = 0;
};

//! Everything day scoring needs, on device-corrected times: the stage 1 channels of the
//! day's CPAP sessions, the device's respiratory events and the oximetry of one source.
struct DayInput {
    QVector<CpapSession> cpap;          //!< enabled CPAP sessions
    QVector<DayEvent> apneas;           //!< AN_ObstructiveApnea, AN_CentralApnea, AN_Apnea
    QVector<DayEvent> candidates;       //!< AN_FlowReduction (type Hypopnea, value: reduction)
    QVector<Span> reras;                //!< AN_RERA
    QVector<Span> flowLimitation;       //!< AN_FlowLimitation
    QVector<Span> periodic;             //!< AN_PeriodicBreathing
    QVector<Span> unscoreable;          //!< AN_Unscoreable
    QVector<TimedValue> flScores;       //!< AN_FLScore, one per breath
    QVector<DayEvent> deviceEvents;     //!< the device's apneas, hypopneas and RERAs

    Grid spo2;                          //!< 1 Hz, as recorded (NaN: no data)
    Grid pulse;
    bool oxiFromSeparateDevice = false; //!< SpO2 not from the CPAP: its clock may be off
};

//! The day's scoring (spec §3.4).
struct DayResult {
    bool hasCpap = false;
    bool hasFlow = false;               //!< some CPAP session's flow was analysed
    double flowRateHz = 0;              //!< lowest sample rate of the analysed sessions
    bool flScored = false;              //!< flow limitation scored in every analysed session
    int flowSeconds = 0;
    int unscoreableSeconds = 0;
    double flSum = 0;
    int flBreaths = 0;
    int flSeconds = 0;                  //!< in flow limitation spans
    int flLongestSeconds = 0;           //!< the longest of those spans
    int flLimitedBreaths = 0;           //!< scored breaths at or above the threshold
    GlasgowCounts glasgow;              //!< Glasgow Index counts of the night, original ...
    GlasgowCounts glasgowAdapted;       //!< ... and adapted
    int pbSeconds = 0;                  //!< in periodic breathing spans

    HypopneaRule rule = HypopneaRule::Auto;
    QVector<DayEvent> apneas;           //!< as stage 1 found them
    QVector<DayEvent> hypopneas;        //!< confirmed under the rule and classified
    QVector<DayEvent> reras;
    int hypopneasAasm3 = 0;             //!< confirmed under each rule
    int hypopneasCms4 = 0;
    int hypopneasFlowOnly = 0;
    int unconfirmable = 0;              //!< candidates without SpO2, scored by flow only
    int count(RespEvent type) const;

    bool hasOximetry = false;
    bool hasPulse = false;
    QString oxiScope;                   //!< "night", or "cpap" when limited to CPAP time
    OxiResult oxi;                      //!< of the day's SpO2 and pulse
    double linkedDesatArea = 0;         //!< %·s of desaturations linked to analysis events
    int linkedDesaturations = 0;
    int deviceLinkedDesaturations = 0;  //!< linked to the device's apneas and hypopneas
    QVector<int> unexplained;           //!< indices into oxi.desaturations: during scoreable
                                        //!< CPAP time, linked to no analysis event
    double dhrSum = 0;                  //!< pulse response to events (spec §3.4.5) ...
    int dhrEvents = 0;                  //!< ... over this many events
    int eventsWithPulseRise = 0;        //!< events followed by a pulse rise

    bool hasComparison = false;
    QVector<DayEvent> deviceEvents;     //!< compared: during scoreable analysed flow
    QVector<DayEvent> analysisEvents;   //!< apneas, hypopneas and RERAs, in time order
    MatchResult match;                  //!< indices into deviceEvents and analysisEvents
    int deviceCount(EventGroup group) const;
    bool hasApneaOffset = false;        //!< median offset of matched apneas' device stamps ...
    qint64 apneaOffsetToEndMs = 0;      //!< ... from the analysis' event end
    qint64 apneaOffsetToStartMs = 0;    //!< ... from the analysis' event start

    bool hasOffsetHint = false;         //!< oximeter clock looks off (spec §3.4.7)
    qint64 offsetHintMs = 0;            //!< shift to add to the oximeter's times
};

//! Scores a day (spec §3.4). Pure: all times in \a input are device-corrected.
DayResult scoreDay(const DayInput &input, const AnalysisParams &params);

} // namespace analysis

#endif // ANALYSIS_DAY_SCORER_H
