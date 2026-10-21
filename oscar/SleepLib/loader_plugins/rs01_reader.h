/* Contec RS01 Reader Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01_READER_H
#define RS01_READER_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

//! One signal as the .HD header describes it.
struct RS01Signal {
    QString label;
    QString unit;
    int samplesPerRecord = 0;
    int bytesPerSample = 0;
    double digMin = 0, digMax = 0, physMin = 0, physMax = 0;
};

//! The 506-byte .HD header of a Contec RS01 recording (fixed-width ASCII fields).
struct RS01Header {
    bool valid = false;
    QString error;
    QDateTime start;           //!< the device's local clock
    int segHeaderBytes = 0;
    int recordsPerSeg = 0;
    int recordSeconds = 0;
    int prefixBytes = 0;
    int suffixBytes = 0;
    QVector<RS01Signal> channels;   //!< in record order
    //! Bytes of one record: prefix, every signal's samples, suffix.
    int recordBytes() const;
    //! The signal labelled \a label, or -1.
    int indexOf(const QString &label) const;
};

//! Reads a .HD header; nothing hard-coded but the field positions.
RS01Header rs01ParseHeader(const QByteArray &hd);

//! Samples without a gap: the first one's time, the interval, the physical values.
struct RS01Run {
    qint64 startMs = 0;
    double intervalMs = 0;
    QVector<float> samples;
};

//! A recording read from its folder: per signal label (Flow, PR, SpO2, Pleth) the runs of
//! physical values. Flow is in Pa around the recording's own zero, inspiration positive;
//! SpO2 and pulse readings of 0 (no signal) are gaps.
struct RS01Recording {
    bool valid = false;
    QString error;
    QString name;              //!< the folder's name, MMDDhhmm
    RS01Header header;
    QHash<QString, QVector<RS01Run>> runs;
    int seconds = 0;           //!< from the start to the last second read
    int missingSeconds = 0;    //!< records without the marker or with a wrong index
};

//! Reads the recording in \a folder (MMDDhhmm.HD and 001.SEG, 002.SEG ...). Only reads.
RS01Recording rs01ReadRecording(const QString &folder);

//! The recording folders under \a root (MMDDhhmm with its .HD), in name order.
QStringList rs01FindRecordings(const QString &root);

#endif // RS01_READER_H
