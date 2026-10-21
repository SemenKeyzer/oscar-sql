/* Synthetic Contec RS01 recordings for the tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RS01_SYNTH_H
#define RS01_SYNTH_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QtEndian>
#include <functional>

//! Builders of RS01 .HD and .SEG bytes as the device writes them (format doc §2).
namespace rs01synth {

inline QByteArray field(const QString &s, int len)
{
    QByteArray b = s.toLatin1().left(len);
    return b + QByteArray(len - b.size(), ' ');
}

//! A 506-byte header: start "yyyyMMddHHmmss", pleth at \a plethHz.
inline QByteArray header(const QString &start = QStringLiteral("20261017015053"), int plethHz = 100)
{
    QByteArray b = field(QStringLiteral("110"), 3) + field(QStringLiteral("160"), 3) + field(QString(), 16)
                 + field(start, 14) + QByteArray(16, '\0') + QByteArray(32, '\x5a') + field(QString(), 32)
                 + field(QStringLiteral("512"), 4) + field(QStringLiteral("600"), 4) + field(QStringLiteral("1"), 4)
                 + field(QStringLiteral("4"), 4) + field(QStringLiteral("0"), 4) + field(QStringLiteral("4"), 2);
    const QStringList labels { "Flow", "PR", "SpO2", "Pleth" }, units { "Pa", "PerMin", "Percent", "" };
    const QList<int> samples { 50, 1, 1, plethHz }, bytes { 2, 1, 1, 1 };
    const QStringList dmax { "4095", "250", "100", "127" }, dmin { "0", "0", "0", "0" };
    const QStringList pmax { "511", "250", "100", "100" }, pmin { "-511", "0", "0", "0" };
    for (const QString &l : labels) b += field(l, 8);
    for (int s : samples) b += field(QString::number(s), 4);
    for (int s : bytes) b += field(QString::number(s), 4);
    for (const QString &u : units) b += field(u, 8);
    for (const QString &v : dmax) b += field(v, 8);
    for (const QString &v : dmin) b += field(v, 8);
    for (const QString &v : pmax) b += field(v, 8);
    for (const QString &v : pmin) b += field(v, 8);
    b += field(QStringLiteral("1   1   1   1"), 32 * 4);
    b += field(QString(), 16);
    return b;
}

//! One 1-s record: flow (50 samples), PR, SpO2, pleth (\a plethHz samples); \a marker false: no FF FF.
inline QByteArray record(int index, int flow, int pr, int spo2, int pleth = 60, int plethHz = 100, bool marker = true)
{
    QByteArray r;
    r += marker ? QByteArray("\xff\xff", 2) : QByteArray("\x00\x00", 2);
    char w[2];
    qToLittleEndian<quint16>(quint16(index), w);
    r += QByteArray(w, 2);
    for (int i = 0; i < 50; ++i) {
        qToLittleEndian<quint16>(quint16(flow), w);
        r += QByteArray(w, 2);
    }
    r += char(pr);
    r += char(spo2);
    r += QByteArray(plethHz, char(pleth));
    return r;
}

//! A segment: its number, then \a records seconds made by \a make(index).
inline QByteArray segment(int number, int records, const std::function<QByteArray(int)> &make)
{
    QByteArray s = QString::number(number).rightJustified(3, '0').toLatin1();
    s += QByteArray(512 - 3, '\0');
    for (int i = 1; i <= records; ++i) s += make(i);
    return s;
}

inline void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
}

//! A recording folder <root>/<name>/ with its header and the given segments (number → bytes).
inline QString recording(const QString &root, const QString &name, const QByteArray &hd, const QList<QPair<int, QByteArray>> &segs)
{
    const QString dir = QDir(root).filePath(name);
    QDir().mkpath(dir);
    write(QDir(dir).filePath(name + QStringLiteral(".HD")), hd);
    for (const auto &s : segs) write(QDir(dir).filePath(QString::number(s.first).rightJustified(3, '0') + QStringLiteral(".SEG")), s.second);
    return dir;
}

} // namespace rs01synth

#endif // RS01_SYNTH_H
