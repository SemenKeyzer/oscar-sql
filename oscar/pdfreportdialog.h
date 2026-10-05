/* PDF Report Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PDFREPORTDIALOG_H
#define PDFREPORTDIALOG_H

#include <QDate>
#include <QDialog>
#include <QList>

#include "pdfreportoptions.h"

class Daily;
class Overview;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QRadioButton;

//! «Создать PDF-отчёт»: picks the period and sections and writes one PDF.
class PdfReportDialog : public QDialog
{
    Q_OBJECT
  public:
    PdfReportDialog(Daily *daily, Overview *overview, QWidget *parent = nullptr);

    //! What the window shows now.
    PdfReportOptions options() const;

  public slots:
    //! Esc and the close button do nothing while the report is being written.
    void reject() override;

  private slots:
    void update();
    void create();

  private:
    void setOptions(const PdfReportOptions &o);
    QList<QDate> cpapNights(const QPair<QDate, QDate> &range) const;

    Daily *m_daily;
    Overview *m_overview;
    QDate m_lastNight;
    bool m_loading = false;
    bool m_writing = false;

    QButtonGroup *m_period = nullptr;
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
    QCheckBox *m_summary = nullptr;
    QCheckBox *m_daily_ = nullptr;
    QButtonGroup *m_nights = nullptr;
    QRadioButton *m_allNights = nullptr;
    QCheckBox *m_overview_ = nullptr;
    QComboBox *m_overviewSet = nullptr;
    QCheckBox *m_statistics = nullptr;
    QCheckBox *m_statsSettings = nullptr;
    QCheckBox *m_statsOximetry = nullptr;
    QCheckBox *m_statsDevices = nullptr;
    QCheckBox *m_personal = nullptr;
    QCheckBox *m_serial = nullptr;
    QLabel *m_pages = nullptr;
    QPushButton *m_create = nullptr;
};

#endif // PDFREPORTDIALOG_H
