/* Sleep Analysis Channels Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ANALYSIS_CHANNELS_H
#define ANALYSIS_CHANNELS_H

#include <QList>
#include <QString>

#include "SleepLib/machine_common.h"

//! Channels written by OSCAR's own analysis (range 0x1A00-0x1A3F). They are computed,
//! never reported by a device: see schema::Channel::isComputed().
extern ChannelID AN_ObstructiveApnea, AN_CentralApnea, AN_Apnea, AN_FlowReduction, AN_RERA,
       AN_FlowLimitation, AN_PeriodicBreathing, AN_Unscoreable, AN_FLScore,
       AN_ObstructiveHypopnea, AN_CentralHypopnea, AN_Hypopnea,
       AN_Desaturation, AN_CyclicDesaturation, AN_PulseRise, AN_Bradycardia, AN_Tachycardia,
       AN_OxiProblemZone, AN_Stamp;

const QString GRP_ANALYSIS = "ANALYSIS";

namespace analysis {

//! Adds the analysis channels to schema::channel; called by schema::init().
void registerChannels();

//! Stage 1 flow channels of a CPAP session (events, spans and the per-breath score).
QList<ChannelID> flowChannels();
//! Stage 2 hypopnea channels of a CPAP session.
QList<ChannelID> hypopneaChannels();
//! Stage 1 oximetry channels, in the session that holds the SpO2.
QList<ChannelID> oximetryChannels();
//! True for any channel the analysis writes.
bool isAnalysisChannel(ChannelID code);

} // namespace analysis

#endif // ANALYSIS_CHANNELS_H
