/* Simulated Contec BLE oximeter for unit tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "fakecontecdevice.h"

#include <QMetaObject>

using namespace ContecBle;

FakeContecDevice::FakeContecDevice(int protocolVersion, QObject *parent)
    : ContecBleLink(parent), m_version(protocolVersion)
{
}

int FakeContecDevice::commandLength(const QByteArray &buf) const
{
    const int h = quint8(buf[0]);
    switch (h) {
    case 0x81: case 0x82: case 0x9F: case 0x9A: return 2;
    case 0x8E: case 0x90: case 0x9C: case 0x9B: return 3;
    case 0x8F: return 4;
    case 0x83: return 10;
    case 0xF3: return 22;
    case 0x9D:
        if (buf.size() < 2) return 0;
        return (quint8(buf[1]) == 0x03 || quint8(buf[1]) == 0x04) ? 9 : 8;
    case 0xF4: {
        if (buf.size() < 2) return 0;
        const int n = quint8(buf[1]) & 0x7F;
        return n + (n + 9) / 7 + 6;
    }
    default: return 1;
    }
}

void FakeContecDevice::write(const QByteArray &data)
{
    m_in += data;
    while (!m_in.isEmpty()) {
        const int n = commandLength(m_in);
        if (n == 0 || n > m_in.size()) return;
        const QByteArray cmd = m_in.left(n);
        m_in.remove(0, n);
        if (n == 1) continue;
        const int h = quint8(cmd[0]);
        if (h == 0xF3 && cmd.size() == 22) {
            keyExchange(cmd);
        } else if (h == 0xF4 && m_encrypted) {
            QByteArray as84 = cmd;
            as84[0] = char(0x84);
            sawEncryptedCommand = true;
            handle(open84(as84, m_decrypt));
        } else {
            handle(cmd);
        }
    }
}

void FakeContecDevice::keyExchange(const QByteArray &f3)
{
    const QByteArray seedApp = seedFrom83(f3);
    const QByteArray seedDev = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10");
    deriveRx(seedApp, deviceId, m_encrypt.keyTx, m_encrypt.ivTx);      // the app decrypts 84 with these
    deriveTx(seedDev, deviceId, m_decrypt.keyRx, m_decrypt.ivRx);      // the app encrypts F4 with these
    QByteArray f83 = buildF3(seedDev);
    f83[0] = char(0x83);
    f83[21] = char(checksum(f83.left(21)));
    reply(f83);            // still in clear
    m_encrypted = true;
}

void FakeContecDevice::reply(const QByteArray &frame)
{
    QByteArray out = frame;
    if (m_encrypted) {
        out = buildF4(frame, m_encrypt);
        out[0] = char(0x84);
        out[out.size() - 1] = char(checksum(out.left(out.size() - 1)));
    }
    for (int i = 0; i < out.size(); i += 20) {
        const QByteArray part = out.mid(i, 20);
        QMetaObject::invokeMethod(this, [this, part]() { emit received(part); }, Qt::QueuedConnection);
    }
}

void FakeContecDevice::handle(const QByteArray &cmd)
{
    commands.append(cmd);
    const int h = quint8(cmd[0]);
    if (silentCommands.contains(h)) return;
    if (silentOnceCommand == h) { silentOnceCommand = -1; return; }
    if (rejectCommands.contains(h)) { reply(QByteArray::fromHex("f070")); return; }

    switch (h) {
    case 0x81: {
        QByteArray f1;
        f1.append(char(0xF1));
        f1 += deviceId.left(8);
        reply(f1 + char(checksum(f1)));
        break;
    }
    case 0x82: {
        QByteArray f2;                                   // no strings: exactly 9 bytes
        for (int v : { 0xF2, 0x00, 0x00, 0x02, 0x06, 0x00, m_version, 0x07, 0x00 }) f2.append(char(v));
        reply(f2);
        break;
    }
    case 0x8F: reply(frame({0xFF, 0x00})); break;
    case 0x9F: {
        const bool any = !records.isEmpty();
        reply(frame({0xEF, 0x00, any ? 1 : 0, any ? 0x40 : 0, 0x00, 0x00, 0x00}));
        break;
    }
    case 0x90: {
        const int c = records.size();
        reply(frame({0xE0, 0x06, c & 0x7F, (c >> 7) & 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
        break;
    }
    case 0x8E: reply(frame({0xFE, 0x06, FmtOriginal, 0, 0, 0})); break;
    case 0x9C: {
        const int i = m_nextHeader++;
        if (i >= records.size()) {
            reply(frame({0xEC, 0x40, 1, i + 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
            break;
        }
        const Rec &r = records[i];
        const int n = r.spo2.size();
        const QDate d = r.start.date();
        const QTime t = r.start.time();
        reply(frame({0xEC, i == records.size() - 1 ? 0x40 : 0x00, 1, i + 1,
                     d.year() - 2000, d.month(), d.day(), t.hour(), t.minute(), t.second(),
                     n & 0x7F, (n >> 7) & 0x7F, (n >> 14) & 0x7F, (n >> 21) & 0x7F,
                     'u', 's', 'e', 'r', 0, 0}));
        break;
    }
    case 0x9D: {
        const int sub = quint8(cmd[1]);
        if (sub == 0x7F) {
            erased = true;
            records.clear();
            reply(frame({0xED, 0x7F, 0x7F, 0x7F, 0x7F, 0x00}));
        } else if (sub == 0x03) {
            sendPackets(quint8(cmd[2]), quint8(cmd[4]), int(lo7(cmd, 5, 3)));
        }                                                // 0x02 = abort: stop quietly
        break;
    }
    case 0x83:
        clockSetTo = QDateTime(QDate(2000 + quint8(cmd[1]), quint8(cmd[2]), quint8(cmd[3])),
                               QTime(quint8(cmd[4]), quint8(cmd[5]), quint8(cmd[6])));
        reply(frame({0xF3, 0x00}));
        break;
    default:
        break;
    }
}

void FakeContecDevice::sendPackets(int channel, int m, int offset)
{
    if (m < 1 || m > records.size()) return;
    const QVector<int> &values = (channel == ChSpO2) ? records[m - 1].spo2 : records[m - 1].pulse;
    const int packets = (values.size() + 20) / 21;
    for (int pkt = offset; pkt < packets; ++pkt) {
        QByteArray p(30, '\0');
        p[0] = char(0xED);
        p[1] = char(0x03);
        p[2] = char(channel);
        p[3] = char(pkt & 0x7F);
        p[4] = char((pkt >> 7) & 0x7F);
        for (int j = 0; j < 21; ++j) {
            const int idx = pkt * 21 + j;
            const int v = idx < values.size() ? values[idx] : 0;
            p[8 + j] = char(v & 0x7F);
            if (v & 0x80) p[5 + j / 7] = char(quint8(p[5 + j / 7]) | (1 << (j % 7)));
        }
        p[29] = char(checksum(p.left(29)));
        if (channel == corruptChannel && pkt == corruptPacket) {
            corruptPacket = -1;
            p[29] = char((quint8(p[29]) + 1) & 0x7F);
        }
        reply(p);
    }
}
