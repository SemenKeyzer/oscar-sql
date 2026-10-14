/* O2Ring Bluetooth protocol
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "SleepLib/loader_plugins/o2ring_protocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace O2Ring {

quint8 crc8(const QByteArray &data)
{
    quint8 crc = 0;
    for (char c : data) {
        crc ^= quint8(c);
        for (int i = 0; i < 8; ++i) crc = (crc & 0x80) ? quint8((crc << 1) ^ 0x07) : quint8(crc << 1);
    }
    return crc;
}

QByteArray buildRequest(quint8 cmd, const QByteArray &payload, quint16 seq)
{
    const int n = int(payload.size());
    QByteArray r;
    r.append(char(0xAA));
    r.append(char(cmd));
    r.append(char(~cmd & 0xFF));
    r.append(char(seq & 0xFF));
    r.append(char(seq >> 8));
    r.append(char(n & 0xFF));
    r.append(char((n >> 8) & 0xFF));
    r.append(payload);
    r.append(char(crc8(r)));
    return r;
}

QList<Frame> FrameReader::feed(const QByteArray &data)
{
    m_buffer.append(data);
    QList<Frame> frames;
    while (true) {
        const qsizetype start = m_buffer.indexOf(char(0x55));
        if (start < 0) {
            m_buffer.clear();
            return frames;
        }
        m_buffer.remove(0, start);
        if (m_buffer.size() < 7) return frames;
        const int n = quint8(m_buffer[5]) | (quint8(m_buffer[6]) << 8);
        if (quint8(m_buffer[1]) != quint8(~quint8(m_buffer[2])) || n > MaxPayload) {   // not a head: look further on
            m_buffer.remove(0, 1);
            continue;
        }
        if (m_buffer.size() < n + 8) return frames;
        const QByteArray raw = m_buffer.left(n + 8);
        if (crc8(raw.left(raw.size() - 1)) != quint8(raw.back())) {
            m_buffer.remove(0, 1);
            continue;
        }
        m_buffer.remove(0, n + 8);
        Frame f;
        f.status = quint8(raw[1]);
        f.seq = quint16(quint8(raw[3]) | (quint8(raw[4]) << 8));
        f.payload = raw.mid(7, n);
        frames.append(f);
    }
}

bool parseInfo(const QByteArray &payload, Info *out)
{
    QByteArray json = payload;
    while (json.endsWith('\0')) json.chop(1);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject o = doc.object();
    auto text = [&o](const char *key) { return o.value(QLatin1String(key)).toVariant().toString(); };
    Info info;
    info.serial = text("SN");
    info.model = text("Model");
    info.firmware = text("SoftwareVer");
    info.time = text("CurTIME");
    info.battery = text("CurBAT");
    for (const QString &f : text("FileList").split(QLatin1Char(','))) {
        if (!f.trimmed().isEmpty()) info.files << f.trimmed();
    }
    if (out) *out = info;
    return true;
}

QByteArray timePayload(const QDateTime &local)
{
    return QByteArrayLiteral("{\"SetTIME\":\"") + local.toString(QStringLiteral("yyyy-MM-dd,HH:mm:ss")).toLatin1()
           + QByteArrayLiteral("\"}");
}

QByteArray fileNamePayload(const QString &name)
{
    return name.toLatin1() + QByteArray(1, '\0');
}

bool validateFile(const QString &name, const QByteArray &data, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (data.size() < HeaderSize) return fail(QStringLiteral("shorter than the 40-byte header"));
    const int version = quint8(data[0]);
    if (version != 3 && version != 5) return fail(QStringLiteral("unsupported file version %1").arg(version));
    const quint32 size = qFromLittleEndian<quint32>(data.constData() + 9);
    const quint32 n = quint32(data.size());
    if (size != n && size != (n - HeaderSize) / 2 + HeaderSize) return fail(QStringLiteral("size field %1 != %2 bytes").arg(size).arg(n));
    if ((n - HeaderSize) % RecordSize) return fail(QStringLiteral("the samples are not whole 5-byte records"));
    const int year = qFromLittleEndian<quint16>(data.constData() + 2);
    const QDateTime start(QDate(year, quint8(data[4]), quint8(data[5])), QTime(quint8(data[6]), quint8(data[7]), quint8(data[8])));
    if (!start.isValid()) return fail(QStringLiteral("bad start time in the header"));
    if (start.toString(QStringLiteral("yyyyMMddHHmmss")) != name) return fail(QStringLiteral("header start differs from the name"));
    return true;
}

} // namespace O2Ring
