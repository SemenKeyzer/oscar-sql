/* Scoring Menus Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmenustests.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTreeWidgetItem>

#include "SleepLib/schema.h"
#include "scoringmenus.h"

using ManualScoring::EffectiveEvent;
using ManualScoring::Origin;

namespace {

//! The texts of the menu's actions (submenus by their titles), separators skipped.
QStringList texts(QMenu *menu)
{
    QStringList out;
    for (QAction *a : menu->actions()) {
        if (!a->isSeparator()) out << a->text().section(QLatin1Char('\t'), 0, 0);   // without the key column
    }
    return out;
}

QAction *find(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions()) {
        if (a->text().section(QLatin1Char('\t'), 0, 0) == text) return a;
        if (a->menu()) {
            if (QAction *in = find(a->menu(), text)) return in;
        }
    }
    return nullptr;
}

EffectiveEvent scoredEvent(Origin origin, ChannelID channel, ChannelID original, qint64 editId)
{
    return { channel, original, 300000, 11, origin, editId, false, 300000, 11, 0 };
}

} // namespace

void ScoringMenusTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    if (CPAP_Obstructive == 0) schema::init();
}

void ScoringMenusTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void ScoringMenusTests::testRangeMenu()
{
    QScopedPointer<QMenu> m(ScoringMenus::forRange(15000, nullptr));
    QCOMPARE(m->title(), QStringLiteral("Selected: 15 s"));
    QCOMPARE(texts(m.data()), QStringList({ QStringLiteral("Obstructive apnea"), QStringLiteral("Central apnea"),
                                            QStringLiteral("Apnea (unclassified)"), QStringLiteral("Hypopnea"),
                                            QStringLiteral("Exclude stretch (noise / awake)") }));
    const QVariantMap oa = find(m.data(), QStringLiteral("Obstructive apnea"))->data().toMap();
    QCOMPARE(oa.value(QStringLiteral("action")).toString(), QStringLiteral("add"));
    QCOMPARE(oa.value(QStringLiteral("channel")).toUInt(), CPAP_Obstructive);
    const QVariantMap ex = find(m.data(), QStringLiteral("Exclude stretch (noise / awake)"))->data().toMap();
    QCOMPARE(ex.value(QStringLiteral("action")).toString(), QStringLiteral("exclude"));
}

void ScoringMenusTests::testShortEventWarning()
{
    QScopedPointer<QMenu> shortMenu(ScoringMenus::forRange(9900, nullptr));
    QVERIFY(find(shortMenu.data(), QStringLiteral("Hypopnea — shorter than 10 s")));
    QVERIFY(find(shortMenu.data(), QStringLiteral("Exclude stretch (noise / awake)")));   // no warning for excluding
    QScopedPointer<QMenu> tenSeconds(ScoringMenus::forRange(10000, nullptr));
    QVERIFY(find(tenSeconds.data(), QStringLiteral("Hypopnea")));
}

void ScoringMenusTests::testEventMenu()
{
    QScopedPointer<QMenu> m(ScoringMenus::forEvent(scoredEvent(Origin::Device, CPAP_ClearAirway, CPAP_ClearAirway, 0), nullptr));
    QCOMPARE(texts(m.data()), QStringList({ QStringLiteral("Remove event (do not count)"), QStringLiteral("Change type") }));
    const QVariantMap remove = find(m.data(), QStringLiteral("Remove event (do not count)"))->data().toMap();
    QCOMPARE(remove.value(QStringLiteral("action")).toString(), QStringLiteral("remove"));
    QCOMPARE(remove.value(QStringLiteral("channel")).toUInt(), CPAP_ClearAirway);
    QCOMPARE(remove.value(QStringLiteral("timeMs")).toLongLong(), qint64(300000));
    QMenu *types = find(m.data(), QStringLiteral("Change type"))->menu();
    QCOMPARE(texts(types), QStringList({ QStringLiteral("Obstructive apnea"), QStringLiteral("Apnea (unclassified)"), QStringLiteral("Hypopnea") }));
    const QVariantMap retype = types->actions().first()->data().toMap();
    QCOMPARE(retype.value(QStringLiteral("action")).toString(), QStringLiteral("retype"));
    QCOMPARE(retype.value(QStringLiteral("channel")).toUInt(), CPAP_ClearAirway);
    QCOMPARE(retype.value(QStringLiteral("newChannel")).toUInt(), CPAP_Obstructive);
}

void ScoringMenusTests::testUndoOnlyForEdited()
{
    for (Origin o : { Origin::Added, Origin::Removed, Origin::Retyped }) {
        QScopedPointer<QMenu> m(ScoringMenus::forEvent(scoredEvent(o, CPAP_Obstructive, CPAP_ClearAirway, 42), nullptr));
        QAction *undo = find(m.data(), QStringLiteral("Undo this change"));
        QVERIFY(undo);
        QCOMPARE(undo->data().toMap().value(QStringLiteral("action")).toString(), QStringLiteral("undo"));
        QCOMPARE(undo->data().toMap().value(QStringLiteral("editId")).toLongLong(), qint64(42));
    }
    QScopedPointer<QMenu> device(ScoringMenus::forEvent(scoredEvent(Origin::Device, CPAP_Obstructive, CPAP_Obstructive, 0), nullptr));
    QVERIFY(!find(device.data(), QStringLiteral("Undo this change")));
    // a retyped event is changed again from its device type
    QScopedPointer<QMenu> retyped(ScoringMenus::forEvent(scoredEvent(Origin::Retyped, CPAP_Obstructive, CPAP_ClearAirway, 42), nullptr));
    QCOMPARE(find(retyped.data(), QStringLiteral("Remove event (do not count)"))->data().toMap().value(QStringLiteral("channel")).toUInt(),
             CPAP_ClearAirway);
}

void ScoringMenusTests::testExcludedMenu()
{
    QScopedPointer<QMenu> m(ScoringMenus::forExcluded(7, nullptr));
    QCOMPARE(texts(m.data()), QStringList({ QStringLiteral("Cancel exclusion") }));
    QCOMPARE(m->actions().first()->data().toMap().value(QStringLiteral("editId")).toLongLong(), qint64(7));
    QCOMPARE(m->actions().first()->data().toMap().value(QStringLiteral("action")).toString(), QStringLiteral("undo"));
}

void ScoringMenusTests::testTreeNode()
{
    const QList<ScoringMenus::EditRow> rows = { { 7, 1000000, QStringLiteral("removed: Obstructive apnea") },
                                                { 9, 2000000, QStringLiteral("excluded 1 min") } };
    QScopedPointer<QTreeWidgetItem> node(ScoringMenus::treeNode(rows));
    QCOMPARE(node->text(0), QStringLiteral("Manual scoring (2)"));
    QCOMPARE(node->type(), ScoringMenus::kTreeNodeType);
    QCOMPARE(node->childCount(), 2);
    QVERIFY(node->child(1)->text(0).endsWith(QStringLiteral("excluded 1 min")));
    QCOMPARE(node->child(1)->data(0, Qt::UserRole).toLongLong(), qint64(2000000));
    QCOMPARE(node->child(1)->data(0, ScoringMenus::kEditIdRole).toLongLong(), qint64(9));
    QVERIFY(!ScoringMenus::treeNode({}));   // nothing to list: no node
}

// one letter picks an item, on the Latin and on the Russian layout (the same key)
void ScoringMenusTests::testLetterKeys()
{
    QScopedPointer<QMenu> range(ScoringMenus::forRange(15000, nullptr));
    QCOMPARE(find(range.data(), QStringLiteral("Hypopnea"))->text(), QStringLiteral("Hypopnea\tH"));
    QCOMPARE(ScoringMenus::actionForKey(range.data(), QStringLiteral("h")), find(range.data(), QStringLiteral("Hypopnea")));
    QCOMPARE(ScoringMenus::actionForKey(range.data(), QStringLiteral("р")), find(range.data(), QStringLiteral("Hypopnea")));   // Russian layout
    QCOMPARE(ScoringMenus::actionForKey(range.data(), QStringLiteral("O")), find(range.data(), QStringLiteral("Obstructive apnea")));
    QCOMPARE(ScoringMenus::actionForKey(range.data(), QStringLiteral("x")), find(range.data(), QStringLiteral("Exclude stretch (noise / awake)")));
    QVERIFY(!ScoringMenus::actionForKey(range.data(), QStringLiteral("z")));

    QScopedPointer<QMenu> ev(ScoringMenus::forEvent(scoredEvent(Origin::Retyped, CPAP_Obstructive, CPAP_ClearAirway, 42), nullptr));
    QCOMPARE(ScoringMenus::actionForKey(ev.data(), QStringLiteral("r")), find(ev.data(), QStringLiteral("Remove event (do not count)")));
    QAction *type = ScoringMenus::actionForKey(ev.data(), QStringLiteral("е"));   // T on the Russian layout
    QVERIFY(type && type->menu());
    QCOMPARE(ScoringMenus::actionForKey(type->menu(), QStringLiteral("c")), find(type->menu(), QStringLiteral("Central apnea")));
    QCOMPARE(ScoringMenus::actionForKey(ev.data(), QStringLiteral("u")), find(ev.data(), QStringLiteral("Undo this change")));

    // pressed in the open menu, the letter chooses the item
    QScopedPointer<QMenu> open(ScoringMenus::forRange(15000, nullptr));
    QSignalSpy chosen(open.data(), &QMenu::triggered);
    open->popup(QPoint(10, 10));
    QKeyEvent press(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    QCoreApplication::sendEvent(open.data(), &press);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.first().first().value<QAction *>(), find(open.data(), QStringLiteral("Hypopnea")));

    // with a modifier it is a shortcut, not a letter: Cmd+C does not pick "Central apnea"
    QScopedPointer<QMenu> again(ScoringMenus::forRange(15000, nullptr));
    QSignalSpy none(again.data(), &QMenu::triggered);
    again->popup(QPoint(10, 10));
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("c"));
    QCoreApplication::sendEvent(again.data(), &copy);
    QCOMPARE(none.count(), 0);
}

void ScoringMenusTests::testBulkItems()
{
    QScopedPointer<QMenu> m(ScoringMenus::forEvent(scoredEvent(Origin::Device, CPAP_Obstructive, CPAP_Obstructive, 0), nullptr, 77));
    QAction *all = find(m.data(), QStringLiteral("Change type of all Obstructive apnea (77)"));
    QVERIFY(all && all->menu());
    const QVariantMap toH = find(all->menu(), QStringLiteral("Hypopnea"))->data().toMap();
    QCOMPARE(toH.value(QStringLiteral("action")).toString(), QStringLiteral("retypeAll"));
    QCOMPARE(toH.value(QStringLiteral("channel")).toUInt(), CPAP_Obstructive);
    QCOMPARE(toH.value(QStringLiteral("newChannel")).toUInt(), CPAP_Hypopnea);
    const QVariantMap removeAll = find(m.data(), QStringLiteral("Remove all Obstructive apnea (77)"))->data().toMap();
    QCOMPARE(removeAll.value(QStringLiteral("action")).toString(), QStringLiteral("removeAll"));
    // a single event of its type: nothing to do in bulk
    QScopedPointer<QMenu> one(ScoringMenus::forEvent(scoredEvent(Origin::Device, CPAP_Obstructive, CPAP_Obstructive, 0), nullptr, 1));
    QVERIFY(!find(one.data(), QStringLiteral("Remove all Obstructive apnea (1)")));
}

// Excluded time reads in seconds under two minutes: 17 s is not "0 min".
void ScoringMenusTests::testExcludedText()
{
    QCOMPARE(ScoringMenus::excludedText(17000), QStringLiteral("excluded 17 s"));
    QCOMPARE(ScoringMenus::excludedText(119000), QStringLiteral("excluded 119 s"));
    QCOMPARE(ScoringMenus::excludedText(14 * 60000 + 20000), QStringLiteral("excluded 14 min"));
}

// an event given new bounds is still named by its own end when removed or retyped
void ScoringMenusTests::testResizedEventNamedByOwnEnd()
{
    EffectiveEvent e = scoredEvent(Origin::Device, CPAP_Obstructive, CPAP_Obstructive, 0);
    e.endMs = 306000;   // dragged 6 s later
    e.durationSec = 17;
    e.resizeEditId = 5;
    QScopedPointer<QMenu> m(ScoringMenus::forEvent(e, nullptr));
    QCOMPARE(find(m.data(), QStringLiteral("Remove event (do not count)"))->data().toMap().value(QStringLiteral("timeMs")).toLongLong(),
             qint64(300000));
    QAction *toH = find(m.data(), ScoringMenus::typeName(CPAP_Hypopnea));
    QVERIFY(toH);
    QCOMPARE(toH->data().toMap().value(QStringLiteral("timeMs")).toLongLong(), qint64(300000));
}
