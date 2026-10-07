/* Hover Explanations Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helptipstests.h"

#include <QApplication>
#include <QHelpEvent>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTimeEdit>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolTip>

#include "SleepLib/schema.h"
#include "glossary.h"
#include "helptips.h"

void HelpTipsTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    if (CPAP_Obstructive == 0) schema::init();
    HelpTips::instance()->setEnabled(true);
}

void HelpTipsTests::cleanupTestCase()
{
    HelpTips::instance()->setEnabled(true);
    delete m_app;
    m_app = nullptr;
}

void HelpTipsTests::testAttachShowsTooltip()
{
    QLabel label(QStringLiteral("AHI"));
    label.resize(100, 30);
    label.show();
    HelpTips::attach(&label, QStringLiteral("ahi"));
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);

    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&label, &enter);
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(hovered.first().first().toString(), QStringLiteral("ahi"));

    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), label.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&label, &help);
    QVERIFY2(QToolTip::text().contains(QStringLiteral("AHI")), qPrintable(QToolTip::text()));
    QToolTip::hideText();
}

void HelpTipsTests::testDisabled()
{
    QLabel label(QStringLiteral("AHI"));
    label.show();
    HelpTips::attach(&label, QStringLiteral("ahi"));
    HelpTips::instance()->setEnabled(false);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&label, &enter);
    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), label.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&label, &help);
    QCOMPARE(hovered.count(), 0);
    QVERIFY(!QToolTip::text().contains(QStringLiteral("Apnea-Hypopnea")));
    QCOMPARE(HelpTips::term(QStringLiteral("AHI"), QStringLiteral("ahi")), QStringLiteral("AHI"));
    HelpTips::instance()->setEnabled(true);
}

void HelpTipsTests::testTerm()
{
    const QString t = HelpTips::term(QStringLiteral("Leak"), QStringLiteral("leak"));
    QVERIFY(t.contains(QStringLiteral("href='help:leak'")));
    QVERIFY(t.contains(QStringLiteral(">Leak</a>")));
    QVERIFY(t.contains(QStringLiteral("text-decoration:none")));
    QCOMPARE(HelpTips::term(QStringLiteral("x"), QStringLiteral("no_such_key")), QStringLiteral("x"));
    QCOMPARE(HelpTips::keyOf(QUrl(QStringLiteral("help:leak"))), QStringLiteral("leak"));
    QVERIFY(HelpTips::keyOf(QUrl(QStringLiteral("daily=2026-10-01"))).isEmpty());
}

// The explanation goes into a title attribute: quotes, < and % must not break the page. The
// text is the page's own HTML, already escaped.
void HelpTipsTests::testTermEscapes()
{
    const QString page = QStringLiteral("<p>%1 and more</p>").arg(HelpTips::term(QStringLiteral("SpO2 &lt; 90%"), QStringLiteral("t90")));
    QTextDocument doc;
    doc.setHtml(page);
    QCOMPARE(doc.toPlainText(), QStringLiteral("SpO2 < 90% and more"));
    int anchors = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isAnchor()) {
                ++anchors;
                QCOMPARE(it.fragment().charFormat().anchorHref(), QStringLiteral("help:t90"));
                QVERIFY(it.fragment().charFormat().toolTip().contains(QStringLiteral("90")));
            }
        }
    }
    QCOMPARE(anchors, 1);
}

namespace {
// entries of the controls' table used by these tests
const QString kPrefsKey = QStringLiteral("ui.prefs.timeEdit");
const QString kMenuKey = QStringLiteral("ui.menu.actionPurgeCurrentDayAll");
}

void HelpTipsTests::testAttachAllByObjectName()
{
    QWidget root;
    auto *known = new QTimeEdit(&root);
    known->setObjectName(QStringLiteral("timeEdit"));
    auto *unknown = new QCheckBox(&root);
    unknown->setObjectName(QStringLiteral("noSuchSetting"));
    QCOMPARE(HelpTips::attachAll(&root, QStringLiteral("prefs")), 1);
    QCOMPARE(known->property("helpKey").toString(), kPrefsKey);
    QVERIFY(unknown->property("helpKey").toString().isEmpty());
    QCOMPARE(HelpTips::keyFor(known), kPrefsKey);
}

void HelpTipsTests::testLabelTakesBuddyKey()
{
    QWidget root;
    auto *field = new QTimeEdit(&root);
    field->setObjectName(QStringLiteral("timeEdit"));
    auto *label = new QLabel(QStringLiteral("Day Split Time"), &root);
    label->setObjectName(QStringLiteral("label_2"));
    label->setBuddy(field);
    HelpTips::attachAll(&root, QStringLiteral("prefs"));
    QCOMPARE(label->property("helpKey").toString(), kPrefsKey);
}

void HelpTipsTests::testLabelTakesNeighbourKey()
{
    // a label without a buddy explains the control after it in its row
    QWidget box;
    auto *row = new QHBoxLayout(&box);
    auto *label = new QLabel(QStringLiteral("Day Split Time"), &box);
    auto *field = new QTimeEdit(&box);
    field->setObjectName(QStringLiteral("timeEdit"));
    row->addWidget(label);
    row->addWidget(field);

    QWidget grid;
    auto *g = new QGridLayout(&grid);
    auto *gridLabel = new QLabel(QStringLiteral("Day Split Time"), &grid);
    auto *other = new QLabel(QStringLiteral("other row"), &grid);
    auto *gridField = new QTimeEdit(&grid);
    gridField->setObjectName(QStringLiteral("timeEdit"));
    g->addWidget(gridLabel, 0, 0);
    g->addWidget(gridField, 0, 1);
    g->addWidget(other, 1, 0);

    HelpTips::attachAll(&box, QStringLiteral("prefs"));
    HelpTips::attachAll(&grid, QStringLiteral("prefs"));
    QCOMPARE(label->property("helpKey").toString(), kPrefsKey);
    QCOMPARE(gridLabel->property("helpKey").toString(), kPrefsKey);
    QVERIFY(other->property("helpKey").toString().isEmpty());
}

void HelpTipsTests::testLabelStopsAtSpacer()
{
    // a section header followed by a stretch does not explain the control after the gap
    QWidget box;
    auto *row = new QHBoxLayout(&box);
    auto *header = new QLabel(QStringLiteral("Journal"), &box);
    auto *field = new QTimeEdit(&box);
    field->setObjectName(QStringLiteral("timeEdit"));
    row->addWidget(header);
    row->addStretch(1);
    row->addWidget(field);
    HelpTips::attachAll(&box, QStringLiteral("prefs"));
    QVERIFY(header->property("helpKey").toString().isEmpty());
    QCOMPARE(field->property("helpKey").toString(), kPrefsKey);
}

void HelpTipsTests::testMenuHoverFollowsAction()
{
    QMainWindow win;
    QMenu *menu = win.menuBar()->addMenu(QStringLiteral("Data"));
    QAction *action = menu->addAction(QStringLiteral("All including Notes"));
    action->setObjectName(QStringLiteral("actionPurgeCurrentDayAll"));
    HelpTips::attachMenus(&win);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    emit menu->hovered(action);
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(hovered.first().first().toString(), kMenuKey);

    HelpTips::instance()->setEnabled(false);
    emit menu->hovered(action);
    QCOMPARE(hovered.count(), 1);
    HelpTips::instance()->setEnabled(true);
}

void HelpTipsTests::testMenuHoverSkipsDynamicItems()
{
    QMainWindow win;
    QMenu *menu = win.menuBar()->addMenu(QStringLiteral("File"));
    QMenu *recent = menu->addMenu(QStringLiteral("Recent"));
    QAction *dynamic = recent->addAction(QStringLiteral("/some/folder"));   // no object name
    HelpTips::attachMenus(&win);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    emit recent->hovered(dynamic);
    QCOMPARE(hovered.count(), 0);
}

void HelpTipsTests::testSubmenuHoverExplainsMenu()
{
    // a submenu whose items are made in code (one per device) is explained by its own name
    QMainWindow win;
    QMenu *data = win.menuBar()->addMenu(QStringLiteral("Data"));
    QMenu *rebuild = data->addMenu(QStringLiteral("Rebuild CPAP Data"));
    rebuild->setObjectName(QStringLiteral("menu_Rebuild_CPAP_Data"));
    HelpTips::attachMenus(&win);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    emit data->hovered(rebuild->menuAction());
    QCOMPARE(hovered.count(), 1);
    QCOMPARE(hovered.first().first().toString(), QStringLiteral("ui.menu.menu_Rebuild_CPAP_Data"));
}

void HelpTipsTests::testMenuTooltipUsesActiveAction()
{
    QMenu menu;
    QAction *action = menu.addAction(QStringLiteral("All including Notes"));
    action->setObjectName(QStringLiteral("actionPurgeCurrentDayAll"));
    menu.show();
    menu.setActiveAction(action);
    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), menu.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&menu, &help);
    const QString term = Glossary::find(kMenuKey)->term;
    QVERIFY2(QToolTip::text().contains(term), qPrintable(QToolTip::text()));
    QToolTip::hideText();
    menu.hide();
}

// With the explanations off nothing reports a hover, whoever asks (the Daily event rows too).
void HelpTipsTests::testHoverSilentWhenOff()
{
    HelpTips::instance()->setEnabled(false);
    QSignalSpy hovered(HelpTips::instance(), &HelpTips::hovered);
    HelpTips::instance()->hover(QStringLiteral("ahi"));
    QCOMPARE(hovered.count(), 0);
    HelpTips::instance()->setEnabled(true);
    HelpTips::instance()->hover(QStringLiteral("ahi"));
    QCOMPARE(hovered.count(), 1);
}
