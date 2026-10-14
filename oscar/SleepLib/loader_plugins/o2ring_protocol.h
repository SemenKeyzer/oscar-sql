/* O2Ring Bluetooth protocol
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RING_PROTOCOL_H
#define O2RING_PROTOCOL_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/*! The Viatom "Oxy" protocol (version 1, unencrypted) of the Wellue / Viatom O2Ring pulse oximeter
    over Bluetooth LE. Pure: frames, checksums and payloads, no input or output.

    Request:  0xAA | cmd    | ~cmd    | seq (2, LE) | len (2, LE) | payload | crc8
    Response: 0x55 | status | ~status | seq (2, LE) | len (2, LE) | payload | crc8 */
namespace O2Ring {

const char *const ServiceUuid = "14839ac4-7d7e-415c-9a42-167340cf2339";
const char *const WriteUuid = "8b00ace7-eb0b-49b0-bbe9-9aee0a26e1a3";
const char *const NotifyUuid = "0734594a-a8e7-4b1a-a6b1-cd5243059a57";
const char *const NamePrefix = "O2Ring ";

enum Command : quint8 {
    CmdStart = 0x03,   //!< begin reading a file: its name (ASCII + 0) → its size, uint32 LE
    CmdData = 0x04,    //!< the next piece of the file; seq counts the pieces from 0
    CmdEnd = 0x05,     //!< end of reading; sent after every read, also after an error
    CmdInfo = 0x14,    //!< the ring's details as JSON (serial, clock, battery, files)
    CmdSync = 0x16,    //!< settings as JSON; here only the clock
};

const int MaxPayload = 8192;          //!< a longer length in a response is a false start
const int MaxFileSize = 2000000;      //!< the ring's own limit
const int HeaderSize = 40;
const int RecordSize = 5;

//! CRC-8, polynomial 0x07, start 0.
quint8 crc8(const QByteArray &data);
QByteArray buildRequest(quint8 cmd, const QByteArray &payload = QByteArray(), quint16 seq = 0);

struct Frame {
    quint8 status = 0;
    quint16 seq = 0;
    QByteArray payload;
};

//! Puts the ring's answers back together from Bluetooth notifications of any size.
class FrameReader
{
  public:
    QList<Frame> feed(const QByteArray &data);
    void reset() { m_buffer.clear(); }

  private:
    QByteArray m_buffer;
};

struct Info {
    QString serial, model, firmware, time, battery;
    QStringList files;   //!< record names, "yyyyMMddHHmmss"
};
//! Reads the ring's details (JSON; trailing NULs ignored); false when it is not JSON.
bool parseInfo(const QByteArray &payload, Info *out);

//! The clock setting: {"SetTIME":"yyyy-MM-dd,HH:mm:ss"} in local time.
QByteArray timePayload(const QDateTime &local);
QByteArray fileNamePayload(const QString &name);

//! Whether \a data is a whole record named \a name: version 3 or 5, the size the header gives
//! (or the 2 s firmware's (n − 40) / 2 + 40), whole 5-byte samples, the header's start = the name.
bool validateFile(const QString &name, const QByteArray &data, QString *error);

} // namespace O2Ring

#endif // O2RING_PROTOCOL_H
