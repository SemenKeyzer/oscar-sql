/* Contec BLE Oximeter Download Session
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "contec_ble_downloader.h"

#include <QDebug>

using namespace ContecBle;

namespace {
const int kMaxAttempts = 5;
const int kEraseTimeoutMs = 20000;
const int kMaxHeaders = 1000;   // safety net if a device never flags its last record
}

ContecBleDownloader::ContecBleDownloader(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ContecBleDownloader::onTimeout);
}

void ContecBleDownloader::start(ContecBleLink *link, const QString &advertisedName)
{
    m_link = link;
    m_model = modelForName(advertisedName);
    if (!m_model.isValid()) {
        fail(tr("%1 is not a known Contec oximeter.").arg(advertisedName));
        return;
    }
    if (m_model.variant != 'A') {
        fail(tr("The %1 is not supported yet.").arg(m_model.model));
        return;
    }
    connect(link, &ContecBleLink::received, this, &ContecBleDownloader::onReceived);
    connect(link, &ContecBleLink::linkLost, this, &ContecBleDownloader::onLinkLost);
    send(cmdId());
    expect(State::WaitId);
}

void ContecBleDownloader::cancel()
{
    ++m_generation;
    m_timer.stop();
    m_state = State::Cancelled;
}

void ContecBleDownloader::setClock(const QDateTime &localNow)
{
    if (m_state != State::Ready) return;
    send(cmdSetTime(localNow));
    expect(State::WaitSetTime);
}

void ContecBleDownloader::eraseAllRecords()
{
    if (m_state != State::Ready) return;
    if (!m_allowDestructive) {
        fail(tr("Refusing to erase the oximeter without permission."));
        return;
    }
    send(cmdEraseAllRecords());
    expect(State::WaitErase, kEraseTimeoutMs);
}

void ContecBleDownloader::send(const QByteArray &cmd)
{
    if (isDestructive(cmd) && !m_allowDestructive) {
        fail(tr("Refusing to erase the oximeter without permission."));
        return;
    }
    qDebug() << "ContecBLE tx" << cmd.toHex(' ');     // commands only, never sample data
    m_link->write(m_encrypted ? buildF4(cmd, m_keys) : cmd);
}

void ContecBleDownloader::expect(State next, int timeoutMs)
{
    m_state = next;
    m_timer.start(timeoutMs < 0 ? m_responseTimeoutMs : timeoutMs);
}

void ContecBleDownloader::fail(const QString &message)
{
    if (stopped()) return;
    ++m_generation;
    m_timer.stop();
    m_state = State::Failed;
    qWarning() << "ContecBLE:" << message;
    emit failed(message);
}

void ContecBleDownloader::onLinkLost()
{
    if (stopped() || m_state == State::Ready || m_state == State::Idle) return;
    fail(tr("The connection to the oximeter was lost."));
}

void ContecBleDownloader::onReceived(const QByteArray &data)
{
    if (stopped()) return;
    for (const QByteArray &f : m_outer.feed(data)) {
        if (stopped()) return;
        if (quint8(f[0]) == 0x84 && m_encrypted) {
            for (const QByteArray &g : m_inner.feed(open84(f, m_keys))) {
                if (stopped()) return;
                onFrame(g);
            }
        } else {
            onFrame(f);
        }
    }
}

void ContecBleDownloader::onFrame(const QByteArray &f)
{
    const int h = quint8(f[0]);
    if (h == 0xF0) {
        fail(tr("The oximeter rejected a command (%1).").arg(QString::fromLatin1(f.toHex(' '))));
        return;
    }
    switch (m_state) {
    case State::WaitId:
        if (h != 0xF1) return;
        m_deviceId = parseF1(f);
        send(cmdInfo());
        expect(State::WaitInfo);
        return;
    case State::WaitInfo: {
        if (h != 0xF2) return;
        const DeviceInfo info = parseF2(f);
        m_version = info.protocolVersion;
        emit deviceIdentified(m_model.model, info.firmware, m_version);
        if (stopped()) return;
        if (m_version > 13) startKeyExchange();
        else requestStorage();
        return;
    }
    case State::WaitSeed:
        if (h != 0x83) return;
        deriveTx(seedFrom83(f), m_deviceId.toLatin1(), m_keys.keyTx, m_keys.ivTx);
        m_encrypted = true;
        requestStorage();
        return;
    case State::WaitStorage:
    case State::WaitStorageRetry: {
        if (h != 0xEF) return;
        const StorageStatus st = parseEF(f);
        if (!st.hasData || !st.hasRecords()) {
            m_total = 0;
            emit recordCountKnown(0);
            if (!stopped()) finishDownload();
            return;
        }
        send(cmdCountRecords());
        expect(State::WaitCount);
        return;
    }
    case State::WaitPrepare:
        if (h != 0xFF) return;
        send(cmdStorage());
        expect(State::WaitStorageRetry);
        return;
    case State::WaitCount:
        if (h != 0xE0) return;
        m_total = parseE0Count(f);
        emit recordCountKnown(m_total);
        if (stopped()) return;
        if (m_total == 0) { finishDownload(); return; }
        send(cmdFormats());
        expect(State::WaitFormats);
        return;
    case State::WaitFormats:
        if (h != 0xFE || f.size() < 3 || quint8(f[1]) != 0x06) return;
        m_format = pickFormat(parseFE06Formats(f));
        requestHeader();
        return;
    case State::WaitHeader:
        if (h == 0xEC) onHeader(f);
        return;
    case State::ReadChannel:
        if (h == 0xED) onChannelPacket(f);
        return;
    case State::WaitSetTime:
        if (h != 0xF3) return;
        m_timer.stop();
        m_state = State::Ready;
        emit clockSet(true);
        return;
    case State::WaitErase:
        if (h != 0xED || f.size() < 6 || quint8(f[1]) != 0x7F) return;
        m_timer.stop();
        m_state = State::Ready;
        emit eraseFinished((quint8(f[5]) & 0x7F) == 0);
        return;
    default:
        return;   // includes RetryPause: frames of the aborted stream are dropped
    }
}

void ContecBleDownloader::onTimeout()
{
    switch (m_state) {
    case State::WaitStorage:        // the vendor app always prepares first; do the same on silence
        send(cmdPrepare());
        expect(State::WaitPrepare);
        return;
    case State::ReadChannel:
        retryChannel();
        return;
    case State::WaitSetTime:
        m_state = State::Ready;
        emit clockSet(false);
        return;
    case State::WaitErase:
        m_state = State::Ready;
        emit eraseFinished(false);
        return;
    case State::Idle: case State::Ready: case State::RetryPause: case State::Failed: case State::Cancelled:
        return;
    default:
        fail(tr("The oximeter didn't answer in time."));
        return;
    }
}

void ContecBleDownloader::startKeyExchange()
{
    const QByteArray seedApp = appSeed(QDateTime::currentDateTime());
    deriveRx(seedApp, m_deviceId.toLatin1(), m_keys.keyRx, m_keys.ivRx);
    const QByteArray f3 = buildF3(seedApp);
    m_link->write(f3.left(18));                 // the vendor app sends 18 bytes, pauses, then 4
    const int generation = m_generation;
    QTimer::singleShot(300, this, [this, f3, generation]() {
        if (generation == m_generation && m_state == State::WaitSeed) m_link->write(f3.mid(18));
    });
    expect(State::WaitSeed);
}

void ContecBleDownloader::requestStorage()
{
    send(cmdStorage());
    expect(State::WaitStorage);
}

void ContecBleDownloader::requestHeader()
{
    if (m_done >= kMaxHeaders) { finishDownload(); return; }
    send(cmdNextHeader());
    expect(State::WaitHeader);
}

void ContecBleDownloader::onHeader(const QByteArray &f)
{
    m_timer.stop();
    const RecordHeader hdr = parseEC(f);
    if (hdr.samples == 0) { finishDownload(); return; }
    const bool want = m_wantRecord ? m_wantRecord(hdr) : true;
    if (m_state != State::WaitHeader) return;   // the callback cancelled us
    m_record = Record();
    m_record.header = hdr;
    if (!want) {
        ++m_done;
        emit progress(m_done, m_total);
        if (stopped()) return;
        if (hdr.last) finishDownload();
        else requestHeader();
        return;
    }
    m_channels = { ChSpO2, ChPulse };
    if (hdr.hasPI) m_channels.append(ChPI);
    m_channelIndex = 0;
    startChannel();
}

void ContecBleDownloader::startChannel()
{
    m_packet = 0;
    m_attempt = 0;
    m_samples.clear();
    m_decoder = CodeDecoder();
    send(cmdChannel(m_format, m_channels.at(m_channelIndex), m_record.header.l, m_record.header.m, 0));
    expect(State::ReadChannel);
}

void ContecBleDownloader::onChannelPacket(const QByteArray &f)
{
    if (f.size() < 2 || quint8(f[1]) != formatCode(m_format)) return;
    const int ch = m_channels.at(m_channelIndex);
    if (!checksumOk(f) || quint8(f[2]) != ch || edPacketNo(f) != m_packet) {
        retryChannel();
        return;
    }
    m_attempt = 0;
    if (m_format == FmtDifference) m_samples += parseEdDifference(f, m_version);
    else if (m_format == FmtOriginal) m_samples += parseEdOriginal(f);
    else m_samples += m_decoder.feed(f);
    ++m_packet;
    if (m_samples.size() < m_record.header.samples) {
        m_timer.start(m_responseTimeoutMs);
        return;
    }
    m_samples.resize(m_record.header.samples);
    if (ch == ChSpO2) m_record.spo2 = m_samples;
    else if (ch == ChPulse) m_record.pulse = m_samples;
    else m_record.pi = m_samples;
    if (++m_channelIndex < m_channels.size()) {
        startChannel();
        return;
    }
    finishRecord();
}

void ContecBleDownloader::retryChannel()
{
    const int ch = m_channels.at(m_channelIndex);
    if (++m_attempt > kMaxAttempts) {
        fail(tr("Downloading stopped: packet %1 of channel %2 kept failing.").arg(m_packet).arg(ch));
        return;
    }
    qDebug() << "ContecBLE retry: channel" << ch << "packet" << m_packet << "attempt" << m_attempt;
    send(cmdChannelAbort(ch, m_record.header.l, m_record.header.m));
    if (stopped()) return;
    m_timer.stop();
    m_state = State::RetryPause;
    const int generation = m_generation;
    QTimer::singleShot(m_retryPauseMs, this, [this, ch, generation]() {
        if (generation != m_generation || m_state != State::RetryPause) return;
        m_outer.clear();
        m_inner.clear();
        if (m_format == FmtCode) {             // CODE state cannot resume mid-stream
            m_samples.clear();
            m_decoder = CodeDecoder();
            m_packet = 0;
        }
        send(cmdChannel(m_format, ch, m_record.header.l, m_record.header.m, m_packet));
        expect(State::ReadChannel);
    });
}

void ContecBleDownloader::finishRecord()
{
    m_timer.stop();
    ++m_done;
    const Record done = m_record;
    emit recordDownloaded(done);
    if (stopped()) return;
    emit progress(m_done, m_total);
    if (stopped()) return;
    if (done.header.last) finishDownload();
    else requestHeader();
}

void ContecBleDownloader::finishDownload()
{
    m_timer.stop();
    m_state = State::Ready;
    emit downloadFinished();
}
