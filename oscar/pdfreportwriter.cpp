/* PDF Report Writer
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "pdfreportwriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>

#include "SleepLib/profiles.h"
#include "SleepLib/progressdialog.h"
#include "common_gui.h"
#include "daily.h"
#include "doctorreport.h"
#include "htmlpages.h"
#include "overview.h"
#include "reports.h"
#include "statistics.h"

namespace {

QString tr(const char *text) { return QCoreApplication::translate("PdfReportWriter", text); }

// Puts the Daily tab back on the day it showed, however the report ends.
struct DailyRestore {
    Daily *daily;
    QDate date;
    explicit DailyRestore(Daily *d) : daily(d), date(d->getDate()) {}
    ~DailyRestore() { if (date.isValid()) daily->LoadDateNow(date); }
};

// Puts the Overview back on its dates and set of graphs, however the report ends.
struct OverviewRestore {
    Overview *overview;
    QDate start, end;
    OverviewPresets::Preset preset;
    explicit OverviewRestore(Overview *o)
        : overview(o), start(o->displayedStart()), end(o->displayedEnd()), preset(o->currentPreset()) {}
    ~OverviewRestore()
    {
        overview->showPreset(preset);
        if (start.isValid() && end.isValid()) overview->setRange(start, end);
    }
};

} // namespace

PdfReportWriter::PdfReportWriter(Daily *daily, Overview *overview) : m_daily(daily), m_overview(overview) {}

bool PdfReportWriter::write(const PdfReportOptions &o, const QDate &lastNight, const QString &path,
                            ProgressDialog *progress, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (m_cancelled) return false;

    const QPair<QDate, QDate> range = o.range(lastNight);
    QList<QDate> cpapNights;
    for (QDate d = range.first; d.isValid() && d <= range.second; d = d.addDays(1)) {
        Day *day = p_profile->GetDay(d, MT_CPAP);
        if (day && day->hours(MT_CPAP) > 0) cpapNights << d;
    }
    if (cpapNights.isEmpty()) return fail(tr("There are no CPAP nights between these dates."));

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
    QPainter painter;
    if (!painter.begin(&printer)) {
        QFile::remove(path);
        return fail(tr("Could not write %1.").arg(path));
    }

    bool anyPage = false;
    bool ok = true;
    QString failure;   // set when a section cannot be printed as asked
    auto section = [&](const QString &message) {
        if (progress) {
            progress->setMessage(message);
            QCoreApplication::processEvents();
        }
        return !m_cancelled;
    };
    auto nextPage = [&]() {
        if (anyPage) ok = ok && printer.newPage();
        anyPage = true;
    };

    if (o.summary && section(tr("Summary page..."))) {
        Statistics stats;
        const DoctorReport report = stats.doctorReport(range.first, range.second, o.personalData, o.serialNumbers);
        DoctorReportPage::paintPage(painter, printer, report, anyPage);
        anyPage = true;
    }

    if (o.daily && m_daily && !m_cancelled) {
        DailyRestore restore(m_daily);
        const QList<QDate> nights = o.nightsToPrint(cpapNights);
        for (int i = 0; ok && i < nights.size() && section(tr("Night %1 of %2...").arg(i + 1).arg(nights.size())); ++i) {
            m_daily->LoadDateNow(nights[i]);
            if (m_daily->getDate() != nights[i]) {
                // the Daily tab kept another day (a time alignment was not closed): its figures
                // must not be printed under this night's date
                failure = tr("Could not show the night of %1 on the Daily tab.")
                              .arg(QLocale().toString(nights[i], QLocale::ShortFormat));
                ok = false;
                break;
            }
            nextPage();
            PrintTarget target;
            target.personalData = o.personalData;
            target.progress = progress;
            ok = Report::paint(painter, printer, m_daily->graphView(), STR_TR_Daily, nights[i], target) && ok;
        }
    }

    if (ok && o.overview && m_overview && section(tr("Overview..."))) {
        OverviewRestore restore(m_overview);
        // from the period's first CPAP night to its last: no empty weeks of graphs
        const QPair<QDate, QDate> shown = PdfReportOptions::clamped(range, cpapNights.first(), cpapNights.last());
        QDate from = shown.first, to = shown.second;
        m_overview->showPreset(o.overviewPreset);
        m_overview->setRange(from, to);
        nextPage();
        PrintTarget target;
        target.personalData = o.personalData;
        target.progress = progress;
        ok = Report::paint(painter, printer, m_overview->graphView(), STR_TR_Overview, to, target) && ok;
    }

    if (ok && o.statistics && section(tr("Statistics..."))) {
        StatisticsSections s;
        s.settingsChanges = o.statsSettings;
        s.oximetry = o.statsOximetry;
        s.devices = o.statsDevices;
        s.personalData = o.personalData;
        s.serialNumbers = o.serialNumbers;
        Statistics stats;
        QFont font(QStringLiteral("Helvetica"));
        font.setPointSizeF(8);
        paintHtmlPages(painter, printer, stats.periodHtml(range.first, range.second, s), font, {}, anyPage);
        anyPage = true;
    }

    painter.end();
    if (m_cancelled) {
        QFile::remove(path);
        return false;
    }
    const QFileInfo written(path);
    if (!ok || printer.printerState() == QPrinter::Error || !written.exists() || written.size() == 0) {
        QFile::remove(path);
        return fail(failure.isEmpty() ? tr("Could not write %1.").arg(path) : failure);
    }
    return true;
}
