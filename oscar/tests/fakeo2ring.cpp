/* A simulated O2Ring for the tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "fakeo2ring.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtEndian>

#include "tests/o2ringtestdata.h"

FakeO2Ring::FakeO2Ring(QObject *parent) : O2RingLink(parent) {}

void FakeO2Ring::answer(const QByteArray &payload, quint8 status, quint16 seq, int delayMs)
{
    const QByteArray raw = O2RingTestData::response(payload, status, echoSeq ? seq : 0);
    QTimer::singleShot(latency + delayMs, this, [this, raw]() {
        for (int i = 0; i < raw.size(); i += 20) emit received(raw.mid(i, 20));
    });
}

void FakeO2Ring::write(const QByteArray &data)
{
    if (data.size() < 8 || quint8(data[0]) != 0xAA) return;
    Request r;
    r.cmd = quint8(data[1]);
    r.seq = quint16(quint8(data[3]) | (quint8(data[4]) << 8));
    r.payload = data.mid(7, data.size() - 8);
    requests << r;
    const int n = ++m_count[r.cmd];
    for (const auto &l : std::as_const(lose)) {
        if (l.first == r.cmd && l.second == n) return;
    }
    switch (r.cmd) {
    case O2Ring::CmdInfo: {
        QJsonObject o;
        o.insert(QStringLiteral("SN"), serial);
        o.insert(QStringLiteral("Model"), QStringLiteral("1652"));
        o.insert(QStringLiteral("SoftwareVer"), QStringLiteral("1.13.1"));
        o.insert(QStringLiteral("CurTIME"), clock);
        o.insert(QStringLiteral("CurBAT"), QStringLiteral("65%"));
        o.insert(QStringLiteral("FileList"), QStringList(files.keys()).join(QLatin1Char(',')) + QLatin1Char(','));
        answer(QJsonDocument(o).toJson(QJsonDocument::Compact) + QByteArray(2, '\0'), 0, r.seq);
        break;
    }
    case O2Ring::CmdSync:
        clock = QJsonDocument::fromJson(r.payload).object().value(QStringLiteral("SetTIME")).toString();
        answer(QByteArray(), 0, r.seq);
        break;
    case O2Ring::CmdStart: {
        m_current = QString::fromLatin1(r.payload.left(r.payload.indexOf('\0')));
        QByteArray size(4, '\0');
        qToLittleEndian<quint32>(quint32(startSize >= 0 ? startSize : files.value(m_current).size()), size.data());
        answer(size, 0, r.seq);
        break;
    }
    case O2Ring::CmdData: {
        if (m_current == cutOn) {
            QTimer::singleShot(0, this, [this]() { emit linkLost(); });
            return;
        }
        const QByteArray piece = files.value(m_current).mid(int(r.seq) * chunk, chunk);
        const bool late = m_current == lateFile && int(r.seq) == lateSeq;
        if (late) lateSeq = -1;   // only the first answer is late
        answer(piece, 0, r.seq, late ? lateMs : 0);
        break;
    }
    case O2Ring::CmdEnd:
        answer(QByteArray(), 0, r.seq);
        break;
    default:
        answer(QByteArray(), 1, r.seq);
    }
    if (afterRequest) afterRequest(r);
}
