/* Contec RS01 Clock Commands Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01_CLOCK_H
#define RS01_CLOCK_H

#include <QByteArray>
#include <QDateTime>
#include <functional>

// The RS01's HID commands OSCAR sends (format notes §4, verified on the owner's device): identify and
// set the clock; nothing else. Each is a 64-byte report body (without the report ID), zero padded.

//! The checksum the device uses: the command's low 7 bits plus the parameters, 7 bits.
quint8 rs01Checksum(const QByteArray &commandAndParams);
//! «81 01»: what are you? (protocol A answers «F1 RS01 …»)
QByteArray rs01IdentifyCommand();
//! «83 YY MM DD hh mm ss 00 00 CK» for the local time \a local; empty outside the years 2000–2127.
QByteArray rs01ClockCommand(const QDateTime &local);
//! An RS01 answering in protocol A, with a valid checksum (a leading report ID 0x00 is skipped).
bool rs01IsIdentifyReply(const QByteArray &reply);
//! «F3 00 73»: the clock was set.
bool rs01IsClockReply(const QByteArray &reply);

//! Reads replies with \a read (given the time left, in ms; empty on a timeout) until \a accepted takes
//! one or \a totalMs have passed; then the last reply read (empty if none came).
QByteArray rs01AwaitReply(const std::function<QByteArray(int)> &read, bool (*accepted)(const QByteArray &), int totalMs);

#endif // RS01_CLOCK_H
