/* Oximetry Session Builder
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oximetry_session_builder.h"
#include "SleepLib/calcs.h"
#include "SleepLib/session.h"
#include "SleepLib/analysis/session_analysis.h"

qint64 addOximetryEvents(Session *session, qint64 startMs, const QVector<OxiRecord> &records,
                         qint64 stepMs, bool havePerfIndex)
{
    EventList *ELpulse = nullptr;
    EventList *ELspo2 = nullptr;
    EventList *ELperf = nullptr;
    quint16 lastpulse = 0, lastspo2 = 0, lastperf = 0;
    quint16 lastgoodpulse = 0, lastgoodspo2 = 0, lastgoodperf = 0;
    qint64 ti = startMs;

    for (const OxiRecord &rec : records) {
        if (rec.pulse > 0) {
            if (lastpulse == 0) ELpulse = session->AddEventList(OXI_Pulse, EVL_Event);
            if (lastpulse != rec.pulse) {
                if (lastpulse > 0) ELpulse->AddEvent(ti, lastpulse);
                ELpulse->AddEvent(ti, rec.pulse);
            }
            lastgoodpulse = rec.pulse;
        } else if (lastgoodpulse > 0) {          // end section properly
            ELpulse->AddEvent(ti, lastpulse);
            session->setLast(OXI_Pulse, ti);
            lastgoodpulse = 0;
        }
        lastpulse = rec.pulse;

        if (rec.spo2 > 0) {
            if (lastspo2 == 0) ELspo2 = session->AddEventList(OXI_SPO2, EVL_Event);
            if (lastspo2 != rec.spo2) {
                if (lastspo2 > 0) ELspo2->AddEvent(ti, lastspo2);
                ELspo2->AddEvent(ti, rec.spo2);
            }
            lastgoodspo2 = rec.spo2;
        } else if (lastgoodspo2 > 0) {
            ELspo2->AddEvent(ti, lastspo2);
            session->setLast(OXI_SPO2, ti);
            lastgoodspo2 = 0;
        }
        lastspo2 = rec.spo2;

        if (havePerfIndex) {                      // Perfusion Index
            if (rec.perf > 0) {
                if (lastperf == 0) ELperf = session->AddEventList(OXI_Perf, EVL_Event, 0.01f);
                if (lastperf != rec.perf) {
                    if (lastperf > 0) ELperf->AddEvent(ti, lastperf);
                    ELperf->AddEvent(ti, rec.perf);
                }
                lastgoodperf = rec.perf;
            } else if (lastgoodperf > 0) {
                ELperf->AddEvent(ti, lastperf);
                session->setLast(OXI_Perf, ti);
                lastgoodperf = 0;
            }
            lastperf = rec.perf;
        }
        ti += stepMs;
    }
    ti -= stepMs;
    if (ELpulse && lastpulse > 0) {
        ELpulse->AddEvent(ti, lastpulse);
        session->setLast(OXI_Pulse, ti);
    }
    if (ELspo2 && lastspo2 > 0) {
        ELspo2->AddEvent(ti, lastspo2);
        session->setLast(OXI_SPO2, ti);
    }
    if (havePerfIndex && ELperf && lastperf > 0) {
        ELperf->AddEvent(ti, lastperf);
        session->setLast(OXI_Perf, ti);
    }
    return ti;
}

void finishOximetrySession(Session *session, qint64 lastMs, bool havePerfIndex)
{
    if (havePerfIndex) {
        session->first(OXI_Perf);
        session->last(OXI_Perf);
        session->count(OXI_Perf);
        session->Min(OXI_Perf);
        session->Max(OXI_Perf);
    }

    calcSPO2Drop(session);
    calcPulseChange(session);
    analysis::analyzeSession(session, analysis::activeParams(), true);   // also makes its summaries

    session->first(OXI_Pulse);
    session->first(OXI_SPO2);
    session->last(OXI_Pulse);
    session->last(OXI_SPO2);

    session->first(OXI_PulseChange);
    session->first(OXI_SPO2Drop);
    session->last(OXI_PulseChange);
    session->last(OXI_SPO2Drop);

    session->cph(OXI_PulseChange);
    session->sph(OXI_PulseChange);
    session->cph(OXI_SPO2Drop);
    session->sph(OXI_SPO2Drop);

    session->count(OXI_Pulse);
    session->count(OXI_SPO2);
    session->count(OXI_PulseChange);
    session->count(OXI_SPO2Drop);
    session->Min(OXI_Pulse);
    session->Min(OXI_SPO2);
    session->Max(OXI_Pulse);
    session->Max(OXI_SPO2);
    // avg/wavg are needed so daily_summaries.spo2_avg and pulse_avg are correct
    session->avg(OXI_Pulse);
    session->avg(OXI_SPO2);
    session->wavg(OXI_Pulse);
    session->wavg(OXI_SPO2);

    session->really_set_last(lastMs);
    session->SetChanged(true);
    session->setOpened(true);
}
