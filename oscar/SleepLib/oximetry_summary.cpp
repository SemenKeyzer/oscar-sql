/* Oximetry night summary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oximetry_summary.h"

#include <QSet>
#include <cmath>

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/analysis/oxi_analyzer.h"
#include "SleepLib/analysis/session_analysis.h"
#include "SleepLib/loader_plugins/applehealth_loader.h"
#include "database/analysis_daily_repository.h"

using namespace analysis;

namespace {

// Running figures over the valid seconds of one channel.
struct Tally {
    int seconds = 0;
    double sum = 0;
    float min = 0, max = 0;
    int below90 = 0;

    void add(const Grid &g)
    {
        for (float v : g.v) {
            if (!hasData(v)) continue;
            if (seconds == 0 || v < min) min = v;
            if (seconds == 0 || v > max) max = v;
            ++seconds;
            sum += v;
            below90 += std::lround(v) < 90;   // whole-% readings below 90, as the analysis counts them
        }
    }
};

} // namespace

OximetryNight summarizeOximetry(Day *day, MachineType source)
{
    OximetryNight n;
    if (!day) return n;
    Machine *mach = day->machine(source);
    if (!mach || day->hours(source) <= 0) return n;
    n.device = (mach->brand() + QLatin1Char(' ') + mach->model()).trimmed();

    if (mach->loaderName() == applehealth_class_name) {
        // Spot checks: desaturation detection doesn't apply (as in the Daily view).
        n.spotChecks = true;
        n.hours = day->hours(source);
        for (Session *sess : day->sessions) {
            if (!sess->enabled() || sess->machine() != mach) continue;
            n.spotSpo2 += int(sess->count(OXI_SPO2));
            n.spotPulse += int(sess->count(OXI_Pulse));
        }
        n.valid = n.spotSpo2 > 0 || n.spotPulse > 0;
        return n;
    }

    // The figures come from the cleaned 1 Hz samples, so that a night with the probe
    // off for a while is not diluted by the time without readings. Only SpO2 and pulse
    // are loaded: a CPAP's waveforms are not needed here.
    Tally spo2, pulse;
    for (Session *sess : day->sessions) {
        if (!sess->enabled() || sess->machine() != mach) continue;
        n.desaturations += int(sess->count(OXI_SPO2Drop));
        // Something in memory (loaded, partly loaded, or built by an import) stays there.
        const bool inMemory = sess->eventsLoaded() || !sess->eventlist.isEmpty();
        if (!sess->LoadEventsFromDatabase(QSet<ChannelID>{ OXI_SPO2, OXI_Pulse })) continue;
        spo2.add(cleanSpo2(sessionGrid(sess, OXI_SPO2, 50, 100)));
        pulse.add(cleanPulse(sessionGrid(sess, OXI_Pulse, 30, 220)));
        if (!inMemory) sess->TrashEvents();
    }
    if (spo2.seconds > 0) {
        n.hours = spo2.seconds / 3600.0;
        n.spo2Avg = spo2.sum / spo2.seconds;
        n.spo2Min = spo2.min;
        n.minutesBelow90 = spo2.below90 / 60.0;
        n.percentBelow90 = 100.0 * spo2.below90 / spo2.seconds;
    } else {
        n.hours = pulse.seconds / 3600.0;
        n.desaturations = 0;
    }
    if (pulse.seconds > 0) {
        n.pulseAvg = pulse.sum / pulse.seconds;
        n.pulseMin = pulse.min;
        n.pulseMax = pulse.max;
    }
    if (p_profile) {
        n.dropPercent = p_profile->oxi->spO2DropPercentage();
        n.dropSeconds = p_profile->oxi->spO2DropDuration();
    }
    n.valid = n.hours > 0;
    return n;
}

OximetryNight summarizeOximetry(const AnalysisDailyData &row)
{
    OximetryNight n;
    if (row.id == 0 || !row.hasOximetry || row.oxiSeconds <= 0) return n;
    n.valid = true;
    n.fromAnalysis = true;
    n.device = row.oxiSource;
    n.hours = row.oxiSeconds / 3600.0;
    n.spo2Avg = row.spo2Sum / row.oxiSeconds;
    n.spo2Min = row.spo2Nadir;
    int below = 0;
    for (int i = 0; i < row.spo2Hist.size(); ++i) {
        if (kSpo2HistMin + i < 90) below += row.spo2Hist[i];
    }
    n.minutesBelow90 = below / 60.0;
    n.percentBelow90 = 100.0 * below / row.oxiSeconds;
    n.desaturations = row.nDesat3;
    n.desaturations4 = row.nDesat4;
    n.zones = row.nZones;
    n.zoneMinutes = row.zoneSeconds / 60.0;
    if (row.hasPulse && row.pulseSeconds > 0) {
        n.pulseAvg = row.pulseSum / row.pulseSeconds;
        n.pulseMin = row.pulseMin;
        n.pulseMax = row.pulseMax;
    }
    return n;
}
