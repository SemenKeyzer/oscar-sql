/* O2Ring test data
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RINGTESTDATA_H
#define O2RINGTESTDATA_H

#include <QByteArray>
#include <QDateTime>

//! Synthetic O2Ring records and ring answers for the tests (no one's own recordings).
namespace O2RingTestData {

//! A record file as the ring stores it: 40-byte header, then \a points 5-byte samples (SpO2 95,
//! pulse 60) every \a step seconds; avg 94, min 89, drops 3.
QByteArray makeFile(const QDateTime &start = QDateTime(QDate(2026, 10, 12), QTime(23, 0, 5)), int points = 900,
                    int version = 3, int step = 4);
//! A ring answer: 0x55 | status | ~status | seq | len | payload | crc8.
QByteArray response(const QByteArray &payload = QByteArray(), quint8 status = 0, quint16 seq = 0);

} // namespace O2RingTestData

#endif // O2RINGTESTDATA_H
