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
#include "SleepLib/machine_common.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/manual_scoring_repository.h"
#include "database/daily_summary_repository.h"
#include "database/database_manager.h"
#include <QSqlDatabase>

namespace ManualScoring {

QList<ChannelID> scoredChannels()
{
    // built on each call: the channel ids are set by schema::init()
    return { CPAP_Obstructive, CPAP_ClearAirway, CPAP_Apnea, CPAP_Hypopnea };
}

QList<ChannelID> countedChannels()
{
    QList<ChannelID> out = scoredChannels();
    for (ChannelID c : *ahiChannelGroup(AllAhiChannels)) {
        if (!out.contains(c)) out.append(c);
    }
    out.append(CPAP_RERA);
    return out;
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
    // RERA and the hypopneas some devices split by mechanism (OH/CH) cannot be edited, but an
    // excluded stretch leaves them out too: they count in the AHI or RDI
    const QList<ChannelID> scored = countedChannels();
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
    r.excludedSpans = excluded;
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

BulkPlan bulkEdits(const Result &r, ChannelID from, ChannelID to)
{
    BulkPlan plan;
    for (const EffectiveEvent &e : r.events) {
        if (e.channel != from || e.origin == Origin::Removed || e.excluded) continue;
        ++plan.count;
        const qint64 start = e.endMs - qint64(e.durationSec * 1000);
        if (e.origin == Origin::Added) {
            // an event added by hand is taken back, and added again as the new type
            plan.undo.append(e.editId);
            if (to != 0) {
                Edit add;
                add.kind = Kind::Add;
                add.channel = to;
                add.startMs = start;
                add.endMs = e.endMs;
                plan.add.append(add);
            }
            continue;
        }
        if (e.origin == Origin::Retyped && to == e.originalChannel) {
            plan.undo.append(e.editId);   // back to what the device said: take the retype back
            continue;
        }
        Edit change;   // a device event, as recorded or retyped before: named by its own type
        change.kind = to == 0 ? Kind::Remove : Kind::Retype;
        change.channel = e.originalChannel;
        change.newChannel = to;
        change.startMs = start;
        change.endMs = e.endMs;
        plan.add.append(change);
    }
    return plan;
}

int stepEvent(const QList<EffectiveEvent> &events, ChannelID type, qint64 fromMs, int fromIndex, bool forward)
{
    const QList<ChannelID> scored = scoredChannels();
    const QPair<qint64, int> from(fromMs, fromIndex);
    int best = -1;
    for (int i = 0; i < events.size(); ++i) {
        const EffectiveEvent &e = events.at(i);
        if (type == 0 ? !scored.contains(e.channel) : e.channel != type) continue;
        const QPair<qint64, int> at(e.endMs, i);
        if (forward ? !(from < at) : !(at < from)) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        const QPair<qint64, int> b(events.at(best).endMs, best);
        if (forward ? at < b : b < at) best = i;
    }
    return best;
}

int countOf(const QList<EffectiveEvent> &events, ChannelID type)
{
    const QList<ChannelID> scored = scoredChannels();
    int n = 0;
    for (const EffectiveEvent &e : events) {
        if (e.origin == Origin::Removed || e.excluded) continue;
        if (type == 0 ? scored.contains(e.channel) : e.channel == type) ++n;
    }
    return n;
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
    const SessionKey key = keyOf(s);
    // most sessions have none: no query for them
    const bool has = ManualScoringRepository::hasScoring(key) && ManualScoringRepository::loadSummary(key, delta, excludedMs, notFound);
    s->setManualScoring(delta, excludedMs, notFound, has);
}

Result resultFor(Session *s)
{
    const QList<Edit> edits = ManualScoringRepository::editsForSession(keyOf(s));
    const QList<ChannelID> channels = countedChannels();
    // the device events are needed: load the scored channels if they are not all in memory
    // (nothing loaded, or the analysis holds only some channels)
    const bool wasEmpty = s->eventlist.isEmpty() && !s->eventsLoaded();
    if (wasEmpty || s->partialEvents()) s->LoadEventsFromDatabase(QSet<ChannelID>(channels.cbegin(), channels.cend()));
    QList<DeviceEvent> device;
    for (ChannelID code : channels) {
        for (EventList *el : s->eventlist.value(code)) {
            for (quint32 i = 0; i < el->count(); ++i) device.append({ code, el->time(i), double(el->data(i)) });
        }
    }
    if (wasEmpty) s->TrashEvents();

    // the time counted: the mask-on slices, or the whole session (device time, like the events)
    QList<QPair<qint64, qint64>> spans;
    for (const SessionSlice &slice : s->m_slices) {
        if (slice.status == MaskOn) spans.append({ slice.start, slice.end });
    }
    if (spans.isEmpty()) spans.append({ s->realFirst(), s->realLast() });
    Result r = apply(device, edits, spans);

    // shown on the graphs with the device's time correction
    const qint64 c = s->correctionMs();
    for (EffectiveEvent &e : r.events) e.endMs += c;
    for (auto &span : r.excludedSpans) {
        span.first += c;
        span.second += c;
    }
    return r;
}

void refresh(Session *s)
{
    const SessionKey key = keyOf(s);
    if (!ManualScoringRepository::hasScoring(key)) {
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
    // made on the graphs, which show the device time plus its correction: stored in device time
    const qint64 c = s->correctionMs();
    edit.startMs -= c;
    edit.endMs -= c;
    if (ManualScoringRepository::add(edit) == 0) return false;
    refresh(s);
    return true;
}

bool addEdits(Session *s, QList<Edit> edits)
{
    const SessionKey key = keyOf(s);
    const qint64 c = s->correctionMs();
    bool ok = true;
    for (Edit &edit : edits) {
        edit.key = key;
        edit.startMs -= c;
        edit.endMs -= c;
        ok = ManualScoringRepository::add(edit) != 0 && ok;
    }
    refresh(s);
    return ok;
}

bool applyBulk(Session *s, const BulkPlan &plan)
{
    QSqlDatabase db = DatabaseManager::instance().database();
    const bool tx = db.transaction();
    const SessionKey key = keyOf(s);
    const qint64 c = s->correctionMs();
    bool ok = true;
    for (qint64 id : plan.undo) ok = ManualScoringRepository::remove(id) && ok;
    for (Edit edit : plan.add) {
        edit.key = key;
        edit.startMs -= c;
        edit.endMs -= c;
        ok = ManualScoringRepository::add(edit) != 0 && ok;
    }
    if (tx) {
        if (ok) ok = db.commit();
        else db.rollback();
    }
    if (!ok) ManualScoringRepository::remove(-1);   // the cache may hold what was rolled back
    refresh(s);
    return ok;
}

bool removeEdit(Session *s, qint64 id)
{
    if (!ManualScoringRepository::remove(id)) return false;
    refresh(s);
    return true;
}

bool undoEdit(Day *day, qint64 id)
{
    // find the edit
    Edit target;
    bool found = false;
    for (Session *s : day->sessions) {
        for (const Edit &e : ManualScoringRepository::editsForSession(keyOf(s))) {
            if (e.id == id) {
                target = e;
                found = true;
            }
        }
    }
    if (!found) return false;
    for (Session *s : day->sessions) {
        bool changed = false;
        for (const Edit &e : ManualScoringRepository::editsForSession(keyOf(s))) {
            const bool same = e.id == id
                              || (target.kind == Kind::Exclude && e.kind == Kind::Exclude && e.startMs == target.startMs
                                  && e.endMs == target.endMs);
            if (same) changed = ManualScoringRepository::remove(e.id) || changed;
        }
        if (changed) refresh(s);
    }
    return true;
}

void storeDaySummary(Day *day)
{
    Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
    if (cpap && cpap->getProfileId() > 0) DailySummaryRepository().calculateAndStoreFromDay(day, cpap->getProfileId());
}

void clearDay(Day *day)
{
    for (Session *s : day->sessions) {
        ManualScoringRepository::removeAllForSession(keyOf(s));
        refresh(s);
    }
}

} // namespace ManualScoring
