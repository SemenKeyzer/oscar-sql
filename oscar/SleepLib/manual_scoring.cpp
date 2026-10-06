/* Manual scoring of respiratory events
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "SleepLib/manual_scoring.h"

#include <algorithm>

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/manual_scoring_repository.h"

namespace ManualScoring {

QList<ChannelID> scoredChannels()
{
    // built on each call: the channel ids are set by schema::init()
    return { CPAP_Obstructive, CPAP_ClearAirway, CPAP_Apnea, CPAP_Hypopnea };
}

namespace {

//! The union of \a spans clipped to \a sessions, sorted and without overlaps.
QList<QPair<qint64, qint64>> clippedUnion(const QList<QPair<qint64, qint64>> &spans, const QList<QPair<qint64, qint64>> &sessions)
{
    QList<QPair<qint64, qint64>> parts;
    for (const auto &s : spans) {
        for (const auto &sess : sessions) {
            const qint64 a = qMax(s.first, sess.first), b = qMin(s.second, sess.second);
            if (b > a) parts.append({ a, b });
        }
    }
    std::sort(parts.begin(), parts.end());
    QList<QPair<qint64, qint64>> merged;
    for (const auto &p : parts) {
        if (!merged.isEmpty() && p.first <= merged.last().second) merged.last().second = qMax(merged.last().second, p.second);
        else merged.append(p);
    }
    return merged;
}

} // namespace

Result apply(const QList<DeviceEvent> &device, const QList<Edit> &edits, const QList<QPair<qint64, qint64>> &sessionSpans)
{
    const QList<ChannelID> scored = scoredChannels();
    Result r;
    QHash<ChannelID, int> deviceCount;
    for (const DeviceEvent &d : device) {
        if (!scored.contains(d.channel)) continue;
        r.events.append({ d.channel, d.channel, d.endMs, d.durationSec, Origin::Device, 0, false });
        ++deviceCount[d.channel];
    }
    const int deviceEnd = r.events.size();   // the device events come first

    QList<Edit> ordered = edits;
    std::sort(ordered.begin(), ordered.end(), [](const Edit &a, const Edit &b) { return a.id < b.id; });
    QList<QPair<qint64, qint64>> excludes;
    for (const Edit &e : ordered) {
        switch (e.kind) {
        case Kind::Add:
            r.events.append({ e.channel, e.channel, e.endMs, (e.endMs - e.startMs) / 1000.0, Origin::Added, e.id, false });
            break;
        case Kind::Exclude:
            excludes.append({ e.startMs, e.endMs });
            break;
        case Kind::Remove:
        case Kind::Retype: {
            // the nearest device event of that type; a later edit of the same event wins
            int best = -1;
            qint64 bestGap = kMatchToleranceMs + 1;
            for (int i = 0; i < deviceEnd; ++i) {
                const EffectiveEvent &ev = r.events.at(i);
                const qint64 gap = qAbs(ev.endMs - e.endMs);
                if (ev.originalChannel == e.channel && gap < bestGap) {
                    best = i;
                    bestGap = gap;
                }
            }
            if (best < 0) {
                r.notFound.append(e.id);
                break;
            }
            EffectiveEvent &ev = r.events[best];
            ev.editId = e.id;
            if (e.kind == Kind::Remove) {
                ev.origin = Origin::Removed;
                ev.channel = ev.originalChannel;
            } else {
                ev.origin = Origin::Retyped;
                ev.channel = e.newChannel;
            }
            break;
        }
        }
    }

    const QList<QPair<qint64, qint64>> excluded = clippedUnion(excludes, sessionSpans);
    for (const auto &span : excluded) r.excludedMs += span.second - span.first;
    QHash<ChannelID, int> counted;
    for (EffectiveEvent &ev : r.events) {
        for (const auto &span : excluded) {
            if (ev.endMs >= span.first && ev.endMs <= span.second) ev.excluded = true;
        }
        if (ev.origin != Origin::Removed && !ev.excluded) ++counted[ev.channel];
    }
    for (ChannelID c : scored) {
        const int d = counted.value(c) - deviceCount.value(c);
        if (d != 0) r.delta.insert(c, d);
    }
    return r;
}

// ---- on real sessions and days

SessionKey keyOf(Session *s)
{
    SessionKey k;
    Machine *m = s ? s->machine() : nullptr;
    if (m) k.profileId = m->getProfileId();
    if (m) k.serial = m->serial();
    if (s) k.session = s->session();
    return k;
}

void loadSummary(Session *s)
{
    QHash<ChannelID, int> delta;
    qint64 excludedMs = 0;
    int notFound = 0;
    const bool has = ManualScoringRepository::loadSummary(keyOf(s), delta, excludedMs, notFound);
    s->setManualScoring(delta, excludedMs, notFound, has);
}

Result resultFor(Session *s)
{
    const QList<Edit> edits = ManualScoringRepository::editsForSession(keyOf(s));
    // the device events are needed; load them for the call if they are not in memory
    const bool opened = s->eventlist.isEmpty() && s->OpenEvents();
    QList<DeviceEvent> device;
    for (ChannelID code : scoredChannels()) {
        for (EventList *el : s->eventlist.value(code)) {
            for (quint32 i = 0; i < el->count(); ++i) device.append({ code, el->time(i), double(el->data(i)) });
        }
    }
    if (opened) s->TrashEvents();
    return apply(device, edits, { { s->first(), s->last() } });
}

void refresh(Session *s)
{
    const SessionKey key = keyOf(s);
    if (ManualScoringRepository::editsForSession(key).isEmpty()) {
        ManualScoringRepository::removeSummary(key);
        s->setManualScoring({}, 0, 0, false);
        return;
    }
    const Result r = resultFor(s);
    ManualScoringRepository::storeSummary(key, r);
    s->setManualScoring(r.delta, r.excludedMs, int(r.notFound.size()), true);
}

bool addEdit(Session *s, Edit edit)
{
    edit.key = keyOf(s);
    if (ManualScoringRepository::add(edit) == 0) return false;
    refresh(s);
    return true;
}

bool removeEdit(Session *s, qint64 id)
{
    if (!ManualScoringRepository::remove(id)) return false;
    refresh(s);
    return true;
}

void clearDay(Day *day)
{
    for (Session *s : day->sessions) {
        ManualScoringRepository::removeAllForSession(keyOf(s));
        refresh(s);
    }
}

} // namespace ManualScoring
