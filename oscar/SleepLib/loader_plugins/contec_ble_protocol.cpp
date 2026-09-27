/* Contec BLE Oximeter Protocol
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_protocol.h"

namespace ContecBle {

namespace {

const quint8 kXorMask = 0x56;   // value obfuscation used by protocol versions above 13

struct NamedModel { const char *prefix; const char *model; char variant; };
const NamedModel kModels[] = {
    { "SpO201", "CMS50EW", 'A' }, { "SpO202", "CMS50FW", 'A' }, { "SpO206", "CMS50IW", 'A' },
    { "SpO208", "CMS50D-BT", 'A' }, { "SpO209", "CMS50K", 'K' }, { "SpO210", "CMS50K1", 'K' },
    { "SpO211", "CMS50S", 'A' }, { "SpO212", "CMS60D1", 'A' }, { "SpO213", "CMS50S+", 'A' },
};

int fixedLength(quint8 h)
{
    switch (h) {
    case 0x83: return 22;
    case 0xCF: return 10;
    case 0xD0: return 14;
    case 0xD1: return 4;
    case 0xD2: case 0xD3: case 0xD7: return 20;
    case 0xE1: case 0xE2: return 11;
    case 0xE3: return 9;
    case 0xE4: return 17;
    case 0xE5: return 18;
    case 0xE6: return 17;
    case 0xEA: return 13;
    case 0xEC: return 21;
    case 0xEF: return 8;
    case 0xF0: return 2;
    case 0xF1: return 10;
    case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xFA: case 0xFB: case 0xFF: return 3;
    default: return 0;
    }
}

inline int at(const QByteArray &f, int i) { return quint8(f.at(i)); }

// Puts the high bits kept in hi-bytes back into three groups of seven data bytes (ED 03 / ED 04).
QVector<int> restoreEd(const QByteArray &f)
{
    QVector<int> out;
    out.reserve(21);
    const int groups[3][2] = { { 8, 5 }, { 15, 6 }, { 22, 7 } };
    for (const auto &g : groups) {
        for (int i = 0; i < 7; ++i) {
            out.append((at(f, g[0] + i) & 0x7F) | (((at(f, g[1]) >> i) & 1) << 7));
        }
    }
    return out;
}

} // namespace

ModelInfo modelForName(const QString &advertisedName)
{
    ModelInfo best;
    int bestLength = 0;
    for (const NamedModel &m : kModels) {
        const QString prefix = QString::fromLatin1(m.prefix);
        if (advertisedName.startsWith(prefix) && prefix.size() > bestLength) {
            best.model = QString::fromLatin1(m.model);
            best.variant = m.variant;
            bestLength = prefix.size();
        }
    }
    return best;
}

quint8 checksum(const QByteArray &body)
{
    int sum = 0;
    for (char c : body) sum += quint8(c);
    return quint8(sum & 0x7F);
}

QByteArray frame(std::initializer_list<int> body)
{
    QByteArray b;
    for (int v : body) b.append(char(v & 0xFF));
    b.append(char(checksum(b)));
    return b;
}

void pack7(const QByteArray &data, QByteArray &hi, QByteArray &lo)
{
    hi = QByteArray((data.size() + 6) / 7, '\0');
    lo.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        const int b = quint8(data[i]);
        hi[i / 7] = char(quint8(hi[i / 7]) | (((b >> 7) & 1) << (i % 7)));
        lo[i] = char(b & 0x7F);
    }
}

QByteArray unpack7(const QByteArray &hi, const QByteArray &lo)
{
    QByteArray out(lo.size(), '\0');
    for (int i = 0; i < lo.size(); ++i) {
        const int h = (i / 7 < hi.size()) ? at(hi, i / 7) : 0;
        out[i] = char((at(lo, i) & 0x7F) | (((h >> (i % 7)) & 1) << 7));
    }
    return out;
}

quint32 lo7(const QByteArray &f, int from, int count)
{
    quint32 v = 0;
    for (int i = 0; i < count; ++i) v |= quint32(at(f, from + i) & 0x7F) << (7 * i);
    return v;
}

int frameLength(const QByteArray &buf, char variant)
{
    if (buf.isEmpty()) return 0;
    const quint8 h = quint8(buf[0]);
    if (h < 0x80) return 1;
    if (const int n = fixedLength(h)) return n;
    if (buf.size() < 2) return 0;
    const int b1 = at(buf, 1);
    switch (h) {
    case 0x84: { const int n = b1 & 0x7F; return n + (n + 3 + 6) / 7 + 6; }
    case 0xE0: return (variant == 'A' && (b1 & 7) == 6) ? 15 : 7;
    case 0xEB:
        switch (b1) { case 0x00: return 6; case 0x01: case 0x02: return 8; case 0x7F: return 3; default: return 2; }
    case 0xED:
        switch (b1) { case 0x01: return 24; case 0x03: case 0x04: return 30; case 0x7F: return 7; default: return 2; }
    case 0xFE:
        switch (b1) { case 0x09: return 11; case 0x07: return 5; case 0x06: return 7; default: return 2; }
    case 0xF2: {
        if (buf.size() < 9) return 0;
        const int l1 = at(buf, 8) & 0x7F;
        if (l1 == 0) return 9;
        if (buf.size() < l1 + 11) return 0;
        return l1 + (at(buf, l1 + 10) & 0x7F) + 11;
    }
    default:
        return 1;
    }
}

QList<QByteArray> FrameSplitter::feed(const QByteArray &data)
{
    m_buf += data;
    QList<QByteArray> out;
    while (!m_buf.isEmpty()) {
        const int n = frameLength(m_buf, m_variant);
        if (n == 0 || n > m_buf.size()) break;
        if (n == 1) {              // stray byte or unknown header: resynchronise like the SDK
            ++m_skipped;
            m_buf.remove(0, 1);
            continue;
        }
        out.append(m_buf.left(n));
        m_buf.remove(0, n);
    }
    return out;
}

quint8 formatCode(int format)
{
    switch (format) {
    case FmtDifference: return 0x01;
    case FmtCode: return 0x04;
    default: return 0x03;
    }
}

QByteArray cmdId()           { return frame({0x81}); }
QByteArray cmdInfo()         { return frame({0x82}); }
QByteArray cmdPrepare()      { return frame({0x8F, 0x04, 0x00}); }
QByteArray cmdStorage()      { return frame({0x9F}); }
QByteArray cmdFormats()      { return frame({0x8E, 0x06}); }
QByteArray cmdCountRecords() { return frame({0x90, 0x06}); }
QByteArray cmdNextHeader()   { return frame({0x9C, 0x01}); }

QByteArray cmdChannel(int format, int channel, int l, int m, int offset)
{
    if (format == FmtDifference) {   // 14-bit packet offset
        return frame({0x9D, 0x01, channel, l, m, offset & 0x7F, (offset >> 7) & 0x7F});
    }
    return frame({0x9D, formatCode(format), channel, l, m,   // 21-bit packet offset
                  offset & 0x7F, (offset >> 7) & 0x7F, (offset >> 14) & 0x7F});
}

QByteArray cmdChannelAbort(int channel, int l, int m)
{
    return frame({0x9D, 0x02, channel, l, m, 0x00, 0x00});
}

QByteArray cmdSetTime(const QDateTime &localTime)
{
    const QDate d = localTime.date();
    const QTime t = localTime.time();
    const int ms = t.msec();
    return frame({0x83, (d.year() - 2000) & 0x7F, d.month(), d.day(), t.hour(), t.minute(), t.second(),
                  ms & 0x7F, (ms >> 7) & 0x7F});
}

QByteArray cmdEraseAllRecords()
{
    return frame({0x9D, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00});
}

bool isDestructive(const QByteArray &cmd)
{
    if (cmd.isEmpty()) return false;
    const int h = at(cmd, 0);
    if (h == 0xA1) return true;                                     // erase continuous data
    if (h == 0x9D && cmd.size() >= 2 && at(cmd, 1) == 0x7F) return true;   // erase records
    return (h == 0x91 || h == 0x92 || h == 0x94 || h == 0x96) && cmd.size() == 3 && at(cmd, 1) == 0x7F;
}

int pickFormat(int supported)
{
    for (int f : { int(FmtOriginal), int(FmtCode), int(FmtDifference) }) {
        if (supported & f) return f;
    }
    return FmtDifference;
}

QString parseF1(const QByteArray &f)
{
    QByteArray s;
    for (int i = 1; i < 9 && i < f.size(); ++i) s.append(char(at(f, i) & 0x7F));
    return QString::fromLatin1(s);
}

DeviceInfo parseF2(const QByteArray &f)
{
    DeviceInfo info;
    info.firmware = QStringLiteral("%1.%2.%3").arg(at(f, 3) & 0x7F).arg(at(f, 2) & 0x7F).arg(at(f, 1) & 0x7F);
    info.protocolVersion = at(f, 6) & 0x7F;
    return info;
}

StorageStatus parseEF(const QByteArray &f)
{
    StorageStatus st;
    st.busy = at(f, 1) & 1;
    st.hasData = at(f, 2) & 1;
    st.mask1 = at(f, 3) & 0x7F;
    st.mask2 = at(f, 5) & 0x7F;
    return st;
}

int parseE0Count(const QByteArray &f) { return int(lo7(f, 2, 2)); }
int parseFE06Formats(const QByteArray &f) { return at(f, 2) & 0x7F; }

QDateTime RecordHeader::start() const
{
    const QDate d(year, month, day);
    const QTime t(hour, minute, second);
    if (!d.isValid() || !t.isValid()) return QDateTime();
    return QDateTime(d, t);
}

RecordHeader parseEC(const QByteArray &f)
{
    RecordHeader h;
    h.last = at(f, 1) & 0x40;
    h.hasPI = at(f, 1) & 0x0F;
    h.l = at(f, 2) & 0x7F;
    h.m = at(f, 3) & 0x7F;
    h.year = (at(f, 4) & 0x7F) + 2000;
    h.month = at(f, 5) & 0x0F;
    h.day = at(f, 6) & 0x1F;
    h.hour = at(f, 7) & 0x1F;
    h.minute = at(f, 8) & 0x3F;
    h.second = at(f, 9) & 0x3F;
    h.samples = int(lo7(f, 10, 4));
    return h;
}

int edPacketNo(const QByteArray &f)
{
    return at(f, 1) == 0x01 ? int(lo7(f, 5, 2)) : int(lo7(f, 3, 2));
}

bool checksumOk(const QByteArray &f)
{
    return !f.isEmpty() && checksum(f.left(f.size() - 1)) == quint8(f.back());
}

QVector<int> parseEdOriginal(const QByteArray &f)
{
    return restoreEd(f);
}

// One base value and 13 bytes of nibble deltas -> 27 samples; a delta of 7 means "no data" (255).
QVector<int> parseEdDifference(const QByteArray &f, int protocolVersion)
{
    const int base = 9, hiA = 7, hiB = 8;
    QVector<int> b(f.size());
    for (int i = 0; i < f.size(); ++i) b[i] = at(f, i);
    for (int i = 0; i < 7; ++i) {
        b[base + i] |= ((b[hiA] >> i) & 1) << 7;
        b[base + 7 + i] |= ((b[hiB] >> i) & 1) << 7;
    }
    QVector<int> s;
    s.reserve(27);
    s.append(b[base] ^ (protocolVersion > 13 ? kXorMask : 0));
    for (int k = base + 1; k < base + 14; ++k) {
        const int v = b[k];
        const int deltas[2][2] = { { (v >> 4) & 7, v & 0x80 }, { v & 7, v & 0x08 } };
        for (const auto &d : deltas) {
            s.append(d[0] == 7 ? 255 : (d[1] ? s.last() - d[0] : s.last() + d[0]));
        }
    }
    return s;
}

QVector<int> CodeDecoder::feed(const QByteArray &f)
{
    const QVector<int> b = restoreEd(f);
    QVector<int> out;
    int i = 0;
    if (m_pending >= 0 && !b.isEmpty() && (b[0] & 0xF0) == 0xF0) {
        m_base = ((m_pending & 0x0F) << 4) | (b[0] & 0x0F);
        i = 1;
    }
    m_pending = -1;
    for (; i < b.size(); ++i) {
        const int v = b[i];
        if ((v & 0xF0) == 0xF0) {
            if (i + 1 < b.size()) {
                if ((b[i + 1] & 0xF0) == 0xF0) {
                    m_base = ((v & 0x0F) << 4) | (b[i + 1] & 0x0F);
                    ++i;
                }
            } else {
                m_pending = v;
            }
        } else {
            out.append((m_base - (v >> 4)) & 0xFF);
            if ((v & 0x0F) != 0x0F) out.append((m_base - (v & 0x0F)) & 0xFF);
        }
    }
    return out;
}

} // namespace ContecBle
