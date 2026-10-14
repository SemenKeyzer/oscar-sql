/* O2Ring record import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RING_IMPORT_H
#define O2RING_IMPORT_H

#include <QByteArray>
#include <QString>

#include "SleepLib/machine_common.h"

/*! \class O2RingImporter
    \brief Keeps a record downloaded from an O2Ring in the profile's backup folder and imports it
           with the Viatom loader, which takes the device serial from the folder name. */
class O2RingImporter
{
public:
    enum class Result { Imported, AlreadyThere, Failed };

    explicit O2RingImporter(const QString &serial);

    //! <profile data folder>/Backup/O2Ring_<serial>
    QString folder() const;
    //! The Viatom device of this ring already has the session this record would become.
    bool alreadyImported(const QString &name) const;
    //! Checks \a data, writes it as folder()/name (through a temporary file) and imports it.
    Result importFile(const QString &name, const QByteArray &data, QString *error);

    //! The session id the Viatom loader gives the record \a name ("yyyyMMddHHmmss", local time).
    static SessionID sessionIdFor(const QString &name);

private:
    QString machineSerial() const;

    QString m_serial;
};

#endif // O2RING_IMPORT_H
