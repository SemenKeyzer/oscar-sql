/* O2Ring downloader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef O2RING_DOWNLOADER_H
#define O2RING_DOWNLOADER_H

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include "SleepLib/loader_plugins/o2ring_protocol.h"

//! Byte pipe to an O2Ring: the Bluetooth link in the app, a simulator in the unit tests.
class O2RingLink : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    ~O2RingLink() override = default;
    virtual void write(const QByteArray &data) = 0;
  signals:
    void received(const QByteArray &data);
    void linkLost();
};

/*! \class O2RingDownloader
    \brief Talks to an O2Ring: its details, its clock, then the chosen records piece by piece.
    Asynchronous (QTimer); every command is retried; a record is handed on only whole. Never erases. */
class O2RingDownloader : public QObject
{
    Q_OBJECT
  public:
    explicit O2RingDownloader(QObject *parent = nullptr);

    void setResponseTimeout(int ms) { m_timeoutMs = ms; }
    void setAttempts(int n) { m_attempts = n; }

    //! Asks for the ring's details: infoReceived() or failed().
    void start(O2RingLink *link);
    //! Sets the ring's clock: clockSet().
    void setClock(const QDateTime &local);
    //! Reads the records \a names one after the other: fileDownloaded() or fileFailed() for each, then finished().
    void download(const QStringList &names);
    //! Stops; later answers are ignored.
    void cancel();

  signals:
    void infoReceived(const O2Ring::Info &info);
    void clockSet(bool ok);
    void fileProgress(const QString &name, qint64 done, qint64 total);
    void fileDownloaded(const QString &name, const QByteArray &data);
    void fileFailed(const QString &name, const QString &error);
    void finished();
    void failed(const QString &message);

  private:
    enum class Step { Idle, Info, Sync, Start, Data, End };
    void send(Step step, quint8 cmd, const QByteArray &payload = QByteArray(), quint16 seq = 0);
    void resend();
    void onReceived(const QByteArray &data);
    void onFrame(const O2Ring::Frame &f);
    void onTimeout();
    void giveUp();
    void fileDone(const QString &error);
    void nextFile();
    void onLinkLost();

    QPointer<O2RingLink> m_link;
    O2Ring::FrameReader m_reader;
    QTimer m_timer;
    int m_timeoutMs = 5000;
    int m_attempts = 4;
    int m_tries = 0;
    Step m_step = Step::Idle;
    quint8 m_cmd = 0;
    QByteArray m_payload;
    quint16 m_seq = 0;
    int m_echo = -1;   //!< whether the ring repeats a piece's seq in its answer: -1 not known yet
    QStringList m_queue;
    QString m_name;
    QByteArray m_data;
    qint64 m_size = 0;
    QString m_error;
};

Q_DECLARE_METATYPE(O2Ring::Info)

#endif // O2RING_DOWNLOADER_H
