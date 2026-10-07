/* Menus of the manual scoring mode
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmenus.h"

#include <QScopedPointer>
#include <QLocale>
#include <QDateTime>
#include <QKeyEvent>
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

// the letter of each choice, shown in the menu's key column
const char *const kKeyProperty = "scoringKey";

QChar keyOf(ChannelID c)
{
    if (c == CPAP_Obstructive) return QLatin1Char('O');
    if (c == CPAP_ClearAirway) return QLatin1Char('C');
    if (c == CPAP_Apnea) return QLatin1Char('A');
    if (c == CPAP_Hypopnea) return QLatin1Char('H');
    return QChar();
}

//! An item with its letter key in the key column ("Hypopnea\tH").
QAction *keyed(QMenu *menu, const QString &text, QChar key)
{
    QAction *a = menu->addAction(key.isNull() ? text : text + QLatin1Char('\t') + key);
    if (!key.isNull()) a->setProperty(kKeyProperty, QString(key));
    return a;
}

//! Letter keys pick an item of an open scoring menu (macOS shows no mnemonics).
class LetterKeys : public QObject
{
  public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        auto *menu = qobject_cast<QMenu *>(o);
        if (!menu || e->type() != QEvent::KeyPress) return false;
        auto *key = static_cast<QKeyEvent *>(e);
        // with Ctrl, Cmd or Option it is a shortcut, not a letter
        if (key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) return false;
        QAction *a = ScoringMenus::actionForKey(menu, key->text());
        if (!a || !a->isEnabled()) return false;
        // as if the item were highlighted and Return (or Right, for a submenu) pressed
        menu->setActiveAction(a);
        QKeyEvent go(QEvent::KeyPress, a->menu() ? Qt::Key_Right : Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(menu, &go);
        return true;
    }
};

void installLetterKeys(QMenu *menu)
{
    menu->installEventFilter(new LetterKeys(menu));
    for (QAction *a : menu->actions()) {
        if (a->menu()) installLetterKeys(a->menu());
    }
}

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
        keyed(menu, text, keyOf(c))->setData(data("add", c));
    }
    menu->addSeparator();
    keyed(menu, tr("Exclude stretch (noise / awake)"), QLatin1Char('X'))->setData(data("exclude"));
    installLetterKeys(menu);
    return menu;
}

QMenu *ScoringMenus::forEvent(const EffectiveEvent &e, QWidget *parent, int countOfType)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("scoringEventMenu"));
    // a device event is named by its own type, whatever it was changed to
    const ChannelID own = e.originalChannel;
    if (e.origin != Origin::Added) {
        if (e.origin != Origin::Removed) keyed(menu, tr("Remove event (do not count)"), QLatin1Char('R'))->setData(data("remove", own, 0, e.endMs));
        QMenu *types = menu->addMenu(tr("Change type"));
        types->menuAction()->setText(tr("Change type") + QStringLiteral("\tT"));
        types->menuAction()->setProperty(kKeyProperty, QStringLiteral("T"));
        for (ChannelID c : ManualScoring::scoredChannels()) {
            if (c != e.channel) keyed(types, typeName(c), keyOf(c))->setData(data("retype", own, c, e.endMs));
        }
    }
    if (e.origin != Origin::Device) {
        menu->addSeparator();
        keyed(menu, tr("Undo this change"), QLatin1Char('U'))->setData(data("undo", own, 0, e.endMs, e.editId));
    }
    // every event of this type at once
    if (e.origin != Origin::Removed && countOfType > 1) {
        menu->addSeparator();
        QMenu *all = menu->addMenu(tr("Change type of all %1 (%2)").arg(typeName(e.channel)).arg(countOfType));
        for (ChannelID c : ManualScoring::scoredChannels()) {
            if (c != e.channel) all->addAction(typeName(c))->setData(data("retypeAll", e.channel, c));
        }
        menu->addAction(tr("Remove all %1 (%2)").arg(typeName(e.channel)).arg(countOfType))->setData(data("removeAll", e.channel));
    }
    installLetterKeys(menu);
    return menu;
}

QMenu *ScoringMenus::forExcluded(qint64 editId, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("scoringExcludedMenu"));
    keyed(menu, tr("Cancel exclusion"), QLatin1Char('U'))->setData(data("undo", 0, 0, 0, editId));
    installLetterKeys(menu);
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
        QScopedPointer<ManualScoring::Result> result;   // for new bounds: the lengths before and after
        for (const ManualScoring::Edit &e : ManualScoringRepository::editsForSession(ManualScoring::keyOf(s))) {
            QString text;
            qint64 at = e.endMs + c;
            switch (e.kind) {
            case Kind::Add: text = tr("added: %1").arg(typeName(e.channel)); break;
            case Kind::Remove: text = tr("removed: %1").arg(typeName(e.channel)); break;
            case Kind::Retype: text = tr("%1 → %2").arg(typeName(e.channel), typeName(e.newChannel)); break;
            case Kind::Resize: {
                if (!result) result.reset(new ManualScoring::Result(ManualScoring::resultFor(s)));
                QString before = QStringLiteral("?"), after = QLocale().toString((e.endMs - e.startMs) / 1000.0, 'f', 1);
                for (const ManualScoring::EffectiveEvent &ev : result->events) {
                    if (ev.resizeEditId == e.id) before = QLocale().toString(ev.originalDurationSec, 'f', 1);
                }
                text = tr("%1: %2 → %3 s").arg(schema::channel[e.channel].label(), before, after);
                break;
            }
            case Kind::Exclude: {
                const QPair<qint64, qint64> span(e.startMs, e.endMs);
                if (stretches.contains(span)) continue;
                stretches.insert(span);
                at = e.startMs + c;
                text = excludedText(e.endMs - e.startMs);
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

QAction *ScoringMenus::actionForKey(QMenu *menu, const QString &text)
{
    if (!menu || text.size() != 1) return nullptr;
    QChar key = text.at(0).toUpper();
    // the same keys on the Russian (ЙЦУКЕН) layout
    static const QHash<QChar, QChar> russian {
        { QChar(0x0429), QLatin1Char('O') }, { QChar(0x0421), QLatin1Char('C') }, { QChar(0x0424), QLatin1Char('A') },
        { QChar(0x0420), QLatin1Char('H') }, { QChar(0x0427), QLatin1Char('X') }, { QChar(0x041A), QLatin1Char('R') },
        { QChar(0x0415), QLatin1Char('T') }, { QChar(0x0413), QLatin1Char('U') },
    };
    key = russian.value(key, key);
    for (QAction *a : menu->actions()) {
        if (a->property(kKeyProperty).toString() == QString(key)) return a;
    }
    return nullptr;
}

QString ScoringMenus::excludedText(qint64 ms)
{
    const qint64 seconds = ms / 1000;
    return seconds < 120 ? tr("excluded %1 s").arg(seconds) : tr("excluded %1 min").arg(qRound(seconds / 60.0));
}
