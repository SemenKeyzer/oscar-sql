/* TabButtons Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tabbuttonstests.h"

#include <QAbstractButton>
#include <QApplication>
#include <QTabBar>
#include <QTabWidget>

#include "tabbuttons.h"

void TabButtonsTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void TabButtonsTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

namespace {
QWidget *page(QTabWidget &tabs, const QString &name)
{
    auto *w = new QWidget(&tabs);
    w->setObjectName(name);
    tabs.addTab(w, name.toUpper());
    return w;
}
} // namespace

void TabButtonsTests::testButtonsFollowPages()
{
    QTabWidget tabs;
    QWidget *a = page(tabs, QStringLiteral("a")), *b = page(tabs, QStringLiteral("b")), *c = page(tabs, QStringLiteral("c"));
    TabButtons buttons(&tabs, { { a, b }, { c } });
    QVERIFY(tabs.tabBar()->isHidden());
    QVERIFY(buttons.button(a) && buttons.button(b) && buttons.button(c));
    QCOMPARE(buttons.button(c)->text(), QStringLiteral("C"));
    QCOMPARE(buttons.button(c)->objectName(), QStringLiteral("tabButton_c"));
    buttons.button(c)->click();
    QCOMPARE(tabs.currentWidget(), c);
    tabs.setCurrentWidget(b);
    QVERIFY(buttons.button(b)->isChecked());
    QVERIFY(!buttons.button(a)->isChecked() && !buttons.button(c)->isChecked());
}

void TabButtonsTests::testHidePage()
{
    QTabWidget tabs;
    QWidget *a = page(tabs, QStringLiteral("a")), *b = page(tabs, QStringLiteral("b"));
    TabButtons buttons(&tabs, { { a, b } });
    tabs.setCurrentWidget(b);
    buttons.setPageVisible(b, false);
    QVERIFY(buttons.button(b)->isHidden());
    QCOMPARE(tabs.currentWidget(), a);
    QVERIFY(buttons.button(a)->isChecked());
    buttons.setPageVisible(b, true);
    QVERIFY(!buttons.button(b)->isHidden());
}
