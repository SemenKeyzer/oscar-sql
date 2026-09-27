/* Time Alignment Session Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignsession.h"
#include "SleepLib/machine.h"
#include "database/database_manager.h"

#include <QSqlDatabase>
#include <QtGlobal>
#include <cmath>

// ---------------------------------------------------------------------------
// RepositoryTimeAlignStore
// ---------------------------------------------------------------------------

QList<DeviceTimeCorrectionData> RepositoryTimeAlignStore::findActive(qint64 machineId)
{
    return m_repo.findActive(machineId);
}

QList<DeviceTimeCorrectionData> RepositoryTimeAlignStore::findManualOffsetRows(qint64 machineId)
{
    return m_repo.findManualOffsetRows(machineId);
}

bool RepositoryTimeAlignStore::upsertOffset(qint64 machineId, const QString& date, qint64 offsetMs)
{
    // upsertOffset() marks the old row undone before inserting the new one: keep both steps
    // atomic so a failed insert cannot lose the previous offset. If a transaction is already
    // open elsewhere, transaction() fails and we simply run inside it.
    QSqlDatabase db = DatabaseManager::instance().database();
    const bool ownTransaction = db.transaction();
    const bool ok = m_repo.upsertOffset(machineId, date, offsetMs) >= 0;
    if (ownTransaction) {
        if (ok && db.commit()) return true;
        db.rollback();
        return false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// TimeAlignSession
// ---------------------------------------------------------------------------

TimeAlignSession::TimeAlignSession(TimeAlignStore* store, QObject* parent)
    : QObject(parent)
    , m_store(store ? store : new RepositoryTimeAlignStore)
{
}

TimeAlignSession::~TimeAlignSession() = default;

bool TimeAlignSession::isThisNightsOffsetRow(const DeviceTimeCorrectionData& d) const
{
    const QString night = m_night.toString(Qt::ISODate);
    return d.type == QLatin1String("offset") && d.dateFrom == night && d.dateTo == night;
}

bool TimeAlignSession::begin(Machine* mach, const QDate& night)
{
    if (isDirty()) restoreStored(m_machine);
    end();
    if (!mach || !night.isValid() || mach->getDatabaseId() <= 0) return false;

    m_machine = mach;
    m_night   = night;
    for (const auto& d : m_store->findActive(mach->getDatabaseId())) {
        if (isThisNightsOffsetRow(d)) m_savedMs = d.offsetMs;
        else                          m_otherRows.append(d);
    }
    m_offsetMs = m_savedMs;
    restoreStored(mach);   // in-memory corrections must match the store before previewing
    return true;
}

void TimeAlignSession::end()
{
    m_machine  = nullptr;
    m_night    = QDate();
    m_savedMs  = 0;
    m_offsetMs = 0;
    m_otherRows.clear();
}

qint64 TimeAlignSession::otherCorrectionsMs() const
{
    if (!m_machine) return 0;
    // The machine's in-memory corrections are always "other rows + this night's value".
    return m_machine->correctionMs(m_night) - m_offsetMs;
}

std::optional<qint64> TimeAlignSession::previousNightOffset() const
{
    if (!m_machine) return std::nullopt;
    std::optional<qint64> result;
    QDate best;
    for (const auto& d : m_store->findManualOffsetRows(m_machine->getDatabaseId())) {
        const QDate date = QDate::fromString(d.dateFrom, Qt::ISODate);
        if (!date.isValid() || date >= m_night) continue;
        if (!best.isValid() || date > best) {
            best   = date;
            result = d.offsetMs;
        }
    }
    return result;
}

void TimeAlignSession::setOffsetMs(qint64 ms)
{
    if (!m_machine) return;
    ms = qBound(-kMaxOffsetMs, ms, kMaxOffsetMs);
    if (ms == m_offsetMs) return;
    m_offsetMs = ms;
    applyPreview();
    emit offsetChanged(m_offsetMs);
}

bool TimeAlignSession::commit()
{
    if (!m_machine) return false;
    if (!isDirty()) return true;
    if (!m_store->upsertOffset(m_machine->getDatabaseId(), m_night.toString(Qt::ISODate), m_offsetMs)) {
        return false;
    }
    m_savedMs = m_offsetMs;
    restoreStored(m_machine);
    return true;
}

void TimeAlignSession::cancel()
{
    if (!m_machine) return;
    const bool changed = (m_offsetMs != m_savedMs);
    m_offsetMs = m_savedMs;
    restoreStored(m_machine);
    if (changed) emit offsetChanged(m_offsetMs);
}

void TimeAlignSession::applyPreview()
{
    QList<TimeCorrectionRow> rows;
    rows.reserve(m_otherRows.size() + 1);
    for (const auto& d : m_otherRows) {
        rows.append(Machine::rowFromData(d));
    }
    if (m_offsetMs != 0) {
        TimeCorrectionRow r;
        r.dateFrom = r.dateTo = m_night;
        r.type     = QStringLiteral("offset");
        r.offsetMs = m_offsetMs;
        rows.append(r);
    }
    m_machine->rebuildCorrections(rows);
}

void TimeAlignSession::restoreStored(Machine* mach)
{
    QList<TimeCorrectionRow> rows;
    for (const auto& d : m_store->findActive(mach->getDatabaseId())) {
        rows.append(Machine::rowFromData(d));
    }
    mach->rebuildCorrections(rows);
}

qint64 TimeAlignSession::snapStepMs(double msPerPx)
{
    if (msPerPx > 20000.0) return 60000;
    if (msPerPx > 2000.0)  return 10000;
    return 1000;
}

qint64 TimeAlignSession::snapDelta(double rawDeltaMs, double msPerPx)
{
    if (!std::isfinite(rawDeltaMs)) return 0;
    const qint64 step = snapStepMs(msPerPx);
    return qRound64(rawDeltaMs / double(step)) * step;
}

QString TimeAlignSession::formatOffset(qint64 ms)
{
    const QChar sign = (ms < 0) ? QLatin1Char('-') : QLatin1Char('+');
    const qint64 s = qAbs(ms) / 1000;
    return QStringLiteral("%1%2:%3:%4")
        .arg(sign)
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}
