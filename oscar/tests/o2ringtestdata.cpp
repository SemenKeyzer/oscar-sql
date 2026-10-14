/* O2Ring test data
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringtestdata.h"

#include <QtEndian>

#include "SleepLib/loader_plugins/o2ring_protocol.h"

namespace O2RingTestData {

QByteArray makeFile(const QDateTime &start, int points, int version, int step)
{
    QByteArray body;
    for (int i = 0; i < points; ++i) {
        body.append(char(95));
        body.append(char(60));
        body.append(char(0));
        body.append(char(0));
        body.append(char(0));
    }
    QByteArray h(40, '\0');
    h[0] = char(version);
    qToLittleEndian<quint16>(quint16(start.date().year()), h.data() + 2);
    h[4] = char(start.date().month());
    h[5] = char(start.date().day());
    h[6] = char(start.time().hour());
    h[7] = char(start.time().minute());
    h[8] = char(start.time().second());
    qToLittleEndian<quint32>(quint32(40 + body.size()), h.data() + 9);
    qToLittleEndian<quint16>(quint16(points * step), h.data() + 13);
    h[17] = char(94);
    h[18] = char(89);
    h[20] = char(3);
    return h + body;
}

QByteArray response(const QByteArray &payload, quint8 status, quint16 seq)
{
    QByteArray raw;
    raw.append(char(0x55));
    raw.append(char(status));
    raw.append(char(~status & 0xFF));
    raw.append(char(seq & 0xFF));
    raw.append(char(seq >> 8));
    raw.append(char(payload.size() & 0xFF));
    raw.append(char(payload.size() >> 8));
    raw.append(payload);
    raw.append(char(O2Ring::crc8(raw)));
    return raw;
}

} // namespace O2RingTestData
