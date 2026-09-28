/* Löwenstein Prisma Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "prismatests.h"
#include "../SleepLib/schema.h"

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
