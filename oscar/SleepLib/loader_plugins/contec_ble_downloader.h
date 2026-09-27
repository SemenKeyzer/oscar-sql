/* Contec BLE Oximeter Download Session Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CONTEC_BLE_DOWNLOADER_H
#define CONTEC_BLE_DOWNLOADER_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "SleepLib/loader_plugins/contec_ble_protocol.h"

//! Byte pipe to an oximeter: the Bluetooth link in the app, a simulator in the unit tests.
class ContecBleLink : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ContecBleLink() override = default;
    //! Sends raw bytes (already framed, and encrypted in secure mode).
    virtual void write(const QByteArray &data) = 0;
signals:
    void received(const QByteArray &data);
    void linkLost();
};

/*! \class ContecBleDownloader
    \brief Runs a Contec variant-A session: handshake, optional key exchange, then every stored
    record the owner wants. Asynchronous (QTimer, no blocking waits). Never erases anything
    unless allowDestructive(true) was called first. */
class ContecBleDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ContecBleDownloader(QObject *parent = nullptr);

    void setWantRecord(std::function<bool(const ContecBle::RecordHeader &)> want) { m_wantRecord = std::move(want); }
    void setResponseTimeout(int ms) { m_responseTimeoutMs = ms; }
    void setRetryPause(int ms) { m_retryPauseMs = ms; }
    void allowDestructive(bool allow) { m_allowDestructive = allow; }

    void start(ContecBleLink *link, const QString &advertisedName);
    //! Stops silently; later answers are ignored.
    void cancel();
    //! After downloadFinished(): sets the oximeter clock; answers with clockSet().
    void setClock(const QDateTime &localNow);
    //! After downloadFinished(): erases every stored record; needs allowDestructive(true).
    void eraseAllRecords();

    bool isEncrypted() const { return m_encrypted; }

signals:
    void deviceIdentified(const QString &model, const QString &firmware, int protocolVersion);
    void recordCountKnown(int count);
    void recordDownloaded(const ContecBle::Record &record);
    void progress(int done, int total);
    void downloadFinished();
    void clockSet(bool ok);
    void eraseFinished(bool ok);
    void failed(const QString &message);

private:
    enum class State { Idle, WaitId, WaitInfo, WaitSeed, WaitStorage, WaitPrepare, WaitStorageRetry,
                       WaitCount, WaitFormats, WaitHeader, ReadChannel, RetryPause, Ready,
                       WaitSetTime, WaitErase, Failed, Cancelled };

    void send(const QByteArray &cmd);
    void expect(State next, int timeoutMs = -1);
    void onReceived(const QByteArray &data);
    void onFrame(const QByteArray &f);
    void onTimeout();
    void onLinkLost();
    void fail(const QString &message);
    void startKeyExchange();
    void requestStorage();
    void requestHeader();
    void onHeader(const QByteArray &f);
    void startChannel();
    void onChannelPacket(const QByteArray &f);
    void retryChannel();
    void finishRecord();
    void finishDownload();
    bool stopped() const { return m_state == State::Failed || m_state == State::Cancelled; }

    ContecBleLink *m_link = nullptr;
    ContecBle::ModelInfo m_model;
    State m_state = State::Idle;
    QTimer m_timer;
    int m_generation = 0;
    int m_responseTimeoutMs = 5000;
    int m_retryPauseMs = 500;
    bool m_allowDestructive = false;
    std::function<bool(const ContecBle::RecordHeader &)> m_wantRecord;

    ContecBle::FrameSplitter m_outer{'A'};
    ContecBle::FrameSplitter m_inner{'A'};
    bool m_encrypted = false;
    ContecBle::Keys m_keys;
    QString m_deviceId;
    int m_version = 0;

    int m_total = 0;
    int m_done = 0;
    int m_format = ContecBle::FmtOriginal;
    ContecBle::Record m_record;
    QList<int> m_channels;
    int m_channelIndex = 0;
    int m_packet = 0;
    int m_attempt = 0;
    QVector<int> m_samples;
    ContecBle::CodeDecoder m_decoder;
};

#endif // CONTEC_BLE_DOWNLOADER_H
