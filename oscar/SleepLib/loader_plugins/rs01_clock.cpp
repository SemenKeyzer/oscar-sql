/* Contec RS01 Clock Commands
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01_clock.h"

#include <QElapsedTimer>

namespace {

constexpr int kReportBytes = 64;

QByteArray padded(QByteArray b)
{
    b.append(QByteArray(kReportBytes - b.size(), '\0'));
    return b;
}

QByteArray withChecksum(QByteArray b)
{
    b.append(char(rs01Checksum(b)));
    return padded(b);
}

// The reply without the report ID hidapi may leave in front.
QByteArray payload(const QByteArray &reply)
{
    return !reply.isEmpty() && reply.at(0) == '\0' ? reply.mid(1) : reply;
}

} // namespace

quint8 rs01Checksum(const QByteArray &commandAndParams)
{
    if (commandAndParams.isEmpty()) return 0;
    int sum = quint8(commandAndParams.at(0)) & 0x7F;
    for (int i = 1; i < commandAndParams.size(); ++i) sum += quint8(commandAndParams.at(i));
    return quint8(sum & 0x7F);
}

QByteArray rs01IdentifyCommand()
{
    return withChecksum(QByteArray(1, char(0x81)));
}

QByteArray rs01ClockCommand(const QDateTime &local)
{
    const QDate d = local.date();
    const QTime t = local.time();
    if (!local.isValid() || d.year() < 2000 || d.year() > 2127) return QByteArray();
    QByteArray b;
    for (int v : { 0x83, d.year() - 2000, d.month(), d.day(), t.hour(), t.minute(), t.second(), 0, 0 }) b.append(char(v));
    return withChecksum(b);
}

bool rs01IsIdentifyReply(const QByteArray &reply)
{
    // «F1» + the model in 8 characters + the checksum
    const QByteArray p = payload(reply);
    if (p.size() < 10 || quint8(p.at(0)) != 0xF1) return false;
    return p.mid(1, 8).trimmed() == "RS01" && quint8(p.at(9)) == rs01Checksum(p.left(9));
}

bool rs01IsClockReply(const QByteArray &reply)
{
    const QByteArray p = payload(reply);
    return p.size() >= 3 && quint8(p.at(0)) == 0xF3 && p.at(1) == '\0' && quint8(p.at(2)) == rs01Checksum(p.left(2));
}

QByteArray rs01AwaitReply(const std::function<QByteArray(int)> &read, bool (*accepted)(const QByteArray &), int totalMs)
{
    QElapsedTimer clock;
    clock.start();
    QByteArray last;
    for (qint64 left = totalMs; left > 0; left = totalMs - clock.elapsed()) {
        const QByteArray reply = read(int(left));
        if (reply.isEmpty()) break;   // timed out
        if (accepted(reply)) return reply;
        last = reply;   // something else: wait on, but it is what the device said
    }
    return last;
}
