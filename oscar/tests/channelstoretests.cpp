/* Channel Store Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channelstoretests.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/channel_options_repository.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"

namespace {
const QString kProfileName = QStringLiteral("ChannelStoreUnitTest");

ChannelData storedRow(ChannelID id)
{
    const qint64 profileId = ProfileRepository().findByUsername(kProfileName).id;
    for (const ChannelData &d : ChannelRepository().findByProfile(profileId)) {
        if (d.channelId == id) return d;
    }
    return ChannelData();
}
} // namespace

void ChannelStoreTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char appName[] = "test";
        static char *argv[] = { appName, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
    if (DatabaseManager::instance().isOpen()) DatabaseManager::instance().close();
    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-channelstoretests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());
    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    schema::init();
    Profiles::Scan();
    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Create(kProfileName, &profileDir);
    QVERIFY(p_profile != nullptr);
    p_profile->setDatabaseId(ProfileRepository().findByUsername(kProfileName).id);
}

// A name the user changed in Preferences is kept; one left alone is not stored, so the
// channel shows its registered (translated) name even when a later version improves it.
void ChannelStoreTests::testOnlyTheUsersNamesAreKept()
{
    schema::Channel &pressure = schema::channel[CPAP_Pressure];
    schema::Channel &leak = schema::channel[CPAP_Leak];
    const QString leakName = leak.fullname();
    pressure.setLabel(QStringLiteral("My pressure"));
    QVERIFY(p_profile->saveChannelsToDatabase());

    QCOMPARE(storedRow(CPAP_Pressure).label, QStringLiteral("My pressure"));
    QVERIFY(storedRow(CPAP_Pressure).fullname.isEmpty());
    QVERIFY(storedRow(CPAP_Leak).fullname.isEmpty());

    // as if a newer translation registered other names
    pressure.setLabel(pressure.defaultLabel());
    leak.setFullname(QStringLiteral("Newly translated leak"));
    QVERIFY(p_profile->loadChannelsFromDatabase());
    QCOMPARE(pressure.label(), QStringLiteral("My pressure"));
    QCOMPARE(leak.fullname(), QStringLiteral("Newly translated leak"));

    // a name the user set back to the registered one is not kept either
    pressure.setLabel(pressure.defaultLabel());
    leak.setFullname(leakName);
    QVERIFY(p_profile->saveChannelsToDatabase());
    QVERIFY(storedRow(CPAP_Pressure).label.isEmpty());
}

// Stored options never replace the registered ones (those are translated, the stored may
// be in another language or an older wording), but an option only the store knows stays.
void ChannelStoreTests::testRegisteredOptionsWin()
{
    schema::Channel &mode = schema::channel[CPAP_Mode];
    const QString cpap = mode.option(1);
    QVERIFY(!cpap.isEmpty());
    QHash<int, QString> stored = mode.m_options;
    stored[1] = QStringLiteral("stale wording");
    stored[77] = QStringLiteral("only in the store");
    QVERIFY(ChannelOptionsRepository().saveBatch(CPAP_Mode, stored));
    QVERIFY(p_profile->loadChannelsFromDatabase());
    QCOMPARE(mode.option(1), cpap);
    QCOMPARE(mode.option(77), QStringLiteral("only in the store"));
    mode.m_options.remove(77);
}

// Preferences, Reset to defaults, Names only: the registered (translated) names come back,
// the user's colours stay.
void ChannelStoreTests::testResetNamesKeepsColours()
{
    schema::Channel &leak = schema::channel[CPAP_Leak];
    const QColor original = leak.defaultColor();
    const QColor colour(0x12, 0x34, 0x56);
    leak.setDefaultColor(colour);
    leak.setFullname(QStringLiteral("An old translation"));
    leak.setLabel(QStringLiteral("Old"));
    schema::resetChannelNames();
    QCOMPARE(leak.fullname(), leak.defaultFullname());
    QCOMPARE(leak.label(), leak.defaultLabel());
    QCOMPARE(leak.defaultColor(), colour);
    leak.setDefaultColor(original);
}

void ChannelStoreTests::cleanupTestCase()
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
