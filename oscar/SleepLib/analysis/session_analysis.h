/* Sleep Analysis of One Session Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_SESSION_ANALYSIS_H
#define ANALYSIS_SESSION_ANALYSIS_H

#include <QList>
#include <QString>
#include <QVector>

#include "analysis_params.h"
#include "oxi_analyzer.h"
#include "signal_utils.h"
#include "SleepLib/machine_common.h"

class EventList;
class Session;

namespace analysis {

//! The parameters the analysis runs with now. Loaders may run on worker threads, so the
//! analysis reads this copy instead of the profile; the main thread keeps it current.
AnalysisParams activeParams();
void setActiveParams(const AnalysisParams &params);

//! What stage 1 last stored in a session (the AN_Stamp setting): per part, the
//! algorithm version and the hash of the parameters it ran with, and the flow totals
//! that day scoring needs without loading the waveform.
struct SessionStamp {
    int flowVersion = 0;
    QString flowHash;
    int oxiVersion = 0;
    QString oxiHash;

    bool flowAnalyzed = false;      //!< false: no flow, or its sample rate is too low
    double flowRateHz = 0;          //!< of the recording; 0 when there is no flow
    bool flScored = false;          //!< flow limitation scored (sample rate >= 10 Hz)
    int flowSeconds = 0;            //!< scoreable flow time
    int unscoreableSeconds = 0;
    double flSum = 0;               //!< sum of the scores of flBreaths breaths
    int flBreaths = 0;

    static SessionStamp read(Session *session);
    void write(Session *session) const;
    QString toJson() const;
};

//! The stage 1 parts of a session that are missing or outdated for \a params.
struct StageOneNeed {
    bool flow = false;
    bool oxi = false;
    bool any() const { return flow || oxi; }
};
StageOneNeed stageOneNeeded(Session *session, const AnalysisParams &params);

//! Stage 1 on a session whose events are in memory: replaces its flow and/or oximetry
//! analysis channels and its stamp, and refreshes those channels' summaries. With
//! \a onlyIfOutdated, parts whose stamp matches \a params are left alone. Returns the
//! channels it replaced (to store with Session::StoreChannelEvents()). Does nothing when
//! the analysis is switched off, or the session's events are not (all) in memory.
//! Never touches the device's channels.
QList<ChannelID> analyzeSession(Session *session, const AnalysisParams &params, bool onlyIfOutdated = false);

//! A session's SpO2 or pulse lists on a 1 Hz grid (raw device time).
Grid sessionGrid(Session *session, ChannelID code, float minValid, float maxValid);

//! The samples of each of a session's lists of \a code, their times moved by \a shiftMs.
QVector<TimedSamples> sessionSamples(Session *session, ChannelID code, qint64 shiftMs = 0);

//! A channel's span events (time = end, data = duration in s) as spans moved by
//! \a shiftMs; value is the event's data2 (0 without one).
QVector<Span> sessionSpans(Session *session, ChannelID code, qint64 shiftMs = 0);

//! Adds a span event in OSCAR's convention: time = end, data = duration in s, and
//! \a value2 as data2 when the list has one.
void addSpanEvent(EventList *list, qint64 start, qint64 end, double value2 = 0);

} // namespace analysis

#endif // ANALYSIS_SESSION_ANALYSIS_H
