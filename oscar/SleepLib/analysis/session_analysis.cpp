/* Sleep Analysis of One Session
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "session_analysis.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>
#include <exception>

#include "analysis_channels.h"
#include "flow_analyzer.h"
#include "oxi_analyzer.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

namespace analysis {

namespace {

QMutex s_paramsMutex;
AnalysisParams s_params;

// Apple Health spot checks are far too sparse to analyse (as for calcSPO2Drop()).
const QString kAppleHealthLoader = QStringLiteral("AppleHealth");

QVector<TimedSamples> samplesOf(Session *s, ChannelID code)
{
    QVector<TimedSamples> out;
    if (code == 0) return out;
    auto it = s->eventlist.constFind(code);
    if (it == s->eventlist.constEnd()) return out;
    for (EventList *el : it.value()) {
        if (!el || el->count() == 0) continue;
        const int n = int(el->count());
        TimedSamples ts;
        ts.t.reserve(n);
        ts.v.reserve(n);
        const bool waveform = el->type() == EVL_Waveform && el->rate() > 0;
        for (int i = 0; i < n; ++i) {
            ts.t.append(waveform ? el->first() + qint64(std::llround(i * double(el->rate()))) : el->time(i));
            ts.v.append(el->data(i));
        }
        ts.end = waveform ? el->first() + qint64(std::llround(n * double(el->rate()))) : el->last();
        out.append(ts);
    }
    return out;
}

// Span events (time = end, data = duration in seconds) of a channel.
QVector<Span> spansOf(Session *s, ChannelID code)
{
    QVector<Span> out;
    if (code == 0) return out;
    auto it = s->eventlist.constFind(code);
    if (it == s->eventlist.constEnd()) return out;
    for (EventList *el : it.value()) {
        if (!el) continue;
        for (quint32 i = 0; i < el->count(); ++i) {
            const qint64 end = el->time(i);
            out.append(Span { end - qint64(std::llround(el->data(i) * 1000.0)), end, 0 });
        }
    }
    return out;
}

ChannelID channelByCode(const char *code) { return schema::channel[QString(code)].id(); }

EventStoreType raw(double v)
{
    return EventStoreType(qBound(-32768.0, std::round(v), 32767.0));
}

// Adds a span event (OSCAR's convention: time = end, data = duration in s).
void addSpan(EventList *el, qint64 start, qint64 end, double value2 = 0)
{
    const double seconds = (end - start) / 1000.0;
    if (el->hasSecondField()) el->AddEvent(end, raw(seconds), raw(value2));
    else el->AddEvent(end, raw(seconds));
}

// The list for \a code, created on first use (no empty lists are left behind).
class Lists
{
  public:
    explicit Lists(Session *s) : m_s(s) {}
    EventList *get(ChannelID code, bool second = false, EventDataType gain = 1.0) {
        EventList *&el = m_lists[code];
        if (!el) el = m_s->AddEventList(code, EVL_Event, gain, 0, 0, 0, 0, second);
        return el;
    }
  private:
    Session *m_s;
    QHash<ChannelID, EventList *> m_lists;
};

void runFlow(Session *s, const AnalysisParams &p)
{
    auto it = s->eventlist.constFind(CPAP_FlowRate);
    if (it == s->eventlist.constEnd()) return;
    QVector<FlowChunk> chunks;
    for (EventList *el : it.value()) {
        if (!el || el->type() != EVL_Waveform || el->count() < 2 || el->rate() <= 0) continue;
        FlowChunk c;
        c.start = el->first();
        c.rateMs = el->rate();
        c.samples.reserve(int(el->count()));
        for (quint32 i = 0; i < el->count(); ++i) c.samples.append(el->data(i));
        chunks.append(c);
    }
    if (chunks.isEmpty()) return;

    QVector<Span> excluded = spansOf(s, CPAP_LargeLeak);
    excluded += spansOf(s, channelByCode("Prisma_Artifact"));
    excluded += spansOf(s, channelByCode("Prisma_CriticalLeak"));
    const Grid pulse = sessionGrid(s, OXI_Pulse, 30, 220);
    const Grid obstruct = toOneHz(samplesOf(s, channelByCode("Prisma_ObstructLevel")), 0, 100);

    const FlowResult r = analyzeFlow(chunks, excluded, pulse.size() ? &pulse : nullptr,
                                     obstruct.size() ? &obstruct : nullptr, p.flow);
    if (!r.analyzed) return;

    Lists lists(s);
    for (const FlowEvent &ev : r.events) {
        if (ev.apnea) {
            const ChannelID code = ev.cls == ApneaClass::Obstructive ? AN_ObstructiveApnea
                                 : ev.cls == ApneaClass::Central ? AN_CentralApnea : AN_Apnea;
            addSpan(lists.get(code), ev.start, ev.end);
        } else {
            addSpan(lists.get(AN_FlowReduction, true), ev.start, ev.end, 100.0 * ev.reduction);
        }
    }
    for (const Span &sp : r.reras) addSpan(lists.get(AN_RERA), sp.start, sp.end);
    for (const Span &sp : r.flowLimitation) addSpan(lists.get(AN_FlowLimitation), sp.start, sp.end);
    for (const Span &sp : r.periodic) addSpan(lists.get(AN_PeriodicBreathing, true), sp.start, sp.end, sp.value);
    for (const Span &sp : r.unscoreable) addSpan(lists.get(AN_Unscoreable), sp.start, sp.end);
    for (const Breath &b : r.breaths) {
        if (hasData(b.fl)) lists.get(AN_FLScore, false, 0.01f)->AddEvent(b.inspEnd, raw(b.fl * 100));
    }
}

void runOximetry(Session *s, const AnalysisParams &p)
{
    if (s->machine() && s->machine()->loaderName() == kAppleHealthLoader) return;
    const Grid spo2 = sessionGrid(s, OXI_SPO2, 50, 100);
    const Grid pulse = sessionGrid(s, OXI_Pulse, 30, 220);
    if (spo2.size() == 0 && pulse.size() == 0) return;

    const OxiResult r = analyzeOximetry(spo2, pulse, p.oxi);
    Lists lists(s);
    for (const Desaturation &d : r.desaturations) {
        addSpan(lists.get(AN_Desaturation, true), d.start, d.end, d.depth());
    }
    for (const Span &sp : r.cyclic) addSpan(lists.get(AN_CyclicDesaturation, true), sp.start, sp.end, sp.value);
    for (const PulseRise &pr : r.pulseRises) addSpan(lists.get(AN_PulseRise, true), pr.start, pr.end, pr.amplitude);
    for (const Span &sp : r.bradycardia) addSpan(lists.get(AN_Bradycardia), sp.start, sp.end);
    for (const Span &sp : r.tachycardia) addSpan(lists.get(AN_Tachycardia), sp.start, sp.end);
    for (const ProblemZone &z : r.zones) addSpan(lists.get(AN_OxiProblemZone, true), z.start, z.end, z.severity);
}

} // namespace

AnalysisParams activeParams()
{
    QMutexLocker lock(&s_paramsMutex);
    return s_params;
}

void setActiveParams(const AnalysisParams &params)
{
    QMutexLocker lock(&s_paramsMutex);
    s_params = params;
}

SessionStamp SessionStamp::read(Session *session)
{
    SessionStamp st;
    const QString json = session->settings.value(AN_Stamp).toString();
    if (json.isEmpty()) return st;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonObject flow = o.value("flow").toObject();
    const QJsonObject oxi = o.value("oxi").toObject();
    st.flowVersion = flow.value("v").toInt();
    st.flowHash = flow.value("p").toString();
    st.oxiVersion = oxi.value("v").toInt();
    st.oxiHash = oxi.value("p").toString();
    return st;
}

QString SessionStamp::toJson() const
{
    QJsonObject o;
    if (flowVersion) o.insert("flow", QJsonObject { { "v", flowVersion }, { "p", flowHash } });
    if (oxiVersion) o.insert("oxi", QJsonObject { { "v", oxiVersion }, { "p", oxiHash } });
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void SessionStamp::write(Session *session) const
{
    session->settings[AN_Stamp] = toJson();
}

StageOneNeed stageOneNeeded(Session *session, const AnalysisParams &params)
{
    const SessionStamp st = SessionStamp::read(session);
    StageOneNeed need;
    need.flow = st.flowVersion != kAnalysisAlgoVersion || st.flowHash != params.flowHash();
    need.oxi = st.oxiVersion != kAnalysisAlgoVersion || st.oxiHash != params.oxiHash();
    return need;
}

Grid sessionGrid(Session *session, ChannelID code, float minValid, float maxValid)
{
    return toOneHz(samplesOf(session, code), minValid, maxValid);
}

QList<ChannelID> analyzeSession(Session *s, const AnalysisParams &p, bool onlyIfOutdated)
{
    QList<ChannelID> written;
    if (!p.enabled || !s || !s->machine()) return written;
    const MachineType mt = s->type();
    if (mt != MT_CPAP && mt != MT_OXIMETER) return written;
    // Only on the session's full events: with none in memory (a summary being re-saved)
    // or only some, it would record an analysis of nothing as up to date.
    if (s->eventlist.isEmpty() || s->partialEvents()) return written;

    const StageOneNeed need = onlyIfOutdated ? stageOneNeeded(s, p) : StageOneNeed { true, true };
    if (!need.any()) return written;

    SessionStamp stamp = SessionStamp::read(s);
    if (need.flow) {
        for (ChannelID c : flowChannels()) s->destroyEvent(c);
        try {
            runFlow(s, p);
        } catch (const std::exception &e) {
            qWarning() << "Sleep analysis: flow analysis of session" << s->session() << "failed:" << e.what();
        }
        written += flowChannels();
        stamp.flowVersion = kAnalysisAlgoVersion;
        stamp.flowHash = p.flowHash();
    }
    if (need.oxi) {
        for (ChannelID c : oximetryChannels()) s->destroyEvent(c);
        try {
            runOximetry(s, p);
        } catch (const std::exception &e) {
            qWarning() << "Sleep analysis: oximetry analysis of session" << s->session() << "failed:" << e.what();
        }
        written += oximetryChannels();
        stamp.oxiVersion = kAnalysisAlgoVersion;
        stamp.oxiHash = p.oxiHash();
    }
    stamp.write(s);

    for (ChannelID c : written) {
        if (!s->eventlist.contains(c)) continue;
        s->updateChannelSummary(c);
        if (!s->m_availableChannels.contains(c)) s->m_availableChannels.push_back(c);
    }
    return written;
}

} // namespace analysis
