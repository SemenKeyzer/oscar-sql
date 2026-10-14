/* A simulated O2Ring for the tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef FAKEO2RING_H
#define FAKEO2RING_H

#include <QHash>
#include <functional>
#include <QList>
#include <QMap>

#include "SleepLib/loader_plugins/o2ring_downloader.h"
#include "SleepLib/loader_plugins/o2ring_protocol.h"

//! Answers the downloader as an O2Ring would, from records in memory, in 20-byte notifications;
//! can lose answers, answer late, cut the link or announce a wrong size.
class FakeO2Ring : public O2RingLink
{
    Q_OBJECT
  public:
    struct Request {
        quint8 cmd;
        quint16 seq;
        QByteArray payload;
    };
    explicit FakeO2Ring(QObject *parent = nullptr);
    void write(const QByteArray &data) override;

    QMap<QString, QByteArray> files;
    QString serial = QStringLiteral("2682C06554");
    QString clock = QStringLiteral("2026-10-14,07:00:00");
    int chunk = 512;
    bool echoSeq = true;
    //! the n-th (from 1) request of a command gets no answer
    QList<QPair<quint8, int>> lose;
    //! DATA of this file at this seq: the first answer comes only after lateMs (the retry is answered at once)
    QString lateFile;
    int lateSeq = -1;
    int lateMs = 0;
    //! every answer comes this much later, in the order asked (a ring that answers one request after another)
    int latency = 0;
    //! DATA of this file: the link is lost
    QString cutOn;
    //! START answers this size instead of the real one (-1: real)
    int startSize = -1;

    QList<Request> requests;
    //! called after each request has been answered (or not)
    std::function<void(const Request &)> afterRequest;

  private:
    void answer(const QByteArray &payload, quint8 status, quint16 seq, int delayMs = 0);
    QHash<quint8, int> m_count;
    QString m_current;
};

#endif // FAKEO2RING_H
