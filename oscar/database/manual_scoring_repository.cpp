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
#include <QStringList>

#include "database/database_manager.h"

using ManualScoring::Edit;
using ManualScoring::Kind;

namespace {

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Add: return QStringLiteral("add");
    case Kind::Remove: return QStringLiteral("remove");
    case Kind::Retype: return QStringLiteral("retype");
    case Kind::Exclude: return QStringLiteral("exclude");
    }
    return QString();
}

Kind kindOf(const QString &name)
{
    if (name == QLatin1String("remove")) return Kind::Remove;
    if (name == QLatin1String("retype")) return Kind::Retype;
    if (name == QLatin1String("exclude")) return Kind::Exclude;
    return Kind::Add;
}

QString rowList(const QList<qint64> &rows)
{
    QStringList ids;
    for (qint64 r : rows) ids << QString::number(r);
    return ids.join(QLatin1Char(','));
}

bool run(QSqlQuery &q, const char *what)
{
    if (q.exec()) return true;
    qWarning() << "ManualScoringRepository:" << what << "failed:" << q.lastError().text();
    return false;
}

} // namespace

QList<Edit> ManualScoringRepository::editsForSessions(const QList<qint64> &sessionRows)
{
    QList<Edit> out;
    if (sessionRows.isEmpty()) return out;
    QSqlQuery q(DatabaseManager::instance().database());
    // the ids are numbers made here, not user text
    q.prepare(QStringLiteral("SELECT id, session_id, kind, channel, new_channel, start_ms, end_ms, note, created_at "
                             "FROM manual_scoring WHERE session_id IN (%1) ORDER BY id").arg(rowList(sessionRows)));
    if (!run(q, "editsForSessions")) return out;
    while (q.next()) {
        Edit e;
        e.id = q.value(0).toLongLong();
        e.sessionRow = q.value(1).toLongLong();
        e.kind = kindOf(q.value(2).toString());
        e.channel = q.value(3).toUInt();
        e.newChannel = q.value(4).toUInt();
        e.startMs = q.value(5).toLongLong();
        e.endMs = q.value(6).toLongLong();
        e.note = q.value(7).toString();
        e.createdAt = QDateTime::fromString(q.value(8).toString(), Qt::ISODate);
        out.append(e);
    }
    return out;
}

QList<Edit> ManualScoringRepository::editsForSession(qint64 sessionRow) { return editsForSessions({ sessionRow }); }

qint64 ManualScoringRepository::add(const Edit &e)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT INTO manual_scoring (session_id, kind, channel, new_channel, start_ms, end_ms, note, created_at) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.sessionRow);
    q.addBindValue(kindName(e.kind));
    q.addBindValue(e.channel);
    q.addBindValue(e.newChannel);
    q.addBindValue(e.startMs);
    q.addBindValue(e.endMs);
    q.addBindValue(e.note);
    q.addBindValue((e.createdAt.isValid() ? e.createdAt : QDateTime::currentDateTime()).toString(Qt::ISODate));
    return run(q, "add") ? q.lastInsertId().toLongLong() : 0;
}

bool ManualScoringRepository::remove(qint64 id)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring WHERE id = ?"));
    q.addBindValue(id);
    return run(q, "remove");
}

bool ManualScoringRepository::removeAllForSessions(const QList<qint64> &sessionRows)
{
    if (sessionRows.isEmpty()) return true;
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring WHERE session_id IN (%1)").arg(rowList(sessionRows)));
    return run(q, "removeAllForSessions");
}

bool ManualScoringRepository::storeSummary(qint64 sessionRow, const ManualScoring::Result &r)
{
    QStringList deltas;
    for (auto it = r.delta.cbegin(); it != r.delta.cend(); ++it) deltas << QStringLiteral("%1:%2").arg(it.key()).arg(it.value());
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO manual_scoring_summary (session_id, deltas, excluded_ms, not_found) VALUES (?, ?, ?, ?)"));
    q.addBindValue(sessionRow);
    q.addBindValue(deltas.join(QLatin1Char(',')));
    q.addBindValue(r.excludedMs);
    q.addBindValue(int(r.notFound.size()));
    return run(q, "storeSummary");
}

bool ManualScoringRepository::loadSummary(qint64 sessionRow, QHash<ChannelID, int> &delta, qint64 &excludedMs, int &notFound)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("SELECT deltas, excluded_ms, not_found FROM manual_scoring_summary WHERE session_id = ?"));
    q.addBindValue(sessionRow);
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

bool ManualScoringRepository::removeSummary(qint64 sessionRow)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral("DELETE FROM manual_scoring_summary WHERE session_id = ?"));
    q.addBindValue(sessionRow);
    return run(q, "removeSummary");
}
