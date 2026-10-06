/* Scoring Menus Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "scoringmenustests.h"

#include <QApplication>
#include <QMenu>

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
        if (!a->isSeparator()) out << a->text();
    }
    return out;
}

QAction *find(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions()) {
        if (a->text() == text) return a;
        if (a->menu()) {
            if (QAction *in = find(a->menu(), text)) return in;
        }
    }
    return nullptr;
}

EffectiveEvent scoredEvent(Origin origin, ChannelID channel, ChannelID original, qint64 editId)
{
    return { channel, original, 300000, 11, origin, editId, false };
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
