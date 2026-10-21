/* Contec RS01 Import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01_import.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include "rs01_reader.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/oximetry_session_builder.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"
#include "database/event_list_repository.h"

namespace {

const QString kSerial = QStringLiteral("RS01");

MachineInfo rs01Info()
{
    MachineInfo info(MT_OXIMETER, 0, RS01Importer::loaderName(), QStringLiteral("Contec"), QStringLiteral("RS01"),
                     QString(), kSerial, QString(), QDateTime::currentDateTime(), 1);
    return info;
}

RS01Header readHeader(const QString &folder)
{
    QFile f(rs01HeaderFile(folder));
    if (!f.open(QIODevice::ReadOnly)) return RS01Header();
    return rs01ParseHeader(f.readAll());
}

// Copies one file from the device (opened read-only) through a temporary name.
bool copyFile(const QString &from, const QString &to)
{
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly)) return false;
    const QString part = to + QStringLiteral(".part");
    QFile out(part);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = in.readAll();
    const bool ok = out.write(data) == data.size();
    out.close();
    if (!ok) {
        QFile::remove(part);
        return false;
    }
    QFile::remove(to);
    return QFile::rename(part, to);
}

// The recording's files: its header and the segments.
QStringList recordingFiles(const QString &folder)
{
    QStringList files { QFileInfo(rs01HeaderFile(folder)).fileName() };   // names as they are (.hd from a copy)
    files << QDir(folder).entryList({ QStringLiteral("*.seg") }, QDir::Files, QDir::Name);
    return files;
}

struct ChannelMap { const char *label; ChannelID *code; double gain; };

} // namespace

QString RS01Importer::loaderName()
{
    return QStringLiteral("ContecRS01");
}

SessionID RS01Importer::sessionIdFor(const QDateTime &start)
{
    return SessionID(start.toUTC().toSecsSinceEpoch());
}

QString RS01Importer::backupFolder() const
{
    return p_profile->dataFolder() + QStringLiteral("/Backup/RS01");
}

bool RS01Importer::alreadyImported(const QString &recordingFolder) const
{
    const RS01Header h = readHeader(recordingFolder);
    if (!h.valid) return false;
    Machine *mach = p_profile->lookupMachine(kSerial, loaderName());
    return mach != nullptr && mach->SessionExists(sessionIdFor(h.start)) != nullptr;
}

RS01Importer::Result RS01Importer::import(const QString &recordingFolder, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error) *error = why;
        return Result::Failed;
    };
    const QString name = QFileInfo(rs01HeaderFile(recordingFolder)).completeBaseName();   // MMDDhhmm, whatever the folder's name
    const RS01Header h = readHeader(recordingFolder);
    if (!h.valid) return fail(QObject::tr("%1: the header can't be read (%2).").arg(name, h.error));
    if (alreadyImported(recordingFolder)) return Result::AlreadyThere;

    // copy first: the device's own disk is only ever read. The copy is named by the full start
    // (the device's MMDDhhmm has no year): another recording never finds an old copy under its name.
    const QString copy = QDir(backupFolder()).filePath(h.start.toString(QStringLiteral("yyyyMMddHHmmss")) + QLatin1Char('/') + name);
    if (!QDir().mkpath(copy)) return fail(QObject::tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(copy)));
    for (const QString &file : recordingFiles(recordingFolder)) {
        const QString to = QDir(copy).filePath(file);
        const QFileInfo src(QDir(recordingFolder).filePath(file));
        if (QFileInfo::exists(to) && QFileInfo(to).size() == src.size() && src.size() > 0) continue;   // copied before, complete
        if (!copyFile(src.filePath(), to)) return fail(QObject::tr("%1: could not copy %2.").arg(name, file));
    }

    const RS01Recording rec = rs01ReadRecording(copy);
    if (!rec.valid) return fail(QObject::tr("%1: %2").arg(name, rec.error));

    Machine *mach = p_profile->CreateMachine(rs01Info());
    const SessionID sid = sessionIdFor(h.start);
    Session *sess = new Session(mach, sid);
    sess->really_set_first(h.start.toMSecsSinceEpoch());
    const ChannelMap maps[] = { { "SpO2", &OXI_SPO2, 1.0 }, { "PR", &OXI_Pulse, 1.0 },
                                { "Pleth", &OXI_Plethy, 0.1 }, { "Flow", &OXI_NasalFlow, 0.1 } };
    qint64 lastMs = h.start.toMSecsSinceEpoch();
    for (const ChannelMap &m : maps) {
        for (const RS01Run &run : rec.runs.value(QLatin1String(m.label))) {
            // stored as raw steps of the gain (qint16), as every waveform: 85.3 Pa at 0.1 is 853
            QVector<qint16> raw(run.samples.size());
            for (int k = 0; k < run.samples.size(); ++k)
                raw[k] = qint16(qBound(-32768, qRound(run.samples[k] / m.gain), 32767));
            const qint64 duration = qint64(run.samples.size() * run.intervalMs);
            EventList *el = sess->AddEventList(*m.code, EVL_Waveform, EventDataType(m.gain), 0, 0, 0, run.intervalMs);
            el->AddWaveform(run.startMs, raw.data(), raw.size(), duration);
            lastMs = qMax(lastMs, run.startMs + duration);
        }
    }
    for (ChannelID c : { OXI_Plethy, OXI_NasalFlow }) {   // the oximetry summaries leave these two
        sess->first(c);
        sess->last(c);
        sess->Min(c);
        sess->Max(c);
    }
    finishOximetrySession(sess, lastMs, false);

    auto reject = [&](const QString &why) {
        mach->unlinkSession(sess);
        delete sess;
        return fail(why);
    };
    if (!mach->AddSession(sess)) return reject(QObject::tr("%1: the session could not be added.").arg(name));
    // a day holds one oximeter: Day::addSession refuses a second one without telling the caller
    if (sess->night().isValid()) {
        Day *day = p_profile->GetDay(sess->night());
        if (!day || !day->sessions.contains(sess))
            return reject(QObject::tr("%1: that night already has another oximeter's recording.").arg(name));
    }
    mach->Save();
    if (sess->sessionRowId() <= 0 || sess->IsChanged() || EventListRepository().countBySession(sess->sessionRowId()) == 0)
        return reject(QObject::tr("%1: the recording could not be saved.").arg(name));
    mach->SaveSummaryCache();
    p_profile->StoreMachines();
    return Result::Imported;
}
