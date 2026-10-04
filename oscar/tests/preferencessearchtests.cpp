/* Preferences Search Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "preferencessearchtests.h"

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include "preferencessearch.h"

using namespace PreferencesSearch;

namespace {

// Two tabs: a leak setting with a tooltip, a label and a hidden setting; then, inside a
// scroll area, a group with a pulse setting known by its tooltip, and a channel list.
struct Dialog {
    QTabWidget tabs;
    QCheckBox *redline = nullptr;
    QCheckBox *hidden = nullptr;
    QCheckBox *pulse = nullptr;
    QTreeView *channels = nullptr;

    Dialog()
    {
        auto *cpap = new QWidget;
        auto *cpapLayout = new QVBoxLayout(cpap);
        redline = new QCheckBox(QStringLiteral("Показывать красную &линию утечки"));
        redline->setToolTip(QStringLiteral("<p>Линия на графике <b>утечки</b></p>"));
        cpapLayout->addWidget(redline);
        cpapLayout->addWidget(new QLabel(QStringLiteral("Порог")));
        hidden = new QCheckBox(QStringLiteral("Скрытая настройка утечки"));
        cpapLayout->addWidget(hidden);
        hidden->setVisible(false);
        tabs.addTab(cpap, QStringLiteral("CPAP"));

        auto *scroll = new QScrollArea;
        auto *page = new QWidget;
        auto *pageLayout = new QVBoxLayout(page);
        auto *group = new QGroupBox(QStringLiteral("Оксиметрия"));
        auto *groupLayout = new QVBoxLayout(group);
        pulse = new QCheckBox(QStringLiteral("Отмечать пульс"));
        pulse->setToolTip(QStringLiteral("Отмечается при скачке пульса больше порога"));
        groupLayout->addWidget(pulse);
        pageLayout->addWidget(group);
        channels = new QTreeView;
        auto *model = new QStandardItemModel(channels);
        model->appendRow(new QStandardItem(QStringLiteral("Утечка")));
        model->appendRow(new QStandardItem(QStringLiteral("Давление")));
        channels->setModel(model);
        pageLayout->addWidget(channels);
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        tabs.addTab(scroll, QStringLiteral("Оксиметрия и каналы"));
    }
};

QStringList texts(const QList<Entry> &entries)
{
    QStringList out;
    for (const Entry &e : entries) out << e.text;
    return out;
}

} // namespace

void PreferencesSearchTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
}

void PreferencesSearchTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void PreferencesSearchTests::testNormalized()
{
    QCOMPARE(normalized(QStringLiteral("Показывать &Линию")), QStringLiteral("показывать линию"));
    QCOMPARE(normalized(QStringLiteral("Ёмкость  ёлки")), QStringLiteral("емкость елки"));
    QCOMPARE(normalized(QStringLiteral("<p>Линия <b>утечки</b></p>")), QStringLiteral("линия утечки"));
    QCOMPARE(normalized(QStringLiteral("Save && quit")), QStringLiteral("save & quit"));
}

void PreferencesSearchTests::testFind()
{
    Dialog d;
    const QList<Entry> entries = index(&d.tabs);

    // by name: the setting and the channel row, not the hidden setting
    QList<Entry> found = find(entries, QStringLiteral("утечк"));
    QVERIFY(texts(found).contains(QStringLiteral("Показывать красную линию утечки")));
    QVERIFY(texts(found).contains(QStringLiteral("Утечка")));
    QVERIFY(!texts(found).contains(QStringLiteral("Скрытая настройка утечки")));
    for (const Entry &e : found) {
        if (e.text == QStringLiteral("Утечка")) {
            QVERIFY(e.viewRow);
            QCOMPARE(e.widget.data(), static_cast<QWidget *>(d.channels));
            QCOMPARE(e.tab, 1);
        }
    }

    // every word counts, in any order; "ё" and "е" alike
    QCOMPARE(texts(find(entries, QStringLiteral("утечки линию"))), QStringList { QStringLiteral("Показывать красную линию утечки") });
    QCOMPARE(texts(find(entries, QStringLiteral("давлёние"))), QStringList { QStringLiteral("Давление") });

    // by tooltip, after the name matches
    found = find(entries, QStringLiteral("пульс"));
    QCOMPARE(texts(found).first(), QStringLiteral("Отмечать пульс"));
    QCOMPARE(texts(find(entries, QStringLiteral("скачке"))), QStringList { QStringLiteral("Отмечать пульс") });

    // group titles are found too
    QVERIFY(texts(find(entries, QStringLiteral("оксиметр"))).contains(QStringLiteral("Оксиметрия")));

    // one letter is not a search; the limit holds
    QVERIFY(find(entries, QStringLiteral("у")).isEmpty());
    QCOMPARE(find(entries, QStringLiteral("ут"), 1).size(), 1);

    QCOMPARE(label(find(entries, QStringLiteral("скачке")).first()),
             QStringLiteral("Оксиметрия и каналы › Отмечать пульс"));
}

void PreferencesSearchTests::testReveal()
{
    Dialog d;
    d.tabs.resize(400, 300);
    const QList<Entry> entries = index(&d.tabs);

    reveal(&d.tabs, find(entries, QStringLiteral("скачке")).first());
    QCOMPARE(d.tabs.currentIndex(), 1);
    QVERIFY(d.pulse->styleSheet().contains(highlightStyle()));

    reveal(&d.tabs, find(entries, QStringLiteral("давление")).first());
    QCOMPARE(d.channels->currentIndex().data().toString(), QStringLiteral("Давление"));

    reveal(&d.tabs, find(entries, QStringLiteral("красную")).first());
    QCOMPARE(d.tabs.currentIndex(), 0);
    QVERIFY(d.redline->styleSheet().contains(highlightStyle()));
}
