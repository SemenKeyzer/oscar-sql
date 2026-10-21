/* Contec RS01 Import Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01_IMPORT_H
#define RS01_IMPORT_H

#include <QDateTime>
#include <QString>
#include "SleepLib/machine_common.h"

/*! \class RS01Importer
    \brief Imports a Contec RS01 recording folder: first copies it into the profile's backup
           (the device's own disk is only read), then reads the copy into one oximeter session
           with SpO2, pulse, pleth and the nasal flow. */
class RS01Importer
{
public:
    enum class Result { Imported, AlreadyThere, Failed };
    //! <profile data folder>/Backup/RS01
    QString backupFolder() const;
    //! The device already has the session the recording in \a recordingFolder would become.
    bool alreadyImported(const QString &recordingFolder) const;
    //! Copies \a recordingFolder (MMDDhhmm) and imports the copy; \a error says why it failed.
    Result import(const QString &recordingFolder, QString *error);
    //! The session id of a recording started at \a start (the device's local clock).
    static SessionID sessionIdFor(const QDateTime &start);
    static QString loaderName();
};

#endif // RS01_IMPORT_H
