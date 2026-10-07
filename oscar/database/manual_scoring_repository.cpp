/* Manual scoring storage
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "database/manual_scoring_repository.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QStringList>

#include "database/database_manager.h"

using ManualScoring::Edit;
using ManualScoring::Kind;
using ManualScoring::SessionKey;

namespace {

const QString kWhereKey = QStringLiteral("profile_id = ? AND machine_serial = ? AND session_id = ?");

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Add: return QStringLiteral("add");
    case Kind::Remove: return QStringLiteral("remove");
    case Kind::Retype: return QStringLiteral("retype");
    case Kind::Exclude: return QStringLiteral("exclude");
    case Kind::Resize: return QStringLiteral("resize");
    }
    return QString();
}

Kind kindOf(const QString &name)
{
    if (name == QLatin1String("remove")) return Kind::Remove;
    if (name == QLatin1String("retype")) return Kind::Retype;
    if (name == QLatin1String("exclude")) return Kind::Exclude;
    if (name == QLatin1String("resize")) return Kind::Resize;
    return Kind::Add;
}

void bindKey(QSqlQuery &q, const SessionKey &key)
{
    q.addBindValue(key.profileId);
    q.addBindValue(key.serial.isNull() ? QStringLiteral("") : key.serial);   // a device without a serial: '', not NULL
    q.addBindValue(qint64(key.session));
}

// The sessions with edits, per database file and profile: (serial, device session number).
// Guarded by a mutex: sessions are stored from worker threads during an import.
QHash<QString, QSet<QPair<QString, qint64>>> s_withScoring;
QMutex s_withScoringLock;

QString cacheKey(qint64 profileId)
{
    return DatabaseManager::instance().database().databaseName() + QLatin1Char('#') + QString::number(profileId);
}

QPair<QString, qint64> sessionOf(const SessionKey &key) { return { key.serial, qint64(key.session) }; }

//! The cached set for the profile, read from the database when not cached; false if that read
//! failed (then nothing is cached and the next call tries again).
bool withScoring(qint64 profileId, QSet<QPair<QString, qint64>> **set)
{
    const QString ck = cacheKey(profileId);
    auto it = s_withScoring.find(ck);
    if (it == s_withScoring.end()) {
        QSet<QPair<QString, qint64>> read;
        QSqlQuery q(DatabaseManager::instance().database());
        q.prepare(QStringLiteral("SELECT DISTINCT machine_serial, session_id FROM manual_scoring WHERE profile_id = ?"));
        q.addBindValue(profileId);
        if (!q.exec()) return false;
        while (q.next()) read.insert({ q.value(0).toString(), q.value(1).toLongLong() });
        it = s_withScoring.insert(ck, read);
    }
    *set = &it.value();
    return true;
}

bool run(QSqlQuery &q, const char *what)
{
    if (q.exec()) return true;
    qWarning() << "ManualScoringRepository:" << what << "failed:" << q.lastError().text();
    return false;
}

} // namespace

bool ManualScoringRepository::hasScoring(const SessionKey &key)
{
    QMutexLocker lock(&s_withScoringLock);
    QSet<QPair<QString, qint64>> *set = nullptr;
    return withScoring(key.profileId, &set) && set->contains(sessionOf(key));
}

QList<Edit> ManualScoringRepository::editsForSession(const SessionKey &key)
{
    QList<Edit> out;
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("SELECT id, kind, channel, new_channel, start_ms, end_ms, note, created_at "
                             "FROM manual_scoring WHERE ") + kWhereKey + QStringLiteral(" ORDER BY id"));
    bindKey(q, key);
    if (!run(q, "editsForSession")) return out;
    while (q.next()) {
        Edit e;
        e.id = q.value(0).toLongLong();
        e.key = key;
        e.kind = kindOf(q.value(1).toString());
        e.channel = q.value(2).toUInt();
        e.newChannel = q.value(3).toUInt();
        e.startMs = q.value(4).toLongLong();
        e.endMs = q.value(5).toLongLong();
        e.note = q.value(6).toString();
        e.createdAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
        out.append(e);
    }
    return out;
}

qint64 ManualScoringRepository::add(const Edit &e)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT INTO manual_scoring (profile_id, machine_serial, session_id, kind, channel, new_channel, "
                             "start_ms, end_ms, note, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    bindKey(q, e.key);
    q.addBindValue(kindName(e.kind));
    q.addBindValue(e.channel);
    q.addBindValue(e.newChannel);
    q.addBindValue(e.startMs);
    q.addBindValue(e.endMs);
    q.addBindValue(e.note);
    q.addBindValue((e.createdAt.isValid() ? e.createdAt : QDateTime::currentDateTime()).toString(Qt::ISODate));
    if (!run(q, "add")) return 0;
    QMutexLocker lock(&s_withScoringLock);
    QSet<QPair<QString, qint64>> *set = nullptr;
    if (withScoring(e.key.profileId, &set)) set->insert(sessionOf(e.key));
    return q.lastInsertId().toLongLong();
}

bool ManualScoringRepository::remove(qint64 id)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring WHERE id = ?"));
    q.addBindValue(id);
    const bool ok = run(q, "remove");
    QMutexLocker lock(&s_withScoringLock);
    s_withScoring.clear();   // which session it was is not known here: read again when asked
    return ok;
}

bool ManualScoringRepository::removeAllForSession(const SessionKey &key)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring WHERE ") + kWhereKey);
    bindKey(q, key);
    const bool ok = run(q, "removeAllForSession");
    QMutexLocker lock(&s_withScoringLock);
    QSet<QPair<QString, qint64>> *set = nullptr;
    if (ok && withScoring(key.profileId, &set)) set->remove(sessionOf(key));
    return ok;
}

bool ManualScoringRepository::storeSummary(const SessionKey &key, const ManualScoring::Result &r)
{
    QStringList deltas;
    for (auto it = r.delta.cbegin(); it != r.delta.cend(); ++it) deltas << QStringLiteral("%1:%2").arg(it.key()).arg(it.value());
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO manual_scoring_summary (profile_id, machine_serial, session_id, deltas, "
                             "excluded_ms, not_found) VALUES (?, ?, ?, ?, ?, ?)"));
    bindKey(q, key);
    q.addBindValue(deltas.join(QLatin1Char(',')));
    q.addBindValue(r.excludedMs);
    q.addBindValue(int(r.notFound.size()));
    return run(q, "storeSummary");
}

bool ManualScoringRepository::loadSummary(const SessionKey &key, QHash<ChannelID, int> &delta, qint64 &excludedMs, int &notFound)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("SELECT deltas, excluded_ms, not_found FROM manual_scoring_summary WHERE ") + kWhereKey);
    bindKey(q, key);
    if (!run(q, "loadSummary") || !q.next()) return false;
    delta.clear();
    for (const QString &part : q.value(0).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList kv = part.split(QLatin1Char(':'));
        if (kv.size() == 2) delta.insert(kv[0].toUInt(), kv[1].toInt());
    }
    excludedMs = q.value(1).toLongLong();
    notFound = q.value(2).toInt();
    return true;
}

bool ManualScoringRepository::removeSummary(const SessionKey &key)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring_summary WHERE ") + kWhereKey);
    bindKey(q, key);
    return run(q, "removeSummary");
}
