/* Sleep Analysis of One Day
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "SleepLib/manual_scoring.h"
#include "day_analysis.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>

#include "analysis_channels.h"
#include "device_event_conventions.h"
#include "session_analysis.h"
#include "database/event_list_repository.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

namespace analysis {

namespace {

// Apple Health spot checks are far too sparse to analyse (as for calcSPO2Drop()).
const QString kAppleHealthLoader = QStringLiteral("AppleHealth");

struct DeviceChannel {
    ChannelID code;
    RespEvent type;
};

// The device's respiratory event channels compared with the analysis (spec §3.4.6).
QVector<DeviceChannel> deviceChannels()
{
    return { { CPAP_Obstructive, RespEvent::ObstructiveApnea },
             { CPAP_ClearAirway, RespEvent::CentralApnea },
             { CPAP_Apnea, RespEvent::Apnea },
             { CPAP_AllApnea, RespEvent::Apnea },
             { CPAP_Hypopnea, RespEvent::Hypopnea },
             { CPAP_ObstructiveHypopnea, RespEvent::ObstructiveHypopnea },
             { CPAP_CentralHypopnea, RespEvent::CentralHypopnea },
             { CPAP_RERA, RespEvent::Rera } };
}

bool isAnalysable(Session *s)
{
    return s && s->machine() && (s->type() == MT_CPAP || s->type() == MT_OXIMETER);
}

// Sessions of the day in a stable order: by device, then by session number.
QList<Session *> sortedSessions(QList<Session *> list)
{
    std::sort(list.begin(), list.end(), [](Session *a, Session *b) {
        const qint64 ma = a->machine()->getDatabaseId(), mb = b->machine()->getDatabaseId();
        return ma != mb ? ma < mb : a->session() < b->session();
    });
    return list;
}

// Events someone else loaded (the Daily view, an import) stay; the ones this module
// loads are put away again.
bool eventsInMemory(Session *s)
{
    return s->eventsLoaded() || s->partialEvents() || !s->eventlist.isEmpty();
}

// Stage 1 of a session whose stamp is outdated: needs its events in full. A session
// without any stored events (summary only) is stamped as analysed, with nothing found,
// so that it does not count as outdated forever.
void ensureStageOne(Session *s, const AnalysisParams &p, bool redo)
{
    if (!redo && !stageOneNeeded(s, p).any()) return;
    if (!s->OpenEvents() || s->eventlist.isEmpty()) {
        if (s->sessionRowId() > 0 && EventListRepository().countBySession(s->sessionRowId()) == 0) {
            SessionStamp stamp;
            stamp.flowVersion = stamp.oxiVersion = kAnalysisAlgoVersion;
            stamp.flowHash = p.flowHash();
            stamp.oxiHash = p.oxiHash();
            stamp.write(s);
            s->StoreSetting(AN_Stamp);
        }
        return;
    }
    const QList<ChannelID> written = analyzeSession(s, p, !redo);
    if (written.isEmpty()) return;
    s->StoreChannelEvents(written);
    s->StoreSetting(AN_Stamp);
}

QVector<DayEvent> events(Session *s, ChannelID code, RespEvent type, int source, qint64 shift)
{
    QVector<DayEvent> out;
    for (const Span &sp : sessionSpans(s, code, shift)) {
        out.append(DayEvent { sp.start, sp.end, type, source, sp.value / 100.0f, 0 });
    }
    return out;
}

// Valid SpO2 (or pulse) seconds of a session.
int validSeconds(Session *s, ChannelID code, float minValid, float maxValid)
{
    int n = 0;
    for (float v : sessionGrid(s, code, minValid, maxValid).v) n += hasData(v);
    return n;
}

// The device with the most valid seconds of \a code among the sessions (spec §3.1.6).
Machine *bestSource(const QList<Session *> &sessions, ChannelID code, float minValid, float maxValid)
{
    QHash<Machine *, int> seconds;
    for (Session *s : sessions) {
        if (s->machine()->loaderName() == kAppleHealthLoader) continue;
        if (!s->eventlist.contains(code)) continue;
        seconds[s->machine()] += validSeconds(s, code, minValid, maxValid);
    }
    Machine *best = nullptr;
    int bestSeconds = 0;
    for (Session *s : sessions) {   // in session order, so that ties are stable
        const int n = seconds.value(s->machine());
        if (n > bestSeconds) {
            best = s->machine();
            bestSeconds = n;
        }
    }
    return best;
}

Grid sourceGrid(const QList<Session *> &sessions, Machine *source, ChannelID code, float minValid, float maxValid)
{
    QVector<TimedSamples> lists;
    for (Session *s : sessions) {
        if (s->machine() == source) lists += sessionSamples(s, code, s->correctionMs());
    }
    return toOneHz(lists, minValid, maxValid);
}

// The sessions whose flow the day scores: the CPAP's, and a sleep screener's analysed nasal flow.
QList<Session *> flowSessions(const QList<Session *> &sessions)
{
    QList<Session *> out;
    for (Session *s : sessions) {
        if (s->type() == MT_CPAP) out.append(s);
    }
    for (Session *s : sessions) {
        if (s->type() != MT_OXIMETER) continue;
        const SessionStamp st = SessionStamp::read(s);
        if (st.flowAnalyzed && st.source == QLatin1String("nasal")) out.append(s);
    }
    return out;
}

bool overlapsAny(const Span &sp, const QVector<Span> &spans)
{
    for (const Span &o : spans) {
        if (sp.start < o.end && sp.end > o.start) return true;
    }
    return false;
}

qint64 overlapMs(const Span &sp, const QVector<Span> &spans)
{
    qint64 ms = 0;
    for (const Span &o : spans) ms += qMax<qint64>(0, qMin(sp.end, o.end) - qMax(sp.start, o.start));
    return ms;
}

QString deviceName(Machine *m)
{
    const QString name = (m->info.brand + QLatin1Char(' ') + m->info.model).trimmed();
    return name.isEmpty() ? m->loaderName() : name;
}

// Replaces the hypopnea channels of the day's CPAP sessions with \a result's.
bool storeHypopneas(const QList<Session *> &cpap, const DayResult &result)
{
    bool ok = true;
    for (int k = 0; k < cpap.size(); ++k) {
        Session *s = cpap[k];
        for (ChannelID c : hypopneaChannels()) s->destroyEvent(c);
        QHash<ChannelID, EventList *> lists;
        const qint64 shift = s->correctionMs();
        for (const DayEvent &h : result.hypopneas) {
            if (h.source != k) continue;
            const ChannelID code = h.type == RespEvent::ObstructiveHypopnea ? AN_ObstructiveHypopnea
                                 : h.type == RespEvent::CentralHypopnea ? AN_CentralHypopnea : AN_Hypopnea;
            EventList *&el = lists[code];
            if (!el) el = s->AddEventList(code, EVL_Event);
            addSpanEvent(el, h.start - shift, h.end - shift);
        }
        for (ChannelID c : hypopneaChannels()) {
            if (!s->eventlist.contains(c)) continue;
            s->updateChannelSummary(c);
            if (!s->m_availableChannels.contains(c)) s->m_availableChannels.push_back(c);
        }
        if (s->sessionRowId() > 0) ok = s->StoreChannelEvents(hypopneaChannels()) && ok;
    }
    return ok;
}

// Replaces the oximeter artefacts of the SpO2 source's sessions with the day's: these follow the
// user's «Артефакт» / «Не артефакт» stretches, which stage 1 does not see.
bool storeOxiArtifacts(const QList<Session *> &sessions, const DayResult &result)
{
    Machine *source = bestSource(sessions, OXI_SPO2, 50, 100);
    if (!source) return true;
    bool ok = true;
    for (Session *s : sessions) {
        if (s->machine() != source) continue;
        s->destroyEvent(AN_OxiArtifact);
        const qint64 shift = s->correctionMs();
        EventList *el = nullptr;
        for (const OxiArtifact &a : result.oxi.artifacts) {
            if (a.end <= s->first() || a.start >= s->last()) continue;   // first/last: graph time
            if (!el) el = s->AddEventList(AN_OxiArtifact, EVL_Event, 1.0, 0, 0, 0, 0, true);
            addSpanEvent(el, a.start - shift, a.end - shift, int(a.reason));
        }
        if (el) {
            s->updateChannelSummary(AN_OxiArtifact);
            if (!s->m_availableChannels.contains(AN_OxiArtifact)) s->m_availableChannels.push_back(AN_OxiArtifact);
        }
        if (s->sessionRowId() > 0) ok = s->StoreChannelEvents({ AN_OxiArtifact }) && ok;
    }
    return ok;
}

QString extraJson(const DayResult &r)
{
    QJsonObject o;
    if (r.hasFlow && r.hasOximetry) {
        o.insert("n_linked_desat", r.linkedDesaturations);
        if (r.hasCpap) o.insert("n_dev_linked_desat", r.deviceLinkedDesaturations);
    }
    if (r.screener) o.insert("screener", true);
    if (r.hasFlow && r.flFlowSeconds != r.flowSeconds) o.insert("fl_flow_s", r.flFlowSeconds);   // a mixed night: the CPAP's time
    if (r.hasPulse && r.hasFlow) o.insert("n_ev_pulse_rise", r.eventsWithPulseRise);
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

QList<Session *> analysableSessions(Day *day)
{
    QList<Session *> out;
    if (!day) return out;
    for (Session *s : day->sessions) {
        if (isAnalysable(s) && s->enabled()) out.append(s);
    }
    return sortedSessions(out);
}

QSet<ChannelID> dayScoringChannels()
{
    QSet<ChannelID> out { OXI_SPO2, OXI_Pulse };
    for (ChannelID c : flowChannels()) out.insert(c);
    for (ChannelID c : hypopneaChannels()) out.insert(c);
    for (const DeviceChannel &d : deviceChannels()) out.insert(d.code);
    return out;
}

QString dayInputsHash(Day *day, const AnalysisParams &params)
{
    QStringList parts { QStringLiteral("v=%1;p=%2").arg(kAnalysisAlgoVersion).arg(params.dayHash()) };
    QList<Session *> all;
    if (day) {
        for (Session *s : day->sessions) {
            if (isAnalysable(s)) all.append(s);
        }
    }
    for (Session *s : sortedSessions(all)) {
        // From the summary only: Session::count() falls back on the events when they are
        // loaded, which would make the hash depend on what happens to be in memory.
        QStringList counts;
        for (const DeviceChannel &d : deviceChannels()) counts << QString::number(s->m_cnt.value(d.code, 0), 'g', 10);
        parts << QStringLiteral("m=%1,s=%2,e=%3,f=%4,l=%5,c=%6,n=%7,st=%8")
                     .arg(s->machine()->getDatabaseId()).arg(s->session()).arg(s->enabled() ? 1 : 0)
                     .arg(s->realFirst()).arg(s->realLast()).arg(s->correctionMs())
                     .arg(counts.join(QLatin1Char('/')), SessionStamp::read(s).toJson());
    }
    // the user's «Артефакт» / «Не артефакт» stretches change what the oximetry counts
    const OxiOverrides o = ManualScoring::oximetryOverrides(day);
    for (const Span &sp : o.exclude) parts << QStringLiteral("x=%1-%2").arg(sp.start).arg(sp.end);
    for (const Span &sp : o.keep) parts << QStringLiteral("k=%1-%2").arg(sp.start).arg(sp.end);
    const QByteArray text = parts.join(QLatin1Char(';')).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha1).toHex().left(16));
}

bool dayOutdated(Day *day, const AnalysisParams &params, const AnalysisDailyData &stored)
{
    if (!params.enabled) return false;
    const QList<Session *> sessions = analysableSessions(day);
    if (sessions.isEmpty()) return false;
    for (Session *s : sessions) {
        if (stageOneNeeded(s, params).any()) return true;
    }
    return stored.id == 0 || stored.algoVersion != kAnalysisAlgoVersion || stored.paramsHash != params.dayHash()
        || stored.inputsHash != dayInputsHash(day, params);
}

DayInput buildDayInput(const QList<Session *> &sessions, QString *oxiSource)
{
    DayInput in;
    const QList<Session *> cpap = flowSessions(sessions);
    QVector<Span> cpapTime;   // a screener's flow counts only outside it
    for (Session *s : cpap) {
        if (s->type() == MT_CPAP) cpapTime.append(Span { s->first(), s->last(), 0 });
    }

    for (int k = 0; k < cpap.size(); ++k) {
        Session *s = cpap[k];
        const qint64 shift = s->correctionMs();
        const SessionStamp st = SessionStamp::read(s);
        const bool screener = s->type() != MT_CPAP;
        CpapSession cs;
        cs.span = Span { s->first(), s->last(), 0 };
        cs.cpap = !screener;
        cs.analyzed = st.flowAnalyzed;
        cs.sampleRateHz = st.flowRateHz;
        cs.flScored = st.flScored;
        cs.flowSeconds = st.flowSeconds;
        cs.unscoreableSeconds = st.unscoreableSeconds;
        cs.flSum = st.flSum;
        cs.flBreaths = st.flBreaths;
        cs.flLimitedBreaths = st.flLimitedBreaths;
        cs.glasgow = st.glasgow;
        cs.glasgowAdapted = st.glasgowAdapted;
        // a screener's flow while a CPAP's is analysed: that time is the CPAP's (not counted twice)
        auto outsideCpap = [&](const Span &sp) { return !screener || !overlapsAny(sp, cpapTime); };
        if (screener) {
            const qint64 length = qMax<qint64>(1, cs.span.end - cs.span.start);
            const double outside = 1.0 - double(overlapMs(cs.span, cpapTime)) / length;
            cs.flowSeconds = int(std::lround(cs.flowSeconds * outside));
            cs.unscoreableSeconds = int(std::lround(cs.unscoreableSeconds * outside));
            if (cs.flowSeconds <= 0) cs.analyzed = false;
        }
        in.cpap.append(cs);

        auto keep = [&](QVector<DayEvent> list) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const DayEvent &e) { return !outsideCpap(Span { e.start, e.end, 0 }); }), list.end());
            return list;
        };
        auto keepSpans = [&](QVector<Span> list) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Span &sp) { return !outsideCpap(sp); }), list.end());
            return list;
        };
        in.apneas += keep(events(s, AN_ObstructiveApnea, RespEvent::ObstructiveApnea, k, shift));
        in.apneas += keep(events(s, AN_CentralApnea, RespEvent::CentralApnea, k, shift));
        in.apneas += keep(events(s, AN_Apnea, RespEvent::Apnea, k, shift));
        in.candidates += keep(events(s, AN_FlowReduction, RespEvent::Hypopnea, k, shift));
        in.reras += keepSpans(sessionSpans(s, AN_RERA, shift));
        in.flowLimitation += keepSpans(sessionSpans(s, AN_FlowLimitation, shift));
        in.periodic += keepSpans(sessionSpans(s, AN_PeriodicBreathing, shift));
        in.unscoreable += keepSpans(sessionSpans(s, AN_Unscoreable, shift));
        if (screener) continue;   // no device events, no flow limitation scores
        for (EventList *el : s->eventlist.value(AN_FLScore)) {
            for (quint32 i = 0; i < el->count(); ++i) in.flScores.append(TimedValue { el->time(i) + shift, float(el->data(i)) });
        }

        const DeviceEventConvention convention = deviceEventConvention(s->machine()->loaderName());
        for (const DeviceChannel &d : deviceChannels()) {
            for (EventList *el : s->eventlist.value(d.code)) {
                for (quint32 i = 0; i < el->count(); ++i) {
                    const qint64 t = el->time(i) + shift;
                    const Span sp = deviceEventSpan(t, el->data(i), convention);
                    in.deviceEvents.append(DayEvent { sp.start, sp.end, d.type, k, 0, t });
                }
            }
        }
    }
    auto byStart = [](const DayEvent &a, const DayEvent &b) { return a.start < b.start; };
    std::sort(in.apneas.begin(), in.apneas.end(), byStart);
    std::sort(in.candidates.begin(), in.candidates.end(), byStart);
    std::sort(in.deviceEvents.begin(), in.deviceEvents.end(), byStart);

    Machine *spo2 = bestSource(sessions, OXI_SPO2, 50, 100);
    if (spo2) {
        in.spo2 = sourceGrid(sessions, spo2, OXI_SPO2, 50, 100);
        in.oxiFromSeparateDevice = spo2->type() != MT_CPAP;
        if (oxiSource) *oxiSource = deviceName(spo2);
    }
    // Pulse from the SpO2 device when it has any, else from the device with the most.
    Machine *pulse = nullptr;
    for (Session *s : sessions) {
        if (spo2 && s->machine() == spo2 && s->eventlist.contains(OXI_Pulse)) pulse = spo2;
    }
    if (!pulse) pulse = bestSource(sessions, OXI_Pulse, 30, 220);
    if (pulse) in.pulse = sourceGrid(sessions, pulse, OXI_Pulse, 30, 220);
    return in;
}

AnalysisDailyData toDailyRow(const DayResult &r, const AnalysisParams &params, const QString &inputsHash,
                             const QString &oxiSource)
{
    AnalysisDailyData d;
    d.algoVersion = kAnalysisAlgoVersion;
    d.paramsHash = params.dayHash();
    d.inputsHash = inputsHash;
    d.computedAt = QDateTime::currentDateTime();

    d.hasFlow = r.hasFlow;
    d.flowSeconds = r.flowSeconds;
    d.flowRateHz = r.flowRateHz;
    d.unscoreableSeconds = r.unscoreableSeconds;
    d.nObstructiveApnea = r.count(RespEvent::ObstructiveApnea);
    d.nCentralApnea = r.count(RespEvent::CentralApnea);
    d.nApnea = r.count(RespEvent::Apnea);
    d.nObstructiveHypopnea = r.count(RespEvent::ObstructiveHypopnea);
    d.nCentralHypopnea = r.count(RespEvent::CentralHypopnea);
    d.nHypopnea = r.count(RespEvent::Hypopnea);
    d.nRera = r.reras.size();
    d.nUnconfirmable = r.unconfirmable;
    d.nHypopneaAasm3 = r.hypopneasAasm3;
    d.nHypopneaCms4 = r.hypopneasCms4;
    d.nHypopneaFlow = r.hypopneasFlowOnly;
    d.flSeconds = r.flSeconds;
    d.flSum = r.flSum;
    d.flBreaths = r.flBreaths;
    d.hasFlRuns = r.flLimitedBreaths >= 0;
    d.flLimitedBreaths = qMax(0, r.flLimitedBreaths);
    d.flLongestSeconds = r.flLongestSeconds;
    d.glasgow = r.glasgow;
    d.glasgowAdapted = r.glasgowAdapted;
    d.pbSeconds = r.pbSeconds;
    d.hypopneaRule = int(r.rule);

    d.hasComparison = r.hasComparison;
    d.devApnea = r.deviceCount(EventGroup::Apnea);
    d.devHypopnea = r.deviceCount(EventGroup::Hypopnea);
    d.devRera = r.deviceCount(EventGroup::Rera);
    d.cmpMatched = r.match.matched.size();
    d.cmpDeviceOnly = r.match.deviceOnly.size();
    d.cmpAnalysisOnly = r.match.analysisOnly.size();
    d.cmpTypeMismatch = r.match.typeMismatch;

    const OxiResult &o = r.oxi;
    d.hasOximetry = r.hasOximetry;
    d.hasCpap = r.hasCpap;
    d.screener = r.screener;
    d.flFlowSeconds = r.flFlowSeconds != r.flowSeconds ? r.flFlowSeconds : 0;
    d.oxiSeconds = o.spo2Seconds;
    d.oxiScope = r.oxiScope;
    d.oxiSource = oxiSource;
    d.nDesat3 = o.countDesaturations(3);
    d.nDesat4 = o.countDesaturations(4);
    d.spo2Hist = o.spo2Hist;
    d.spo2Sum = o.spo2Sum;
    d.spo2Median = hasData(o.spo2Median) ? o.spo2Median : 0;
    d.spo2Nadir = hasData(o.spo2Nadir) ? o.spo2Nadir : 0;
    d.desatArea = o.desaturationArea();
    d.linkedDesatArea = r.linkedDesatArea;
    d.nUnexplainedDesat = r.unexplained.size();
    d.nCyclic = o.cyclic.size();
    d.cyclicSeconds = o.cyclicSeconds();
    d.nZones = o.zones.size();
    d.zoneSeconds = o.zoneSeconds(1);
    d.zoneSevereSeconds = o.zoneSeconds(2);

    d.hasPulse = r.hasPulse;
    d.pulseSeconds = o.pulseSeconds;
    d.pulseSum = o.pulseSum;
    d.pulseSqSum = o.pulseSqSum;
    d.pulseMin = hasData(o.pulseMin) ? o.pulseMin : 0;
    d.pulseMax = hasData(o.pulseMax) ? o.pulseMax : 0;
    d.pulseHist = o.pulseHist;
    d.nPulseRise = o.pulseRises.size();
    d.dhrSum = r.dhrSum;
    d.nDhr = r.dhrEvents;
    d.bradySeconds = o.bradySeconds();
    d.tachySeconds = o.tachySeconds();

    d.hasOffsetHint = r.hasOffsetHint;
    d.oxiOffsetHintMs = r.offsetHintMs;
    d.extraJson = extraJson(r);
    return d;
}

DayAnalysis analyzeDay(Day *day, const AnalysisParams &params, bool onlyIfOutdated, bool redoStageOne)
{
    DayAnalysis out;
    if (!params.enabled || !day) return out;
    const QList<Session *> sessions = analysableSessions(day);
    if (sessions.isEmpty()) return out;

    // Events this call loads are put away again, whatever happens.
    QList<Session *> loadedHere;
    for (Session *s : sessions) {
        if (!eventsInMemory(s)) loadedHere.append(s);
    }
    const auto release = qScopeGuard([&loadedHere] {
        for (Session *s : loadedHere) s->TrashEvents();
    });

    for (Session *s : sessions) ensureStageOne(s, params, redoStageOne);

    const qint64 profileId = sessions.first()->machine()->getProfileId();
    const QString inputs = dayInputsHash(day, params);
    AnalysisDailyRepository repo;
    if (onlyIfOutdated && profileId > 0) {
        const AnalysisDailyData stored = repo.find(profileId, day->date());
        if (stored.id && stored.algoVersion == kAnalysisAlgoVersion && stored.paramsHash == params.dayHash()
            && stored.inputsHash == inputs) {
            out.upToDate = true;
            return out;
        }
    }

    const QSet<ChannelID> needed = dayScoringChannels();
    for (Session *s : sessions) s->LoadEventsFromDatabase(needed);
    DayInput in = buildDayInput(sessions, &out.oxiSource);
    in.oxiOverrides = ManualScoring::oximetryOverrides(day);
    out.result = scoreDay(in, params);
    out.scored = true;

    if (out.result.hasApneaOffset) {
        qDebug().noquote() << "Sleep analysis:" << day->date().toString(Qt::ISODate)
                           << "median offset of matched device apneas from the analysis' event end"
                           << out.result.apneaOffsetToEndMs / 1000.0 << "s, from its start"
                           << out.result.apneaOffsetToStartMs / 1000.0 << "s";
    }

    // the hypopneas go back into the session whose flow they were found in (a CPAP's or a screener's)
    bool ok = storeHypopneas(flowSessions(sessions), out.result);
    ok = storeOxiArtifacts(sessions, out.result) && ok;
    if (profileId > 0) {
        AnalysisDailyData row = toDailyRow(out.result, params, inputs, out.oxiSource);
        row.profileId = profileId;
        row.date = day->date();
        ok = repo.upsert(row) && ok;
    }
    out.stored = ok && profileId > 0;
    return out;
}

DayResult scoreStoredDay(Day *day, const AnalysisParams &params, QString *oxiSource)
{
    const QList<Session *> sessions = analysableSessions(day);
    QList<Session *> loadedHere;
    for (Session *s : sessions) {
        if (!eventsInMemory(s)) loadedHere.append(s);
    }
    const auto release = qScopeGuard([&loadedHere] {
        for (Session *s : loadedHere) s->TrashEvents();
    });
    const QSet<ChannelID> needed = dayScoringChannels();
    for (Session *s : sessions) s->LoadEventsFromDatabase(needed);
    DayInput in = buildDayInput(sessions, oxiSource);
    in.oxiOverrides = ManualScoring::oximetryOverrides(day);
    return scoreDay(in, params);
}

} // namespace analysis
