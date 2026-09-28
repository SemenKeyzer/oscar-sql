/* Sleep Analysis Flow Analyzer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_FLOW_ANALYZER_H
#define ANALYSIS_FLOW_ANALYZER_H

#include <QVector>

#include "analysis_params.h"
#include "oxi_analyzer.h"      // Span
#include "signal_utils.h"

namespace analysis {

//! One continuous piece of flow waveform (one EventList): samples at a fixed rate.
struct FlowChunk {
    qint64 start = 0;      //!< time of the first sample, ms
    double rateMs = 0;     //!< sample interval, ms
    QVector<float> samples;
};

//! One breath, from the start of inspiration to the start of the next one (or, before a
//! pause in breathing, to the end of its expiration).
struct Breath {
    qint64 start = 0;
    qint64 inspEnd = 0;
    qint64 end = 0;
    float pif = 0;         //!< peak inspiratory flow
    float pef = 0;         //!< peak expiratory flow (negative)
    float vi = 0;          //!< inspiratory volume, flow units · s
    float amplitude = 0;   //!< pif - pef
    float fl = kNoData;    //!< flow limitation score 0-1 (NaN: not scored)
};

enum class ApneaClass { Unclassified = 0, Obstructive = 1, Central = 2 };

//! An apnea, or a flow reduction that may become a hypopnea in the day scoring.
struct FlowEvent {
    qint64 start = 0;
    qint64 end = 0;
    bool apnea = false;
    float reduction = 0;   //!< fraction of the baseline lost, 0-1
    float baseline = 0;    //!< envelope baseline at the start (flow units, peak to peak)
    ApneaClass cls = ApneaClass::Unclassified;
    float classScore = 0;  //!< evidence sum of the classifier (> 0 obstructive)
};

//! Everything the flow analysis of one session finds.
struct FlowResult {
    bool analyzed = false;         //!< false: no flow or sample rate below 4 Hz
    double sampleRateHz = 0;       //!< of the recording (before any decimation)
    bool flScored = false;         //!< flow limitation needs >= 10 Hz

    QVector<Breath> breaths;
    QVector<FlowEvent> events;     //!< apneas and hypopnea candidates, in time order
    QVector<Span> flowLimitation;  //!< runs of flow-limited breaths
    QVector<Span> reras;           //!< flow-based RERA-like episodes
    QVector<Span> periodic;        //!< periodic breathing (value: period, s)
    QVector<Span> unscoreable;     //!< gaps, excluded spans, weak signal, long reductions

    Grid envelope;                 //!< 1 Hz peak-to-peak flow (for tests and the classifier)
    Grid baseline;                 //!< 1 Hz envelope baseline

    int flowSeconds = 0;           //!< analysable time: flow present and scoreable
    int unscoreableSeconds = 0;
    double flSum = 0;              //!< sum of the flow limitation scores ...
    int flBreaths = 0;             //!< ... of this many scored breaths outside events

    int count(bool apnea) const;
    int flowLimitationSeconds() const;
    int periodicSeconds() const;
};

//! Flow limitation score 0-1 of one inspiration from its shape (spec §3.3.5): a flat top,
//! a dip in the middle ("M") or an early peak with a plateau ("chair"). \a insp are the
//! inspiratory flow samples (positive). Exposed for tests.
float flowLimitationScore(const QVector<float> &insp);

//! Analyses the flow waveform of one CPAP session. \a excluded are spans the device marks
//! as unusable (large leak, artifacts); \a pulse (may be null) helps the apnea
//! classifier; \a obstructLevel (may be null) is Prisma's obstruction level (0-100 %).
FlowResult analyzeFlow(const QVector<FlowChunk> &chunks, const QVector<Span> &excluded,
                       const Grid *pulse, const Grid *obstructLevel, const FlowParams &params);

} // namespace analysis

#endif // ANALYSIS_FLOW_ANALYZER_H
