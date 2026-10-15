/* ViHealth CSV Oximetry Loader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "vihealth_csv_loader.h"

#include <QDebug>
#include <QFile>
#include <QLocale>

namespace {

const int kNewSessionGapSecs = 360;   // a longer break starts a new session, as in the MD300W1 loader

struct Row {
    QDateTime time;
    quint8 spo2 = 0;
    quint8 pulse = 0;
};

//! "23:37:38 Oct 13 2026,96,92,2,0,0" (the time is in English whatever the app's language).
bool parseRow(const QString &line, Row *row)
{
    const QStringList f = line.split(QLatin1Char(','));
    if (f.size() < 3) return false;
    const QDateTime t = QLocale::c().toDateTime(f[0].trimmed(), QStringLiteral("HH:mm:ss MMM d yyyy"));
    if (!t.isValid()) return false;
    row->time = t;
    bool ok = false;
    const int spo2 = f[1].trimmed().toInt(&ok);
    row->spo2 = (ok && spo2 > 0 && spo2 <= 100) ? quint8(spo2) : 0;   // "--" or 255: no reading
    const int pulse = f[2].trimmed().toInt(&ok);
    row->pulse = (ok && pulse > 0 && pulse < 255) ? quint8(pulse) : 0;
    return true;
}

} // namespace

ViHealthCsvLoader::ViHealthCsvLoader()
{
    m_type = MT_OXIMETER;
    m_abort = false;
    m_streaming = false;
    m_importing = false;
    startTimer.setParent(this);
    resetTimer.setParent(this);
}

int ViHealthCsvLoader::Open(const QString &path)
{
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) return 0;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

    QList<Row> rows;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        Row row;
        if (parseRow(line, &row)) rows << row;
        else if (i > 0) return 0;   // only the header line may be something else
    }
    if (rows.size() < 2) return 0;

    qint64 step = rows[0].time.secsTo(rows[1].time);
    if (step < 1 || step > 60) step = 4;
    m_stepMs = step * 1000;

    trashRecords();
    QDateTime last;
    for (const Row &row : std::as_const(rows)) {
        const qint64 gap = last.isValid() ? last.secsTo(row.time) : -1;
        if (last.isValid() && gap <= 0) continue;   // a repeated time
        if (!last.isValid() || gap > kNewSessionGapSecs) {
            oxirec = new QVector<OxiRecord>;
            oxisessions[row.time] = oxirec;
        } else {
            for (qint64 j = (gap + step / 2) / step - 1; j > 0; --j) oxirec->append(OxiRecord(0, 0));
        }
        oxirec->append(OxiRecord(row.pulse, row.spo2));
        last = row.time;
    }
    m_startTime = oxisessions.firstKey();
    qDebug() << "ViHealth CSV:" << path << rows.size() << "lines," << oxisessions.size() << "sessions, every" << step << "s";
    return 1;
}

static bool ViHealthCsv_initialized = false;

void ViHealthCsvLoader::Register()
{
    if (ViHealthCsv_initialized) return;
    RegisterLoader(new ViHealthCsvLoader());
    ViHealthCsv_initialized = true;
}
