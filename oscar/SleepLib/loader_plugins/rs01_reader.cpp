/* Contec RS01 Reader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01_reader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>

// Format: the RS01 format notes (O2Rig, verified on the owner's device), §2.

namespace {

constexpr int kHeaderMin = 138;       // fields before the per-signal ones
constexpr int kPerSignalBytes = 88;   // label 8, samples 4, bytes 4, unit 8, 4 × 8 scale, 32 unknown

QString text(const QByteArray &b, int at, int len)
{
    return QString::fromLatin1(b.mid(at, len)).trimmed();
}

int number(const QByteArray &b, int at, int len, bool *ok)
{
    bool good = false;
    const int v = text(b, at, len).toInt(&good);
    if (!good) *ok = false;
    return v;
}

double real(const QByteArray &b, int at, int len, bool *ok)
{
    bool good = false;
    const double v = text(b, at, len).toDouble(&good);
    if (!good) *ok = false;
    return v;
}

RS01Header invalid(const QString &why)
{
    RS01Header h;
    h.error = why;
    return h;
}

// One second's raw samples per signal, at its offset from the start.
struct RawRecord {
    int second = 0;
    QVector<QVector<int>> values;   // per signal
};

int segmentNumber(const QString &file)
{
    bool ok = false;
    const int n = QFileInfo(file).completeBaseName().toInt(&ok);
    return ok ? n : -1;
}

} // namespace

int RS01Header::recordBytes() const
{
    int bytes = prefixBytes + suffixBytes;
    for (const RS01Signal &s : channels) bytes += s.samplesPerRecord * s.bytesPerSample;
    return bytes;
}

int RS01Header::indexOf(const QString &label) const
{
    for (int i = 0; i < channels.size(); ++i) {
        if (channels[i].label.compare(label, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

RS01Header rs01ParseHeader(const QByteArray &hd)
{
    if (hd.size() < kHeaderMin) return invalid(QStringLiteral("header too short (%1 bytes)").arg(hd.size()));
    RS01Header h;
    h.start = QDateTime::fromString(text(hd, 22, 14), QStringLiteral("yyyyMMddHHmmss"));
    if (!h.start.isValid()) return invalid(QStringLiteral("no start time in the header"));
    bool ok = true;
    h.segHeaderBytes = number(hd, 116, 4, &ok);
    h.recordsPerSeg = number(hd, 120, 4, &ok);
    h.recordSeconds = number(hd, 124, 4, &ok);
    h.prefixBytes = number(hd, 128, 4, &ok);
    h.suffixBytes = number(hd, 132, 4, &ok);
    const int ns = number(hd, 136, 2, &ok);
    if (!ok || ns < 1 || ns > 16) return invalid(QStringLiteral("unreadable record layout in the header"));
    if (hd.size() < kHeaderMin + ns * kPerSignalBytes) return invalid(QStringLiteral("header too short for %1 signals").arg(ns));
    const int base = kHeaderMin;
    for (int i = 0; i < ns; ++i) {
        RS01Signal s;
        s.label = text(hd, base + 8 * i, 8);
        s.samplesPerRecord = number(hd, base + 8 * ns + 4 * i, 4, &ok);
        s.bytesPerSample = number(hd, base + 12 * ns + 4 * i, 4, &ok);
        s.unit = text(hd, base + 16 * ns + 8 * i, 8);
        s.digMax = real(hd, base + 24 * ns + 8 * i, 8, &ok);
        s.digMin = real(hd, base + 32 * ns + 8 * i, 8, &ok);
        s.physMax = real(hd, base + 40 * ns + 8 * i, 8, &ok);
        s.physMin = real(hd, base + 48 * ns + 8 * i, 8, &ok);
        if (s.samplesPerRecord < 1 || (s.bytesPerSample != 1 && s.bytesPerSample != 2) || s.digMax == s.digMin) ok = false;
        h.channels << s;
    }
    if (!ok || h.segHeaderBytes < 3 || h.recordsPerSeg < 1 || h.recordSeconds < 1 || h.prefixBytes < 4 || h.recordBytes() <= 0)
        return invalid(QStringLiteral("unreadable signal description in the header"));
    h.valid = true;
    return h;
}

RS01Recording rs01ReadRecording(const QString &folder)
{
    RS01Recording r;
    r.name = QFileInfo(folder).fileName();
    QFile hdFile(QDir(folder).filePath(r.name + QStringLiteral(".HD")));
    if (!hdFile.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("no header %1.HD").arg(r.name);
        return r;
    }
    r.header = rs01ParseHeader(hdFile.readAll());
    if (!r.header.valid) {
        r.error = r.header.error;
        return r;
    }
    const RS01Header &h = r.header;
    const int ns = h.channels.size();
    const int recordBytes = h.recordBytes();

    // the segments in number order; a missing number ends the recording (later ones would be misplaced)
    QStringList segs = QDir(folder).entryList({ QStringLiteral("*.SEG"), QStringLiteral("*.seg") }, QDir::Files);
    std::sort(segs.begin(), segs.end(), [](const QString &a, const QString &b) { return segmentNumber(a) < segmentNumber(b); });
    QVector<RawRecord> raw;
    int expected = 1;
    for (const QString &name : segs) {
        const int number = segmentNumber(name);
        if (number < 1) continue;   // not a segment ("001 copy.SEG")
        if (number != expected) break;
        QFile f(QDir(folder).filePath(name));
        if (!f.open(QIODevice::ReadOnly)) break;
        const QByteArray seg = f.readAll();
        if (seg.size() < h.segHeaderBytes || seg.left(3).toInt() != number) break;
        const int records = (seg.size() - h.segHeaderBytes) / recordBytes;
        for (int k = 0; k < records; ++k) {
            const char *p = seg.constData() + h.segHeaderBytes + k * recordBytes;
            const quint16 index = qFromLittleEndian<quint16>(p + 2);
            if (quint8(p[0]) != 0xFF || quint8(p[1]) != 0xFF || index < 1 || index > h.recordsPerSeg) {
                ++r.missingSeconds;
                continue;
            }
            RawRecord rec;
            rec.second = ((number - 1) * h.recordsPerSeg + (index - 1)) * h.recordSeconds;
            rec.values.resize(ns);
            const char *at = p + h.prefixBytes;
            for (int s = 0; s < ns; ++s) {
                const RS01Signal &sig = h.channels[s];
                QVector<int> &v = rec.values[s];
                v.reserve(sig.samplesPerRecord);
                for (int j = 0; j < sig.samplesPerRecord; ++j) {
                    v << (sig.bytesPerSample == 2 ? int(qFromLittleEndian<quint16>(at)) : int(quint8(*at)));
                    at += sig.bytesPerSample;
                }
            }
            raw << rec;
        }
        ++expected;
    }
    if (raw.isEmpty()) {
        r.error = segs.isEmpty() ? QStringLiteral("no segments (.SEG)") : QStringLiteral("no readable records");
        return r;
    }

    // the flow's zero: the recording's median (not mid-scale)
    const int flowIndex = h.indexOf(QStringLiteral("Flow"));
    double flowZero = 0;
    if (flowIndex >= 0) {
        QVector<int> all;
        for (const RawRecord &rec : raw) all << rec.values[flowIndex];
        std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
        flowZero = all[all.size() / 2];
    }

    const qint64 start = h.start.toMSecsSinceEpoch();
    for (int s = 0; s < ns; ++s) {
        const RS01Signal &sig = h.channels[s];
        const double scale = (sig.physMax - sig.physMin) / (sig.digMax - sig.digMin);
        const bool isFlow = s == flowIndex;
        // SpO2 or pulse at 0: no signal
        const bool zeroIsGap = sig.label.compare(QStringLiteral("SpO2"), Qt::CaseInsensitive) == 0
                            || sig.label.compare(QStringLiteral("PR"), Qt::CaseInsensitive) == 0;
        const double interval = 1000.0 * h.recordSeconds / sig.samplesPerRecord;
        QVector<RS01Run> &runs = r.runs[sig.label];
        bool open = false;     // a run goes on while each sample comes when the last one said it would
        double expect = 0;
        for (const RawRecord &rec : raw) {
            for (int j = 0; j < rec.values[s].size(); ++j) {
                const int d = rec.values[s][j];
                if (zeroIsGap && d == 0) {
                    open = false;
                    continue;
                }
                const double t = double(start) + rec.second * 1000.0 + j * interval;
                if (!open || qAbs(t - expect) > interval / 2) runs.append(RS01Run { qint64(t), interval, {} });
                open = true;
                expect = t + interval;
                runs.last().samples << (isFlow ? float(-(d - flowZero) * scale)   // inspiration (lower values) positive
                                               : float(sig.physMin + (d - sig.digMin) * scale));
            }
        }
    }
    r.seconds = raw.last().second + h.recordSeconds;
    r.valid = true;
    return r;
}

QStringList rs01FindRecordings(const QString &root)
{
    static const QRegularExpression kName(QStringLiteral("^\\d{8}$"));
    QStringList out;
    for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!kName.match(name).hasMatch()) continue;
        const QString dir = QDir(root).filePath(name);
        if (QFileInfo::exists(QDir(dir).filePath(name + QStringLiteral(".HD")))) out << dir;
    }
    return out;
}
