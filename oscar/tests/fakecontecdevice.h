/* Simulated Contec BLE oximeter for unit tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef FAKECONTECDEVICE_H
#define FAKECONTECDEVICE_H

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QVector>

#include "SleepLib/loader_plugins/contec_ble_downloader.h"

/*! A variant-A oximeter that answers from synthetic records (ORIGINAL format only), with
    optional secure mode (protocol version > 13) and fault injection. Answers are delivered
    asynchronously in notification-sized chunks, like the real link. */
class FakeContecDevice : public ContecBleLink
{
    Q_OBJECT
public:
    struct Rec { QDateTime start; QVector<int> spo2; QVector<int> pulse; };

    explicit FakeContecDevice(int protocolVersion = 13, QObject *parent = nullptr);
    void write(const QByteArray &data) override;
    void dropLink() { emit linkLost(); }

    QList<Rec> records;
    QByteArray deviceId = QByteArrayLiteral("50F     ");
    QSet<int> silentCommands;        //!< never answer these command headers
    QSet<int> rejectCommands;        //!< answer F0 70
    int silentOnceCommand = -1;      //!< ignore this command header the first time only
    int corruptChannel = 0;          //!< corrupt packet corruptPacket of this channel once
    int corruptPacket = -1;

    QList<QByteArray> commands;      //!< every command received (decrypted in secure mode)
    bool sawEncryptedCommand = false;
    bool erased = false;
    QDateTime clockSetTo;

private:
    int commandLength(const QByteArray &buf) const;
    void handle(const QByteArray &cmd);
    void reply(const QByteArray &frame);
    void sendPackets(int channel, int m, int offset);
    void keyExchange(const QByteArray &f3);

    QByteArray m_in;
    int m_version;
    int m_nextHeader = 0;
    bool m_encrypted = false;
    ContecBle::Keys m_decrypt;   //!< app tx keys, used to open F4
    ContecBle::Keys m_encrypt;   //!< app rx keys, used to build 84
};

#endif // FAKECONTECDEVICE_H
