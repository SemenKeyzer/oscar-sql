/* Help Strip Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helpstriptests.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

#include "glossary.h"
#include "helpstrip.h"
#include "helptips.h"

namespace {
const QString kPrefsKey = QStringLiteral("ui.prefs.timeEdit");
const QString kMenuKey = QStringLiteral("ui.menu.actionPurgeCurrentDayAll");
}

void HelpStripTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    HelpTips::instance()->setEnabled(true);
}

void HelpStripTests::cleanupTestCase()
{
    HelpTips::instance()->setEnabled(true);
    delete m_app;
    m_app = nullptr;
}

void HelpStripTests::testEmptyStateText()
{
    HelpStrip strip({ QStringLiteral("ui.prefs.") }, nullptr);
    QVERIFY(strip.key().isEmpty());
    QVERIFY(strip.toPlainText().contains(QStringLiteral("Hover over a setting")));
    QCOMPARE(strip.objectName(), QStringLiteral("helpStrip"));
}

void HelpStripTests::testShowsHoveredEntry()
{
    HelpStrip strip({ QStringLiteral("ui.prefs.") }, nullptr);
    HelpTips::instance()->hover(kPrefsKey);
    QCOMPARE(strip.key(), kPrefsKey);
    QVERIFY(strip.toPlainText().contains(Glossary::find(kPrefsKey)->term));
}

void HelpStripTests::testKeepsEntryAfterLeave()
{
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *setting = new QCheckBox(QStringLiteral("setting"), &host);
    auto *plain = new QLabel(QStringLiteral("no explanation"), &host);
    auto *strip = new HelpStrip({ QStringLiteral("ui.prefs.") }, &host);
    layout->addWidget(setting);
    layout->addWidget(plain);
    layout->addWidget(strip);
    HelpTips::attach(setting, kPrefsKey);
    host.show();
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(setting, &enter);
    QCOMPARE(strip->key(), kPrefsKey);
    // the mouse moves off the setting onto a widget without an explanation
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(setting, &leave);
    QApplication::sendEvent(plain, &enter);
    QCOMPARE(strip->key(), kPrefsKey);
    QVERIFY(strip->toPlainText().contains(Glossary::find(kPrefsKey)->term));
}

void HelpStripTests::testStripIgnoresOtherWindows()
{
    HelpStrip strip({ QStringLiteral("ui.prefs.") }, nullptr);
    HelpTips::instance()->hover(kPrefsKey);
    HelpTips::instance()->hover(QStringLiteral("ahi"));
    HelpTips::instance()->hover(kMenuKey);
    QCOMPARE(strip.key(), kPrefsKey);
}

void HelpStripTests::testStripHiddenWhenOff()
{
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *strip = new HelpStrip({ QStringLiteral("ui.prefs.") }, &host);
    layout->addWidget(strip);
    host.show();
    QVERIFY(strip->isVisibleTo(&host));
    HelpTips::instance()->setEnabled(false);
    QVERIFY(!strip->isVisibleTo(&host));
    HelpTips::instance()->setEnabled(true);
    QVERIFY(strip->isVisibleTo(&host));
}

// The strip goes between the dialog's content and its OK/Cancel row, not under the buttons.
void HelpStripTests::testPlacedAboveButtons()
{
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *content = new QLabel(QStringLiteral("content"), &host);
    auto *buttons = new QWidget(&host);
    layout->addWidget(content);
    layout->addWidget(buttons);
    auto *strip = new HelpStrip({ QStringLiteral("ui.prefs.") }, &host);
    HelpStrip::placeAbove(layout, buttons, strip);
    QCOMPARE(layout->indexOf(strip), 1);
    QCOMPARE(layout->indexOf(buttons), 2);
}
