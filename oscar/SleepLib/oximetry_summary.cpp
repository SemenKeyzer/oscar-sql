/* Oximetry night summary
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oximetry_summary.h"

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/applehealth_loader.h"

OximetryNight summarizeOximetry(Day *day, MachineType source)
{
    OximetryNight n;
    if (!day) return n;
    Machine *mach = day->machine(source);
    n.hours = day->hours(source);
    if (!mach || n.hours <= 0) return n;
    n.device = (mach->brand() + QLatin1Char(' ') + mach->model()).trimmed();

    if (mach->loaderName() == applehealth_class_name) {
        // Spot checks: desaturation detection doesn't apply (as in the Daily view).
        n.spotChecks = true;
        for (Session *sess : day->sessions) {
            if (!sess->enabled() || sess->machine() != mach) continue;
            n.spotSpo2 += int(sess->count(OXI_SPO2));
            n.spotPulse += int(sess->count(OXI_Pulse));
        }
        n.valid = n.spotSpo2 > 0 || n.spotPulse > 0;
        return n;
    }

    n.spo2Avg = day->wavg(OXI_SPO2);
    n.spo2Min = day->Min(OXI_SPO2);
    n.pulseAvg = day->wavg(OXI_Pulse);
    n.pulseMin = day->Min(OXI_Pulse);
    n.pulseMax = day->Max(OXI_Pulse);
    n.desaturations = int(day->count(OXI_SPO2Drop));

    // Time below 90 % is counted from the samples, which need to be in memory.
    for (Session *sess : day->sessions) {
        if (!sess->enabled() || sess->type() != source) continue;
        const bool loaded = sess->eventsLoaded();
        if (!loaded && !sess->OpenEvents()) continue;
        n.minutesBelow90 += sess->timeBelowThreshold(OXI_SPO2, 89.5);   // whole-% readings below 90
        if (!loaded) sess->TrashEvents();
    }
    n.percentBelow90 = 100.0 * n.minutesBelow90 / (n.hours * 60.0);
    n.valid = n.spo2Avg > 0 || n.pulseAvg > 0;
    return n;
}
