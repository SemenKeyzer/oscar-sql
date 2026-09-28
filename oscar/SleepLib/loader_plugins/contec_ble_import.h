/* Contec BLE Oximeter Import Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_IMPORT_H
#define CONTEC_BLE_IMPORT_H

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTime>
#include <QVector>

#include "SleepLib/loader_plugins/contec_ble_protocol.h"
#include "SleepLib/serialoximeter.h"

class Machine;

namespace ContecBle {

enum class Decision { Import, AlreadyPresent, ReplaceShorter, ConflictOtherOximeter, InvalidStart };
enum class Outcome { Imported, Updated, AlreadyPresent, ConflictOtherOximeter, InvalidStart, NotDownloaded, SaveFailed };

//! This device's session that starts at the same second, if any.
struct ExistingSession { bool exists = false; int samples = 0; };

Decision decideRecord(const RecordHeader &h, const ExistingSession &same, bool nightHasOtherOximeter);
//! The night OSCAR files a session under, by the day-split time alone.
QDate predictNight(const QDateTime &startLocal, const QTime &daySplit);
//! Samples as OSCAR oximetry records: "no data" markers become 0 (a gap); PI 0.1 % -> 0.01 %.
QVector<OxiRecord> toOxiRecords(const Record &r);

struct EraseInput {
    bool eraseEnabled = false;
    bool downloadCompleted = false;
    QList<Outcome> outcomes;        //!< one per record header the oximeter reported
    int headersOnDevice = 0;
    QDateTime lastRecordEnd;        //!< latest start + samples seconds (oximeter clock)
    QDateTime downloadStarted;
    bool clockTrusted = false;      //!< OSCAR has set the oximeter clock before
};
enum class EraseVerdict { Erase, Disabled, NothingToErase, DownloadIncomplete, NotAllSaved, StillRecording, ClockNotSynced };
EraseVerdict canErase(const EraseInput &in);

} // namespace ContecBle

/*! \class ContecBleImporter
    \brief Stores downloaded Contec records as oximetry sessions of one OSCAR device. */
class ContecBleImporter
{
public:
    explicit ContecBleImporter(Machine *mach) : m_mach(mach) {}

    ContecBle::ExistingSession existing(const ContecBle::RecordHeader &h) const;
    bool nightHasOtherOximeter(const ContecBle::RecordHeader &h) const;
    QString otherOximeterName(const ContecBle::RecordHeader &h) const;
    ContecBle::Decision decide(const ContecBle::RecordHeader &h) const;
    //! Adds the record as a session (replacing a shorter one when told to) and saves it.
    ContecBle::Outcome save(const ContecBle::Record &r, ContecBle::Decision d);
    //! Updates the daily summaries after the last save.
    void finish();

private:
    Machine *m_mach;
};

#endif // CONTEC_BLE_IMPORT_H
