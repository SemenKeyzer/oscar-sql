/* Doctor Report Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DOCTORREPORTDIALOG_H
#define DOCTORREPORTDIALOG_H

#include <QDate>
#include <QDialog>

class QDateEdit;
class QPushButton;

//! Picks the dates of the report for the doctor and saves it as a PDF.
class DoctorReportDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit DoctorReportDialog(QWidget *parent = nullptr);

    //! The first date to offer: \a saved when it lies within the data, otherwise the last
    //! 30 days up to \a last, never before \a first.
    static QDate defaultFrom(const QDate &saved, const QDate &first, const QDate &last);

  private slots:
    void save();
    void updateButtons();

  private:
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
    QPushButton *m_save = nullptr;
};

#endif // DOCTORREPORTDIALOG_H
