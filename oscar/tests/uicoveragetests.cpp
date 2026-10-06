/* UI Coverage Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "uicoveragetests.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "analysisprefs.h"
#include "database/database_manager.h"
#include "glossary.h"
#include "helptips.h"
#include "overviewpresets.h"
#include "ui_daily.h"
#include "ui_mainwindow.h"
#include "ui_oximeterimport.h"
#include "ui_overview.h"
#include "ui_preferencesdialog.h"
#include "ui_welcome.h"
#ifdef HAVE_BLUETOOTH
#include "bluetoothoximeterpage.h"
#endif

namespace {

bool interactive(QWidget *w)
{
    if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w)
        || qobject_cast<QLineEdit *>(w) || qobject_cast<QSlider *>(w) || qobject_cast<QCalendarWidget *>(w)) {
        return true;
    }
    auto *group = qobject_cast<QGroupBox *>(w);
    return group && group->isCheckable();
}

//! Names of the controls under \a root that got no explanation from attachAll(root, window).
QStringList uncovered(QWidget *root, const QString &window, const QStringList &exempt)
{
    HelpTips::attachAll(root, window);
    QStringList out;
    for (QWidget *w : root->findChildren<QWidget *>()) {
        QString name = w->objectName();
        if (!interactive(w) || name.startsWith(QLatin1String("qt_")) || exempt.contains(name)) continue;
        // a control made in code without a name cannot be explained
        if (name.isEmpty()) name = QStringLiteral("<unnamed %1>").arg(QString::fromLatin1(w->metaObject()->className()));
        bool internal = false;   // parts of compound widgets and dialog button boxes
        for (QWidget *p = w->parentWidget(); p && p != root; p = p->parentWidget()) {
            if (qobject_cast<QDialogButtonBox *>(p) || qobject_cast<QCalendarWidget *>(p) || qobject_cast<QAbstractSpinBox *>(p)
                || qobject_cast<QComboBox *>(p) || qobject_cast<QTabBar *>(p)) {
                internal = true;
            }
        }
        if (!internal && w->property("helpKey").toString().isEmpty()) out << name;
    }
    out.removeDuplicates();
    return out;
}

} // namespace

void UiCoverageTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QApplication(argc, argv);
    }
    // the Bluetooth page reads the profile's oximetry settings
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-uicoverage-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    if (CPAP_Obstructive == 0) schema::init();
    Profiles::Scan();
    const QString name = QStringLiteral("UiCoverageTest");
    const QString dir = m_tempDir->path() + QStringLiteral("/Profiles/") + name;
    p_profile = Profiles::Get(name);
    if (p_profile == nullptr) p_profile = Profiles::Create(name, &dir);
    QVERIFY(p_profile != nullptr);
    HelpTips::instance()->setEnabled(true);
}

void UiCoverageTests::cleanupTestCase()
{
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

void UiCoverageTests::testMenusCovered()
{
    QMainWindow host;
    Ui::MainWindow ui;
    ui.setupUi(&host);
    // in no menu: hidden, or reached only by a shortcut
    const QStringList exempt = { QStringLiteral("actionChange_User"), QStringLiteral("actionExport_Review"),
                                 QStringLiteral("actionManage_Reports"), QStringLiteral("actionPurge_Current_Selected_Day"),
                                 QStringLiteral("actionUse_AntiAliasing"), QStringLiteral("actionView_Welcome"),
                                 QStringLiteral("action_CycleTabs"), QStringLiteral("action_Profiles") };
    QStringList missing;
    for (QAction *a : host.findChildren<QAction *>()) {
        if (a->isSeparator() || a->objectName().isEmpty() || a->text().isEmpty() || exempt.contains(a->objectName())) continue;
        if (!Glossary::find(QStringLiteral("ui.menu.") + a->objectName())) missing << a->objectName();
    }
    missing.removeDuplicates();
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testMainCovered()
{
    QMainWindow host;
    Ui::MainWindow ui;
    ui.setupUi(&host);
    const QStringList missing = uncovered(&host, QStringLiteral("main"), {});
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testDailyCovered()
{
    QWidget host;
    Ui::Daily ui;
    ui.setupUi(&host);
    const QStringList missing = uncovered(&host, QStringLiteral("daily"), {});
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testOverviewCovered()
{
    QWidget host;
    Ui::Overview ui;
    ui.setupUi(&host);
    const QStringList missing = uncovered(&host, QStringLiteral("overview"), {});
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testWelcomeCovered()
{
    QWidget host;
    Ui::Welcome ui;
    ui.setupUi(&host);
    const QStringList missing = uncovered(&host, QStringLiteral("welcome"), {});
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testPrefsCovered()
{
    QDialog host;
    Ui::PreferencesDialog ui;
    ui.setupUi(&host);
    AnalysisPreferencesPage analysis;
    // OK and Cancel need no explanation
    const QStringList missing = uncovered(&host, QStringLiteral("prefs"), { QStringLiteral("okButton"), QStringLiteral("cancelButton") })
                                + uncovered(&analysis, QStringLiteral("prefs"), {});
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testOximportCovered()
{
    QDialog host;
    Ui::OximeterImport ui;
    ui.setupUi(&host);
    QStringList missing = uncovered(&host, QStringLiteral("oximport"), {});
#ifdef HAVE_BLUETOOTH
    BluetoothOximeterPage page;
    missing += uncovered(&page, QStringLiteral("ble"), {});
#endif
    QVERIFY2(missing.isEmpty(), qPrintable(QString::number(missing.size()) + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
}

void UiCoverageTests::testCodeCreatedControlsCovered()
{
    QStringList keys = { QStringLiteral("ui.menu.actionRecalculateAnalysis"), QStringLiteral("ui.menu.actionHelpPanel"), QStringLiteral("ui.daily.alignButton"),
                         QStringLiteral("ui.prefs.searchSettings"), QStringLiteral("ui.oximport.bluetoothImportButton"),
                         QStringLiteral("ui.oximport.bluetoothRetryButton"), QStringLiteral("ui.oximport.bluetoothDoneButton") };
    // the Daily search tab is built in code
    for (const char *name : { "searchHelpButton", "searchMatchButton", "searchAddMatchButton", "searchClearButton", "searchStartButton",
                              "searchCommandButton", "searchOperationCombo", "searchOperationButton", "searchValueDouble",
                              "searchValueInteger", "searchValueText", "searchProgress", "searchFound", "searchMinMax" }) {
        keys << QStringLiteral("ui.daily.") + QString::fromLatin1(name);
    }
    for (OverviewPresets::Preset preset : OverviewPresets::presets()) {
        keys << QStringLiteral("ui.overview.presetButton_%1").arg(int(preset));
    }
    QStringList missing;
    for (const QString &k : keys) {
        if (!Glossary::find(k)) missing << k;
    }
    QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));
}
