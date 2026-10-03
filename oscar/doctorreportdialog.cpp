/* Doctor Report Dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "doctorreportdialog.h"

#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "SleepLib/profiles.h"
#include "doctorreport.h"
#include "statistics.h"

DoctorReportDialog::DoctorReportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Doctor Report"));
    const QDate first = p_profile->FirstDay();
    QDate last = p_profile->LastDay(MT_CPAP);
    if (!last.isValid()) last = p_profile->LastDay();

    m_from = new QDateEdit(this);
    m_to = new QDateEdit(this);
    for (QDateEdit *edit : { m_from, m_to }) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
        if (first.isValid() && last.isValid()) edit->setDateRange(first, last);
    }
    m_to->setDate(last);
    m_from->setDate(defaultFrom(p_profile->general->doctorReportFrom(), first, last));

    auto *form = new QFormLayout;
    form->addRow(tr("From"), m_from);
    form->addRow(tr("To"), m_to);

    auto *buttons = new QDialogButtonBox(this);
    m_save = buttons->addButton(tr("Save PDF..."), QDialogButtonBox::ActionRole);
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(m_save, &QPushButton::clicked, this, &DoctorReportDialog::save);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_from, &QDateEdit::dateChanged, this, &DoctorReportDialog::updateButtons);
    connect(m_to, &QDateEdit::dateChanged, this, &DoctorReportDialog::updateButtons);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    updateButtons();
}

QDate DoctorReportDialog::defaultFrom(const QDate &saved, const QDate &first, const QDate &last)
{
    if (saved.isValid() && (!first.isValid() || saved >= first) && saved <= last) return saved;
    QDate from = last.addDays(-29);
    if (first.isValid() && from < first) from = first;
    return from;
}

void DoctorReportDialog::updateButtons()
{
    m_save->setEnabled(m_from->date() <= m_to->date());
}

void DoctorReportDialog::save()
{
    const QDate from = m_from->date(), to = m_to->date();
    Statistics stats;
    const DoctorReport report = stats.doctorReport(from, to);
    if (report.nights == 0) {
        QMessageBox::information(this, windowTitle(), tr("There are no CPAP nights between these dates."));
        return;
    }

    const QString name = tr("CPAP report %1 %2–%3.pdf")
                             .arg(p_profile->user->userName(), from.toString(QStringLiteral("dd.MM")),
                                  to.toString(QStringLiteral("dd.MM.yyyy")));
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Doctor Report"), folder + QLatin1Char('/') + name,
                                                      tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;

    QString error;
    if (!DoctorReportPage::writePdf(report, path, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    p_profile->general->setDoctorReportFrom(from);

    QMessageBox done(QMessageBox::Information, windowTitle(), tr("The report is saved."), QMessageBox::Close, this);
    QPushButton *open = done.addButton(tr("Open"), QMessageBox::AcceptRole);
    done.exec();
    if (done.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    accept();
}
