/* UI Snapshot Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "uishotstests.h"

#include <QApplication>
#include <QDir>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/database_manager.h"
#include "Graphs/gGraph.h"
#include "preferencesdialog.h"

namespace {
const QString kProfileName = QStringLiteral("UiShots");
}

void UiShotsTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    if (QCoreApplication::instance() == nullptr) m_app = new QApplication(argc, argv);
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-uishots-XXXXXX"));
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());
    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    if (CPAP_Obstructive == 0) schema::init();
    InitGraphGlobals();   // the graph fonts the dialog shows
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
    // a CPAP in the profile: without one the dialog asks about ResMed in a message box
    p_profile->CreateMachine(MachineInfo(MT_CPAP, 0, QStringLiteral("PRS1"), QStringLiteral("Philips Respironics"),
                                         QStringLiteral("DreamStation"), QString(), QStringLiteral("UISHOTS1"), QString(),
                                         QDateTime::currentDateTime(), 1));
}

// Every tab of the Preferences, as PNG files in OSCAR_UI_SHOTS.
void UiShotsTests::testPreferencesShots()
{
    const QString out = qEnvironmentVariable("OSCAR_UI_SHOTS");
    if (out.isEmpty()) QSKIP("OSCAR_UI_SHOTS not set");
    QDir().mkpath(out);
    PreferencesDialog dialog(nullptr, p_profile);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.resize(1100, 900);
    dialog.show();
    QTabWidget *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
    QVERIFY(tabs);
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        QCoreApplication::processEvents();
        QVERIFY(dialog.grab().save(QDir(out).filePath(QStringLiteral("prefs-%1.png").arg(i))));
    }
}

// The doctor's Preferences remarks (2026-10-23): no frame inside a frame, groups as tall as their
// contents, the check buttons say which column they set, the Analysis tab not grey.
void UiShotsTests::testPreferencesFrames()
{
    PreferencesDialog dialog(nullptr, p_profile);
    auto box = [&dialog](const char *name) { return dialog.findChild<QGroupBox *>(QLatin1String(name)); };
    auto text = [&dialog](const char *name) {
        QPushButton *b = dialog.findChild<QPushButton *>(QLatin1String(name));
        return b ? b->text() : QString();
    };
    QCOMPARE(text("channelsShowAll"), QStringLiteral("Events: select all"));
    QCOMPARE(text("channelsShowNone"), QStringLiteral("Events: clear all"));
    QCOMPARE(text("channelsOverviewAll"), QStringLiteral("Overview: select all"));
    QCOMPARE(text("channelsOverviewNone"), QStringLiteral("Overview: clear all"));
    QCOMPARE(text("waveformsShowAll"), QStringLiteral("Graphs: select all"));
    QCOMPARE(text("waveformsShowNone"), QStringLiteral("Graphs: clear all"));
    QCOMPARE(text("waveformsOverviewAll"), QStringLiteral("Overview: select all"));
    // the outer frames around single groups go; what is inside stays
    for (const char *outer : { "steadyBreathingOverview", "clinicalGroupBox", "oximetryGroupBox" }) {
        QVERIFY2(box(outer) && box(outer)->title().isEmpty() && box(outer)->styleSheet().contains(QStringLiteral("border: none")), outer);
    }
    QVERIFY(box("steadyBreathing")->title().contains(QStringLiteral("experimental")));
    QVERIFY(box("groupBox_Statistics")->title() == QStringLiteral("Statistics"));   // framed like the others
    QVERIFY(!box("groupBox_Statistics")->styleSheet().contains(QStringLiteral("border: none")));
    QCOMPARE(box("groupBox_8")->sizePolicy().verticalPolicy(), QSizePolicy::Maximum);   // Memory and Startup
    QScrollArea *analysis = nullptr;
    for (QScrollArea *a : dialog.findChildren<QScrollArea *>()) {
        if (a->widget() && a->widget()->inherits("AnalysisPreferencesPage")) analysis = a;
    }
    QVERIFY(analysis);
    QVERIFY(!analysis->viewport()->autoFillBackground() && !analysis->widget()->autoFillBackground());
    QVERIFY(analysis->styleSheet().contains(QStringLiteral("transparent")));
}

void UiShotsTests::cleanupTestCase()
{
    if (!m_tempDir) return;
    Profiles::profiles.clear();
    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;
    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);
    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
