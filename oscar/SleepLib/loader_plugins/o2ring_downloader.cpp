/* O2Ring downloader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "SleepLib/loader_plugins/o2ring_downloader.h"

#include <QDebug>
#include <QtEndian>

O2RingDownloader::O2RingDownloader(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<O2Ring::Info>("O2Ring::Info");
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &O2RingDownloader::onTimeout);
}

void O2RingDownloader::start(O2RingLink *link)
{
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
    m_link = link;
    connect(link, &O2RingLink::received, this, &O2RingDownloader::onReceived);
    connect(link, &O2RingLink::linkLost, this, &O2RingDownloader::onLinkLost);
    send(Step::Info, O2Ring::CmdInfo);
}

void O2RingDownloader::setClock(const QDateTime &local)
{
    send(Step::Sync, O2Ring::CmdSync, O2Ring::timePayload(local));
}

void O2RingDownloader::download(const QStringList &names)
{
    m_queue = names;
    nextFile();
}

void O2RingDownloader::cancel()
{
    m_timer.stop();
    // the ring keeps a read open until it is told the read is over
    if ((m_step == Step::Start || m_step == Step::Data) && m_link) m_link->write(O2Ring::buildRequest(O2Ring::CmdEnd));
    m_step = Step::Idle;
    m_queue.clear();
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
    m_link = nullptr;
}

void O2RingDownloader::send(Step step, quint8 cmd, const QByteArray &payload, quint16 seq)
{
    m_step = step;
    m_cmd = cmd;
    m_payload = payload;
    m_seq = seq;
    m_tries = 0;
    resend();
}

void O2RingDownloader::resend()
{
    if (!m_link) return;
    ++m_tries;
    m_reader.reset();   // an answer to an earlier request is no answer to this one
    m_link->write(O2Ring::buildRequest(m_cmd, m_payload, m_seq));
    m_timer.start(m_timeoutMs);
}

void O2RingDownloader::onReceived(const QByteArray &data)
{
    for (const O2Ring::Frame &f : m_reader.feed(data)) onFrame(f);
}

void O2RingDownloader::onFrame(const O2Ring::Frame &f)
{
    if (m_step == Step::Idle) return;
    if (m_step == Step::Data) {
        // A piece's answer carries its seq when the ring repeats it; seq 0 for a later piece means
        // it does not (then the exact size and the file check are the guard).
        qDebug() << "O2Ring: piece" << m_seq << "answered with seq" << f.seq;
        if (f.seq != m_seq) {
            if (f.seq != 0 || m_echo == 1) return;   // a late answer to another piece
            if (m_seq > 0) m_echo = 0;
        } else if (m_seq > 0) {
            m_echo = 1;
        }
    }
    m_timer.stop();
    if (f.status != 0) {   // refused: ask again
        if (m_tries < m_attempts) resend();
        else giveUp();
        return;
    }
    switch (m_step) {
    case Step::Info: {
        O2Ring::Info info;
        m_step = Step::Idle;
        if (O2Ring::parseInfo(f.payload, &info)) emit infoReceived(info);
        else emit failed(tr("The ring's answer could not be read: this ring is not supported yet."));
        break;
    }
    case Step::Sync:
        m_step = Step::Idle;
        emit clockSet(true);
        break;
    case Step::Start: {
        m_size = f.payload.size() >= 4 ? qFromLittleEndian<quint32>(f.payload.constData()) : 0;
        if (m_size <= 0 || m_size > O2Ring::MaxFileSize) {
            fileDone(tr("the ring reports a size of %1").arg(m_size));
            break;
        }
        m_data.clear();
        send(Step::Data, O2Ring::CmdData, QByteArray(), 0);
        break;
    }
    case Step::Data:
        if (f.payload.isEmpty()) {
            fileDone(tr("empty piece at %1 of %2 bytes").arg(m_data.size()).arg(m_size));
            break;
        }
        m_data += f.payload;
        emit fileProgress(m_name, m_data.size(), m_size);
        if (m_data.size() >= m_size) {
            fileDone(m_data.size() == m_size ? QString() : tr("got %1 bytes, the ring announced %2").arg(m_data.size()).arg(m_size));
        } else {
            send(Step::Data, O2Ring::CmdData, QByteArray(), quint16(m_seq + 1));
        }
        break;
    case Step::End:
        nextFile();
        break;
    case Step::Idle:
        break;
    }
}

void O2RingDownloader::onTimeout()
{
    if (m_tries < m_attempts) resend();
    else giveUp();
}

void O2RingDownloader::giveUp()
{
    switch (m_step) {
    case Step::Info:
        m_step = Step::Idle;
        emit failed(tr("The ring does not answer. Close the phone app and wear the ring to wake it up."));
        break;
    case Step::Sync:
        m_step = Step::Idle;
        emit clockSet(false);
        break;
    case Step::Start:
    case Step::Data:
        fileDone(tr("the ring stopped answering"));
        break;
    case Step::End:
        nextFile();   // the end of reading was not confirmed: go on anyway
        break;
    case Step::Idle:
        break;
    }
}

void O2RingDownloader::fileDone(const QString &error)
{
    m_error = error;
    if (error.isEmpty()) emit fileDownloaded(m_name, m_data);
    else emit fileFailed(m_name, error);
    send(Step::End, O2Ring::CmdEnd);   // always, also after an error
}

void O2RingDownloader::nextFile()
{
    if (m_queue.isEmpty()) {
        m_step = Step::Idle;
        m_timer.stop();
        emit finished();
        return;
    }
    m_name = m_queue.takeFirst();
    m_data.clear();
    m_size = 0;
    send(Step::Start, O2Ring::CmdStart, O2Ring::fileNamePayload(m_name));
}

void O2RingDownloader::onLinkLost()
{
    const bool inFile = m_step == Step::Start || m_step == Step::Data;
    const QString name = m_name;
    cancel();
    if (inFile) emit fileFailed(name, tr("the Bluetooth link was lost"));
    emit failed(tr("The Bluetooth link to the ring was lost."));
}
