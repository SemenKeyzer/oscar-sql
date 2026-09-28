/* Sleep Analysis Apnea Classifier Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_APNEA_CLASSIFIER_H
#define ANALYSIS_APNEA_CLASSIFIER_H

#include <QVector>

#include "flow_analyzer.h"

namespace analysis {

//! What the classifier looks at for one apnea (spec §3.3.6).
struct ApneaEvidence {
    //! flow during the apnea (offset removed, not smoothed) and its sample rate
    QVector<float> flow;
    double fs = 0;
    float baseline = 0;              //!< envelope baseline at the event (peak to peak)
    QVector<Breath> before;          //!< up to 3 breaths just before, oldest first
    const Breath *after = nullptr;   //!< the first breath after, if any
    float pulseBpm = kNoData;        //!< mean pulse during the apnea, if measured
    bool inPeriodicBreathing = false;
    float obstructLevel = kNoData;   //!< Prisma's mean obstruction level, 0-100 %
};

//! Share (0-1) of the 4 s windows of \a flow (skipping 2 s at each end) with cardiogenic
//! oscillations: a spectral peak at 0.7-2.5 Hz (or at the pulse rate +- 0.15 Hz) at least
//! 4x the mean power at 3-6 Hz and at least 2 % of \a baseline peak to peak. -1 when
//! the recording is too slow (< 10 Hz) or the apnea too short to tell.
float cardiogenicOscillationShare(const QVector<float> &flow, double fs, float baseline, float pulseBpm);

//! Evidence sum: > 0 points to an obstructed airway, < 0 to an open one.
float apneaScore(const ApneaEvidence &evidence, double flThreshold);

//! The class for an evidence sum: >= 0.75 obstructive, <= -0.75 central.
ApneaClass classForScore(float score);

} // namespace analysis

#endif // ANALYSIS_APNEA_CLASSIFIER_H
