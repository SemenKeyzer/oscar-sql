/* Löwenstein Prisma Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "prismatests.h"
#include "../SleepLib/schema.h"
#include "../SleepLib/common.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../SleepLib/thirdparty/miniz.h"

namespace {

// Synthetic parameters of a Prisma Line (e.g. prisma20A) card in dynamic APAP from 6 to 9 hPa.
// On these devices P1200 (IPAP) follows P min; the upper limit is P1199 (IPAP max).
QHash<int, int> lineApapParameters()
{
    return {
        { PRISMA_LINE_MODE, PRISMA_MODE_APAP },
        { PRISMA_LINE_APAP_DYNAMIC, PRISMA_APAP_MODE_DYNAMIC },
        { PRISMA_LINE_EPAP, 600 },
        { PRISMA_LINE_IPAP, 600 },
        { PRISMA_LINE_IPAP_MAX, 900 },
        { PRISMA_LINE_SOFT_PAP_LEVEL, 1 },
        { PRISMA_LINE_TUBE_TYPE, 150 },
    };
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A synthetic therapy.pdat: a zip holding one small member per session file name.
bool writeTherapyArchive(const QString &path, const QStringList &members)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return false;
    bool ok = true;
    for (const QString &m : members) {
        const QByteArray name = m.toUtf8();
        ok = ok && mz_zip_writer_add_mem(&zip, name.constData(), name.constData(), size_t(name.size()), MZ_DEFAULT_COMPRESSION);
    }
    void *buf = nullptr;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    if (ok) ok = writeFile(path, QByteArray(static_cast<const char *>(buf), int(size)));
    mz_free(buf);
    mz_zip_writer_end(&zip);
    return ok;
}

const char *const kNight1 = "mnt/flash/data/therapy/events/20260925/event_000600.xml";
const char *const kNight2 = "mnt/flash/data/therapy/events/20260926/event_000625.xml";

} // namespace

void PrismaTests::initTestCase()
{
    schema::init();
    PrismaLoader::Register();   // defines the Prisma channels
}

void PrismaTests::testLineApapPressureRange()
{
    QHash<ChannelID, QVariant> settings;
    PrismaImport::applySettings(settings, lineApapParameters());
    QCOMPARE(settings[CPAP_Mode].toInt(), int(MODE_APAP));
    QCOMPARE(settings[Prisma_Mode].toInt(), int(PRISMA_COMBINED_MODE_APAP_DYN));
    QCOMPARE(settings[CPAP_PressureMin].toDouble(), 6.0);
    QCOMPARE(settings[CPAP_PressureMax].toDouble(), 9.0);
    QCOMPARE(settings[Prisma_SoftPAP].toInt(), 1);
}

// Prisma Line cards carry the tube type as P1091; P21 only exists on Prisma SMART cards.
void PrismaTests::testLineTubeType()
{
    const QList<int> modes { PRISMA_MODE_CPAP, PRISMA_MODE_APAP, PRISMA_MODE_ACSV, PRISMA_MODE_S,
                             PRISMA_MODE_AUTO_S, PRISMA_MODE_AUTO_ST };
    for (int mode : modes) {
        QHash<int, int> parameters = lineApapParameters();
        parameters[PRISMA_LINE_MODE] = mode;
        QHash<ChannelID, QVariant> settings;
        PrismaImport::applySettings(settings, parameters);
        QVERIFY2(settings.contains(Prisma_TubeType), qPrintable(QStringLiteral("mode %1").arg(mode)));
        QCOMPARE(settings[Prisma_TubeType].toDouble(), 15.0);
    }
}

// Event 113 is a hypopnea the device scored while the mask leaked; its mechanism is unknown, so it
// is an unclassified hypopnea (as event 103, an apnea during leakage, is an unclassified apnea).
void PrismaTests::testHypopneaDuringLeakIsImported()
{
    ChannelID channel = 0;
    for (const auto &entry : PrismaImport::eventChannels()) {
        if (entry.second.contains(PRISMA_EVENT_HYPOPNEA_LEAKAGE)) channel = entry.first;
    }
    QCOMPARE(channel, CPAP_Hypopnea);
}

// The device shows softPAP as a level number; the label must start with it, and level 3 exists.
void PrismaTests::testSoftPapLabelsShowTheLevel()
{
    schema::Channel &softPap = schema::channel[Prisma_SoftPAP];
    QCOMPARE(softPap.option(0), QObject::tr("Off"));
    for (int level = 1; level <= 3; ++level) {
        QVERIFY2(softPap.option(level).startsWith(QString::number(level)),
                 qPrintable(QStringLiteral("level %1: \"%2\"").arg(level).arg(softPap.option(level))));
    }
}

// A Prisma Line card has only files in its root; copying it must still create the destination.
void PrismaTests::testCopyPathCreatesTheDestination()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString card = tmp.path() + QStringLiteral("/card");
    QVERIFY(QDir().mkpath(card));
    QVERIFY(writeFile(card + QStringLiteral("/config.pcfg"), "config"));
    copyPath(card, tmp.path() + QStringLiteral("/copy"));
    QCOMPARE(readFile(tmp.path() + QStringLiteral("/copy/config.pcfg")), QByteArray("config"));
}

// After a data-version change OSCAR rebuilds a device from its Backup folder, reading the card
// files from that folder itself, so the backup must not go into a sub-folder named after the card.
void PrismaTests::testBackupGoesToTheBackupFolderRoot()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString card = tmp.path() + QStringLiteral("/NO NAME");
    const QString backup = tmp.path() + QStringLiteral("/Backup");
    QVERIFY(QDir().mkpath(card));
    QVERIFY(writeFile(card + QStringLiteral("/config.pcfg"), "config"));
    QVERIFY(writeTherapyArchive(card + QStringLiteral("/therapy.pdat"), { kNight1 }));
    PrismaLoader::backupCard(card, backup);
    QCOMPARE(readFile(backup + QStringLiteral("/config.pcfg")), QByteArray("config"));
    QCOMPARE(readFile(backup + QStringLiteral("/therapy.pdat")), readFile(card + QStringLiteral("/therapy.pdat")));
}

// therapy.pdat holds the whole history and only grows on the card: the copy follows it.
void PrismaTests::testBackupRefreshesAGrowingTherapyFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString card = tmp.path() + QStringLiteral("/card");
    const QString backup = tmp.path() + QStringLiteral("/Backup");
    QVERIFY(QDir().mkpath(card));
    QVERIFY(writeTherapyArchive(card + QStringLiteral("/therapy.pdat"), { kNight1 }));
    PrismaLoader::backupCard(card, backup);
    QVERIFY(writeTherapyArchive(card + QStringLiteral("/therapy.pdat"), { kNight1, kNight2 }));
    PrismaLoader::backupCard(card, backup);
    QCOMPARE(readFile(backup + QStringLiteral("/therapy.pdat")), readFile(card + QStringLiteral("/therapy.pdat")));
    QCOMPARE(QDir(backup).entryList({ QStringLiteral("therapy.pdat.*") }, QDir::Files).size(), 0);
}

// A replaced or formatted card lacks sessions the old copy has: keep the old copy, dated.
void PrismaTests::testBackupKeepsTheOldTherapyFileWhenSessionsWouldBeLost()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString card = tmp.path() + QStringLiteral("/card");
    const QString backup = tmp.path() + QStringLiteral("/Backup");
    QVERIFY(QDir().mkpath(card));
    QVERIFY(writeTherapyArchive(card + QStringLiteral("/therapy.pdat"), { kNight1, kNight2 }));
    PrismaLoader::backupCard(card, backup);
    const QByteArray older = readFile(backup + QStringLiteral("/therapy.pdat"));
    QVERIFY(writeTherapyArchive(card + QStringLiteral("/therapy.pdat"), { kNight2 }));
    PrismaLoader::backupCard(card, backup);
    QCOMPARE(readFile(backup + QStringLiteral("/therapy.pdat")), readFile(card + QStringLiteral("/therapy.pdat")));
    const QStringList kept = QDir(backup).entryList({ QStringLiteral("therapy.pdat.*") }, QDir::Files);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(readFile(backup + QStringLiteral("/") + kept.first()), older);
}

