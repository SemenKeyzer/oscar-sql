/* Contec BLE Oximeter Import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_import.h"

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/oximetry_session_builder.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"
#include "database/event_list_repository.h"

namespace ContecBle {

Decision decideRecord(const RecordHeader &h, const ExistingSession &same, bool nightHasOtherOximeter)
{
    if (!h.start().isValid()) return Decision::InvalidStart;
    if (same.exists) return same.samples >= h.samples ? Decision::AlreadyPresent : Decision::ReplaceShorter;
    if (nightHasOtherOximeter) return Decision::ConflictOtherOximeter;
    return Decision::Import;
}

QDate predictNight(const QDateTime &startLocal, const QTime &daySplit)
{
    const QDate d = startLocal.date();
    return startLocal.time() < daySplit ? d.addDays(-1) : d;
}

QVector<OxiRecord> toOxiRecords(const Record &r)
{
    const int n = r.header.samples;
    QVector<OxiRecord> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int s = i < r.spo2.size() ? r.spo2[i] : 0;
        const int p = i < r.pulse.size() ? r.pulse[i] : 0;
        const int pi = (r.header.hasPI && i < r.pi.size()) ? r.pi[i] : 0;
        const quint8 spo2 = (s > 0 && s <= 100) ? quint8(s) : 0;
        const quint8 pulse = (p > 0 && p < 255) ? quint8(p) : 0;
        const quint16 perf = (pi > 0 && pi < 255) ? quint16(pi * 10) : 0;
        out.append(OxiRecord(pulse, spo2, perf));
    }
    return out;
}

EraseVerdict canErase(const EraseInput &in)
{
    if (!in.eraseEnabled) return EraseVerdict::Disabled;
    if (!in.downloadCompleted) return EraseVerdict::DownloadIncomplete;
    if (in.headersOnDevice <= 0) return EraseVerdict::NothingToErase;
    if (in.outcomes.size() < in.headersOnDevice) return EraseVerdict::NotAllSaved;
    for (Outcome o : in.outcomes) {
        if (o != Outcome::Imported && o != Outcome::Updated && o != Outcome::AlreadyPresent)
            return EraseVerdict::NotAllSaved;
    }
    // Record times are on the oximeter's clock; until OSCAR has set it once they can't be
    // compared with ours, so a recording still in progress could look finished.
    if (!in.clockTrusted) return EraseVerdict::ClockNotSynced;
    // The oximeter keeps writing while the sensor is on; erasing now would lose the tail.
    if (in.lastRecordEnd.isValid() && in.lastRecordEnd > in.downloadStarted.addSecs(-300))
        return EraseVerdict::StillRecording;
    return EraseVerdict::Erase;
}

} // namespace ContecBle

using namespace ContecBle;

namespace {
Machine *otherOximeter(Machine *mine, const RecordHeader &h)
{
    const QDateTime start = h.start();
    if (!start.isValid() || !p_profile) return nullptr;
    Day *day = p_profile->GetDay(predictNight(start, p_profile->session->daySplitTime()), MT_OXIMETER);
    Machine *oxi = day ? day->machine(MT_OXIMETER) : nullptr;
    return (oxi && oxi != mine) ? oxi : nullptr;
}

QString deviceName(Machine *m)
{
    const QString model = m->model().trimmed();
    return model.isEmpty() ? m->loaderName() : model;
}
} // namespace

ExistingSession ContecBleImporter::existing(const RecordHeader &h) const
{
    ExistingSession e;
    const QDateTime start = h.start();
    if (!start.isValid()) return e;
    Session *s = m_mach->sessionlist.value(SessionID(start.toUTC().toSecsSinceEpoch()), nullptr);
    if (s) {
        e.exists = true;
        e.samples = int((s->realLast() - s->realFirst()) / 1000) + 1;
    }
    return e;
}

bool ContecBleImporter::nightHasOtherOximeter(const RecordHeader &h) const
{
    return otherOximeter(m_mach, h) != nullptr;
}

QString ContecBleImporter::otherOximeterName(const RecordHeader &h) const
{
    Machine *other = otherOximeter(m_mach, h);
    return other ? deviceName(other) : QString();
}

Decision ContecBleImporter::decide(const RecordHeader &h) const
{
    return decideRecord(h, existing(h), nightHasOtherOximeter(h));
}

Outcome ContecBleImporter::save(const Record &r, Decision d)
{
    const QDateTime start = r.header.start();
    if (!start.isValid()) return Outcome::InvalidStart;
    const QVector<OxiRecord> recs = toOxiRecords(r);
    if (recs.isEmpty()) return Outcome::SaveFailed;
    const SessionID sid = SessionID(start.toUTC().toSecsSinceEpoch());
    const qint64 startMs = qint64(sid) * 1000;

    // The shorter copy is set aside, not destroyed: it goes back if the longer one isn't stored.
    // Its database row is reused by the replacement (rows are keyed by device and session id).
    Session *old = nullptr;
    if (d == Decision::ReplaceShorter) {
        old = m_mach->sessionlist.value(sid, nullptr);
        if (old) m_mach->unlinkSession(old);
    }
    auto reject = [&](Session *sess, Outcome o) {
        m_mach->unlinkSession(sess);
        delete sess;
        if (old) m_mach->AddSession(old, true);
        return o;
    };

    Session *sess = new Session(m_mach, sid);
    sess->really_set_first(startMs);
    const qint64 lastMs = addOximetryEvents(sess, startMs, recs, 1000, r.header.hasPI);
    finishOximetrySession(sess, lastMs, r.header.hasPI);
    if (!m_mach->AddSession(sess)) {
        return reject(sess, Outcome::SaveFailed);
    }
    // A day holds one oximeter: Day::addSession refuses a second one without telling the caller.
    if (sess->night().isValid()) {
        Day *day = p_profile->GetDay(sess->night());
        if (!day || !day->sessions.contains(sess)) {
            return reject(sess, Outcome::ConflictOtherOximeter);
        }
    }
    m_mach->Save();
    // Machine::Save() only logs a session it couldn't write; a stored one has a row and is clean.
    // Session::Store() doesn't report a failed sample write at all, so look for the samples too.
    if (sess->sessionRowId() <= 0 || sess->IsChanged()
            || EventListRepository().countBySession(sess->sessionRowId()) == 0) {
        return reject(sess, Outcome::SaveFailed);
    }
    delete old;
    m_mach->SaveSummaryCache();
    p_profile->StoreMachines();
    return d == Decision::ReplaceShorter ? Outcome::Updated : Outcome::Imported;
}

void ContecBleImporter::finish()
{
    if (p_profile) p_profile->calculateDailySummaries();
}
