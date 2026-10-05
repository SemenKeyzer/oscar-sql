/* Sleep Analysis Channels
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysis_channels.h"

#include <QColor>
#include <QObject>

#include "SleepLib/common.h"
#include "SleepLib/schema.h"

ChannelID AN_ObstructiveApnea, AN_CentralApnea, AN_Apnea, AN_FlowReduction, AN_RERA,
          AN_FlowLimitation, AN_PeriodicBreathing, AN_Unscoreable, AN_FLScore, AN_GlasgowIndex, AN_GlasgowAdapted,
          AN_ObstructiveHypopnea, AN_CentralHypopnea, AN_Hypopnea,
          AN_Desaturation, AN_CyclicDesaturation, AN_PulseRise, AN_Bradycardia, AN_Tachycardia,
          AN_OxiProblemZone, AN_Stamp;

namespace analysis {

namespace {

using schema::Channel;

// Adds one computed channel, hidden from the Overview (whose generic charts divide by
// device hours, not by the analysed time).
Channel *add(ChannelID &id, ChannelID value, schema::ChanType type, MachineType mt, const char *code,
             const QString &name, const QString &description, const QString &label, const QString &unit,
             const QColor &color)
{
    id = value;
    Channel *ch = new Channel(id, type, mt, schema::SESSION, code, name, description, label, unit, schema::DEFAULT, color);
    ch->setComputed(true);
    ch->setShowInOverview(false);
    schema::channel.add(GRP_ANALYSIS, ch);
    return ch;
}

} // namespace

void registerChannels()
{
    using namespace schema;
    const QString perHour = STR_UNIT_EventsPerHour;

    // stage 1: flow
    add(AN_ObstructiveApnea, 0x1A00, FLAG, MT_CPAP, "AnObstructiveApnea",
        QObject::tr("Obstructive Apnea (analysis)"), QObject::tr("An apnea OSCAR's own analysis judges obstructive (experimental)"),
        QObject::tr("aOA"), perHour, QColor(0x20, 0x70, 0xa0));
    add(AN_CentralApnea, 0x1A01, FLAG, MT_CPAP, "AnCentralApnea",
        QObject::tr("Central Apnea (analysis)"), QObject::tr("An apnea OSCAR's own analysis judges central, with an open airway (experimental)"),
        QObject::tr("aCA"), perHour, QColor(0x90, 0x40, 0xb0));
    add(AN_Apnea, 0x1A02, FLAG, MT_CPAP, "AnApnea",
        QObject::tr("Apnea (analysis)"), QObject::tr("An apnea found by OSCAR's own analysis that it could not classify"),
        QObject::tr("aA"), perHour, QColor(0x20, 0x70, 0x20));
    add(AN_FlowReduction, 0x1A03, MINOR_FLAG, MT_CPAP, "AnFlowReduction",
        QObject::tr("Flow Reduction (analysis)"), QObject::tr("A reduction of airflow of at least 30 % for 10 s or more: a hypopnea candidate"),
        QObject::tr("aFR"), perHour, QColor(0x80, 0x80, 0xc0))->setEnabled(false);
    add(AN_RERA, 0x1A04, FLAG, MT_CPAP, "AnRERA",
        QObject::tr("RERA (analysis)"), QObject::tr("Flow-limited or shrinking breaths ended by a large breath, as found by OSCAR's own analysis"),
        QObject::tr("aRE"), perHour, QColor(0xc0, 0xa0, 0x00));
    add(AN_FlowLimitation, 0x1A05, SPAN, MT_CPAP, "AnFlowLimitation",
        QObject::tr("Flow Limitation (analysis)"), QObject::tr("A run of breaths with a flattened or otherwise limited inspiration"),
        QObject::tr("aFL"), STR_UNIT_Percentage, QColor(0x70, 0x70, 0x70));
    add(AN_PeriodicBreathing, 0x1A06, SPAN, MT_CPAP, "AnPeriodicBreathing",
        QObject::tr("Periodic Breathing (analysis)"), QObject::tr("Breathing that waxes and wanes regularly, as found by OSCAR's own analysis"),
        QObject::tr("aPB"), STR_UNIT_Percentage, QColor(0x80, 0xc0, 0x80));
    add(AN_Unscoreable, 0x1A07, SPAN, MT_CPAP, "AnUnscoreable",
        QObject::tr("Unscoreable (analysis)"), QObject::tr("Time OSCAR's own analysis could not score: gaps, leaks, a weak signal"),
        QObject::tr("aUS"), STR_UNIT_Percentage, QColor(0xc8, 0xc8, 0xc8))->setEnabled(false);
    add(AN_FLScore, 0x1A08, WAVEFORM, MT_CPAP, "AnFLScore",
        QObject::tr("Flow Limitation (analysis)"), QObject::tr("How limited each inspiration looks, 0 (normal) to 1 (clearly limited)"),
        QObject::tr("FL score"), QString(), QColor(0x50, 0x50, 0x50));
    add(AN_GlasgowIndex, 0x1A09, WAVEFORM, MT_CPAP, "AnGlasgowIndex",
        QObject::tr("Glasgow Index (analysis)"),
        QObject::tr("Glasgow Index over the last 5 minutes: the share of breaths with each of 8 shape signs, summed (DaveSkvn's method)"),
        QObject::tr("GI"), QString(), QColor(0xb0, 0x30, 0x60));
    add(AN_GlasgowAdapted, 0x1A0A, WAVEFORM, MT_CPAP, "AnGlasgowAdapted",
        QObject::tr("Glasgow Index, adapted (analysis)"),
        QObject::tr("Glasgow Index over the last 5 minutes, with thresholds relative to the breath's size"),
        QObject::tr("GIa"), QString(), QColor(0x30, 0x60, 0xb0));

    // stage 2: hypopneas
    add(AN_ObstructiveHypopnea, 0x1A10, FLAG, MT_CPAP, "AnObstructiveHypopnea",
        QObject::tr("Obstructive Hypopnea (analysis)"), QObject::tr("A hypopnea with flow-limited breaths, as found by OSCAR's own analysis"),
        QObject::tr("aOH"), perHour, QColor(0x20, 0x60, 0xc0));
    add(AN_CentralHypopnea, 0x1A11, FLAG, MT_CPAP, "AnCentralHypopnea",
        QObject::tr("Central Hypopnea (analysis)"), QObject::tr("A hypopnea without flow limitation during periodic breathing, as found by OSCAR's own analysis"),
        QObject::tr("aCH"), perHour, QColor(0xb0, 0x50, 0xb0));
    add(AN_Hypopnea, 0x1A12, FLAG, MT_CPAP, "AnHypopnea",
        QObject::tr("Hypopnea (analysis)"), QObject::tr("A hypopnea found by OSCAR's own analysis under the chosen hypopnea rule"),
        QObject::tr("aH"), perHour, QColor(0x30, 0x30, 0xd0));

    // stage 1: oximetry
    add(AN_Desaturation, 0x1A20, FLAG, MT_OXIMETER, "AnDesaturation",
        QObject::tr("Desaturation (analysis)"), QObject::tr("A fall of SpO2 by 3 % or more from its recent peak"),
        QObject::tr("aDS"), perHour, QColor(0x20, 0x90, 0xd0));
    add(AN_CyclicDesaturation, 0x1A21, SPAN, MT_OXIMETER, "AnCyclicDesaturation",
        QObject::tr("Cyclic Desaturation (analysis)"), QObject::tr("Three or more desaturations in a row, 20-120 s apart"),
        QObject::tr("aCD"), STR_UNIT_Percentage, QColor(0x90, 0xc0, 0xe0));
    add(AN_PulseRise, 0x1A22, FLAG, MT_OXIMETER, "AnPulseRise",
        QObject::tr("Pulse Rise (analysis)"), QObject::tr("A sudden rise of the pulse rate over its recent level, often with an arousal"),
        QObject::tr("aPR"), perHour, QColor(0xe0, 0x60, 0x60));
    add(AN_Bradycardia, 0x1A23, SPAN, MT_OXIMETER, "AnBradycardia",
        QObject::tr("Low Pulse (analysis)"), QObject::tr("Pulse rate below the low threshold for 30 s or more"),
        QObject::tr("aLP"), STR_UNIT_Percentage, QColor(0xb0, 0x80, 0x80));
    add(AN_Tachycardia, 0x1A24, SPAN, MT_OXIMETER, "AnTachycardia",
        QObject::tr("High Pulse (analysis)"), QObject::tr("Pulse rate above the high threshold for 30 s or more"),
        QObject::tr("aHP"), STR_UNIT_Percentage, QColor(0xe0, 0x90, 0x60));
    add(AN_OxiProblemZone, 0x1A25, SPAN, MT_OXIMETER, "AnOxiProblemZone",
        QObject::tr("Oximetry Problem Zone (analysis)"), QObject::tr("A stretch of the night with repeated desaturations or low SpO2 worth a look"),
        QObject::tr("aPZ"), STR_UNIT_Percentage, QColor(0xf0, 0xb0, 0x60));

    // stage 1 bookkeeping: which parameters and algorithm version produced the session's channels
    add(AN_Stamp, 0x1A30, SETTING, MT_UNKNOWN, "AnStamp",
        QObject::tr("Analysis Stamp"), QObject::tr("Version and parameters of OSCAR's own analysis of this session"),
        QObject::tr("Analysis Stamp"), QString(), Qt::black);
}

QList<ChannelID> flowChannels()
{
    return { AN_ObstructiveApnea, AN_CentralApnea, AN_Apnea, AN_FlowReduction, AN_RERA,
             AN_FlowLimitation, AN_PeriodicBreathing, AN_Unscoreable, AN_FLScore,
             AN_GlasgowIndex, AN_GlasgowAdapted };
}

QList<ChannelID> hypopneaChannels()
{
    return { AN_ObstructiveHypopnea, AN_CentralHypopnea, AN_Hypopnea };
}

QList<ChannelID> oximetryChannels()
{
    return { AN_Desaturation, AN_CyclicDesaturation, AN_PulseRise, AN_Bradycardia, AN_Tachycardia,
             AN_OxiProblemZone };
}

bool isAnalysisChannel(ChannelID code)
{
    return code >= 0x1A00 && code <= 0x1A3F;
}

} // namespace analysis
