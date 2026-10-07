/* Menus of the manual scoring mode
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmenus.h"

#include <QDateTime>
#include <QMenu>
#include <QSet>
#include <QTreeWidgetItem>
#include <algorithm>

#include "SleepLib/day.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/manual_scoring_repository.h"

using ManualScoring::EffectiveEvent;
using ManualScoring::Origin;

namespace {

QVariantMap data(const char *action, ChannelID channel = 0, ChannelID newChannel = 0, qint64 timeMs = 0, qint64 editId = 0)
{
    return { { QStringLiteral("action"), QString::fromLatin1(action) }, { QStringLiteral("channel"), channel },
             { QStringLiteral("newChannel"), newChannel }, { QStringLiteral("timeMs"), timeMs }, { QStringLiteral("editId"), editId } };
}

} // namespace

QString ScoringMenus::typeName(ChannelID channel)
{
    if (channel == CPAP_Obstructive) return tr("Obstructive apnea");
    if (channel == CPAP_ClearAirway) return tr("Central apnea");
    if (channel == CPAP_Apnea) return tr("Apnea (unclassified)");
    if (channel == CPAP_Hypopnea) return tr("Hypopnea");
    return schema::channel[channel].label();
}

QMenu *ScoringMenus::forRange(qint64 durationMs, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("scoringRangeMenu"));
    menu->setTitle(tr("Selected: %1 s").arg(QLocale().toString(durationMs / 1000.0, 'g', 3)));
    const bool shortEvent = durationMs < qint64(ManualScoring::kShortEventSec * 1000);
    for (ChannelID c : ManualScoring::scoredChannels()) {
        const QString text = shortEvent ? tr("%1 — shorter than 10 s").arg(typeName(c)) : typeName(c);
        menu->addAction(text)->setData(data("add", c));
    }
    menu->addSeparator();
    menu->addAction(tr("Exclude stretch (noise / awake)"))->setData(data("exclude"));
    return menu;
}

QMenu *ScoringMenus::forEvent(const EffectiveEvent &e, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("scoringEventMenu"));
    // a device event is named by its own type, whatever it was changed to
    const ChannelID own = e.originalChannel;
    if (e.origin != Origin::Added) {
        if (e.origin != Origin::Removed) menu->addAction(tr("Remove event (do not count)"))->setData(data("remove", own, 0, e.endMs));
        QMenu *types = menu->addMenu(tr("Change type"));
        for (ChannelID c : ManualScoring::scoredChannels()) {
            if (c != e.channel) types->addAction(typeName(c))->setData(data("retype", own, c, e.endMs));
        }
    }
    if (e.origin != Origin::Device) {
        menu->addSeparator();
        menu->addAction(tr("Undo this change"))->setData(data("undo", own, 0, e.endMs, e.editId));
    }
    return menu;
}

QMenu *ScoringMenus::forExcluded(qint64 editId, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("scoringExcludedMenu"));
    menu->addAction(tr("Cancel exclusion"))->setData(data("undo", 0, 0, 0, editId));
    return menu;
}

QList<ScoringMenus::EditRow> ScoringMenus::editRows(Day *day)
{
    using ManualScoring::Kind;
    QList<EditRow> rows;
    if (!day) return rows;
    QSet<QPair<qint64, qint64>> stretches;
    for (Session *s : day->sessions) {
        if (s->type() != MT_CPAP || !s->enabled()) continue;   // a switched-off session's edits do not count
        const qint64 c = s->correctionMs();   // stored in device time, listed in graph time
        for (const ManualScoring::Edit &e : ManualScoringRepository::editsForSession(ManualScoring::keyOf(s))) {
            QString text;
            qint64 at = e.endMs + c;
            switch (e.kind) {
            case Kind::Add: text = tr("added: %1").arg(typeName(e.channel)); break;
            case Kind::Remove: text = tr("removed: %1").arg(typeName(e.channel)); break;
            case Kind::Retype: text = tr("%1 → %2").arg(typeName(e.channel), typeName(e.newChannel)); break;
            case Kind::Exclude: {
                const QPair<qint64, qint64> span(e.startMs, e.endMs);
                if (stretches.contains(span)) continue;
                stretches.insert(span);
                at = e.startMs + c;
                const qint64 seconds = (e.endMs - e.startMs) / 1000;
                text = seconds < 120 ? tr("excluded %1 s").arg(seconds) : tr("excluded %1 min").arg(qRound(seconds / 60.0));
                break;
            }
            }
            rows.append({ e.id, at, text });
        }
    }
    std::sort(rows.begin(), rows.end(), [](const EditRow &a, const EditRow &b) { return a.timeMs < b.timeMs; });
    return rows;
}

QTreeWidgetItem *ScoringMenus::treeNode(const QList<EditRow> &rows)
{
    if (rows.isEmpty()) return nullptr;
    auto *node = new QTreeWidgetItem(QStringList(tr("Manual scoring (%1)").arg(rows.size())), kTreeNodeType);
    for (const EditRow &r : rows) {
        const QString time = QDateTime::fromMSecsSinceEpoch(r.timeMs).time().toString(QStringLiteral("HH:mm:ss"));
        auto *item = new QTreeWidgetItem(QStringList(QStringLiteral("%1 — %2").arg(time, r.text)));
        item->setData(0, Qt::UserRole, r.timeMs);
        item->setData(0, kEditIdRole, r.editId);
        node->addChild(item);
    }
    return node;
}
