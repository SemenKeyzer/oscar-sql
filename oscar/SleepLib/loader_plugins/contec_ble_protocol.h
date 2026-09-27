/* Contec BLE Oximeter Protocol Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_PROTOCOL_H
#define CONTEC_BLE_PROTOCOL_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <initializer_list>

/*! Wire protocol of the Contec CMS50/CMS60 Bluetooth LE oximeters ("variant A":
    CMS50EW/FW/IW/D-BT/S/S+, CMS60D1). Reverse-engineered for interoperability from the vendor
    app com.contec.android.spo2device 3.5.8. Pure functions only: no I/O, no Qt Bluetooth. */
namespace ContecBle {

constexpr quint16 kServiceShortUuid = 0xFF12;   //!< 0000ff12-0000-1000-8000-00805f9b34fb
constexpr quint16 kWriteShortUuid   = 0xFF01;
constexpr quint16 kNotifyShortUuid  = 0xFF02;

struct ModelInfo {
    QString model;       //!< e.g. "CMS50FW"
    char variant = 0;    //!< 'A' or 'K'; 0 when the name is not a known oximeter
    bool isValid() const { return variant != 0; }
};
//! Longest-prefix match of an advertised name ("SpO202…" -> CMS50FW, variant A).
ModelInfo modelForName(const QString &advertisedName);

quint8 checksum(const QByteArray &body);
//! The given bytes followed by their checksum.
QByteArray frame(std::initializer_list<int> body);
//! Splits 8-bit bytes into one "high bits" byte per 7 bytes and the 7-bit low parts.
void pack7(const QByteArray &data, QByteArray &hi, QByteArray &lo);
QByteArray unpack7(const QByteArray &hi, const QByteArray &lo);
//! Little-endian number with 7 bits per byte: f[from] | f[from+1] << 7 | ...
quint32 lo7(const QByteArray &f, int from, int count);

//! Length of the device frame starting at buf[0]: 0 = need more bytes, 1 = skip this byte.
int frameLength(const QByteArray &buf, char variant = 'A');

//! Reassembles device frames from notifications, which may split or merge frames.
class FrameSplitter
{
public:
    explicit FrameSplitter(char variant = 'A') : m_variant(variant) {}
    QList<QByteArray> feed(const QByteArray &data);
    void clear() { m_buf.clear(); }
    int skippedBytes() const { return m_skipped; }
private:
    char m_variant;
    QByteArray m_buf;
    int m_skipped = 0;
};

//! Record formats as reported by FE 06 (bits).
enum Format { FmtDifference = 1, FmtOriginal = 2, FmtCode = 4 };
enum Channel { ChSpO2 = 1, ChPulse = 2, ChPI = 3 };
//! The 9D command / ED answer sub-code of a format: 01, 03 or 04.
quint8 formatCode(int format);

QByteArray cmdId();             //!< 81 01 -> F1
QByteArray cmdInfo();           //!< 82 02 -> F2
QByteArray cmdPrepare();        //!< 8F 04 00 13 -> FF
QByteArray cmdStorage();        //!< 9F 1F -> EF
QByteArray cmdFormats();        //!< 8E 06 14 -> FE 06
QByteArray cmdCountRecords();   //!< 90 06 16 -> E0 06
QByteArray cmdNextHeader();     //!< 9C 01 1D -> EC
QByteArray cmdChannel(int format, int channel, int l, int m, int offset = 0);
QByteArray cmdChannelAbort(int channel, int l, int m);
QByteArray cmdSetTime(const QDateTime &localTime);   //!< 83 YY MM DD hh mi ss msL msH -> F3
QByteArray cmdEraseAllRecords();                      //!< 9D 7F 7F 7F 7F 00 00 19 -> ED 7F
bool isDestructive(const QByteArray &cmd);
//! ORIGINAL (verified on a CMS50FW) if supported, else CODE, else DIFFERENCE.
int pickFormat(int supported);

QString parseF1(const QByteArray &f);
struct DeviceInfo { QString firmware; int protocolVersion = 0; };
DeviceInfo parseF2(const QByteArray &f);
struct StorageStatus {
    bool busy = false;
    bool hasData = false;
    int mask1 = 0;   //!< bit 6: stored records
    int mask2 = 0;
    bool hasRecords() const { return (mask1 & 0x40) != 0; }
};
StorageStatus parseEF(const QByteArray &f);
int parseE0Count(const QByteArray &f);
int parseFE06Formats(const QByteArray &f);

struct RecordHeader {
    bool last = false;
    bool hasPI = false;
    int l = 0;        //!< user number
    int m = 0;        //!< record number
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int samples = 0;  //!< one per second
    //! Start on the oximeter clock (local time); invalid when the device sent impossible fields.
    QDateTime start() const;
};
RecordHeader parseEC(const QByteArray &f);

int edPacketNo(const QByteArray &f);
bool checksumOk(const QByteArray &f);
QVector<int> parseEdOriginal(const QByteArray &f);                        //!< ED 03: 21 samples
QVector<int> parseEdDifference(const QByteArray &f, int protocolVersion); //!< ED 01: 27 samples

//! ED 04 (CODE) decoder; escape pairs may straddle packets, so it keeps state.
class CodeDecoder
{
public:
    QVector<int> feed(const QByteArray &f);
private:
    int m_base = 0;
    int m_pending = -1;
};

//! One stored recording, one sample per second. 127 (SpO2) and 255 (pulse, PI) mean "no data".
struct Record {
    RecordHeader header;
    QVector<int> spo2;
    QVector<int> pulse;
    QVector<int> pi;
};

} // namespace ContecBle

#endif // CONTEC_BLE_PROTOCOL_H
