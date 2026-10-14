/* O2Ring record import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ring_import.h"
#include "o2ring_protocol.h"
#include "viatom_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

O2RingImporter::O2RingImporter(const QString &serial)
    : m_serial(serial)
{
}

QString O2RingImporter::machineSerial() const
{
    return QStringLiteral("O2Ring_") + m_serial;
}

QString O2RingImporter::folder() const
{
    return p_profile->dataFolder() + QStringLiteral("/Backup/") + machineSerial();
}

bool O2RingImporter::alreadyImported(const QString &name) const
{
    Machine *mach = p_profile->lookupMachine(machineSerial(), viatom_class_name);
    return mach != nullptr && mach->SessionExists(sessionIdFor(name)) != nullptr;
}

O2RingImporter::Result O2RingImporter::importFile(const QString &name, const QByteArray &data, QString *error)
{
    if (alreadyImported(name)) return Result::AlreadyThere;
    if (!O2Ring::validateFile(name, data, error)) return Result::Failed;

    const QString path = folder() + QStringLiteral("/") + name;
    const QString part = path + QStringLiteral(".part");
    if (!QDir().mkpath(folder())) {
        if (error) *error = QObject::tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(folder()));
        return Result::Failed;
    }
    QFile file(part);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        if (error) *error = QObject::tr("Could not save the record %1.").arg(name);
        file.close();
        QFile::remove(part);
        return Result::Failed;
    }
    file.close();
    QFile::remove(path);
    if (!QFile::rename(part, path)) {
        if (error) *error = QObject::tr("Could not save the record %1.").arg(name);
        QFile::remove(part);
        return Result::Failed;
    }

    ViatomLoader loader;
    if (loader.Open(QStringList { path }) < 0 || !alreadyImported(name)) {
        if (error) *error = QObject::tr("OSCAR could not read the record %1.").arg(name);
        return Result::Failed;
    }
    return Result::Imported;
}

SessionID O2RingImporter::sessionIdFor(const QString &name)
{
    const QDateTime start = QDateTime::fromString(name, QStringLiteral("yyyyMMddHHmmss"));
    return start.isValid() ? SessionID(start.toSecsSinceEpoch()) : 0;
}
