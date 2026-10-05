/* PDF Report Dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "pdfreportdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "Graphs/gGraphView.h"
#include "SleepLib/profiles.h"
#include "SleepLib/progressdialog.h"
#include "daily.h"
#include "overview.h"
#include "pdfreportwriter.h"

namespace {

// A row of controls indented under the checkbox of their section.
QWidget *indented(QLayout *layout)
{
    auto *w = new QWidget;
    layout->setContentsMargins(24, 0, 0, 0);
    w->setLayout(layout);
    return w;
}

} // namespace

PdfReportDialog::PdfReportDialog(Daily *daily, Overview *overview, QWidget *parent)
    : QDialog(parent), m_daily(daily), m_overview(overview)
{
    setWindowTitle(tr("Create PDF Report"));
    m_lastNight = p_profile->LastDay(MT_CPAP);
    if (!m_lastNight.isValid()) m_lastNight = p_profile->LastDay();
    auto *layout = new QVBoxLayout(this);

    // the period, for every section
    auto *periodBox = new QGroupBox(tr("Period"));
    auto *periodLayout = new QGridLayout(periodBox);
    m_period = new QButtonGroup(this);
    const QStringList periods { tr("Last 7 nights"), tr("Last 30 nights"), tr("Last 90 nights"), tr("From") };
    for (int i = 0; i < periods.size(); ++i) {
        auto *b = new QRadioButton(periods[i]);
        m_period->addButton(b, i);
        periodLayout->addWidget(b, i < 3 ? 0 : 1, i < 3 ? i : 0);
    }
    m_from = new QDateEdit;
    m_to = new QDateEdit;
    for (QDateEdit *e : { m_from, m_to }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
        if (p_profile->FirstDay().isValid() && m_lastNight.isValid()) e->setDateRange(p_profile->FirstDay(), m_lastNight);
    }
    periodLayout->addWidget(m_from, 1, 1);
    periodLayout->addWidget(new QLabel(tr("to")), 1, 2, Qt::AlignCenter);
    periodLayout->addWidget(m_to, 1, 3);
    layout->addWidget(periodBox);

    // ready-made sets
    auto *presets = new QHBoxLayout;
    presets->addWidget(new QLabel(tr("Ready-made:")));
    const QList<QPair<QString, PdfReportOptions::Preset>> presetButtons {
        { tr("Brief for the doctor"), PdfReportOptions::Brief },
        { tr("Detailed"), PdfReportOptions::Detailed },
        { tr("Everything"), PdfReportOptions::Everything },
    };
    for (const auto &p : presetButtons) {
        auto *b = new QPushButton(p.first);
        b->setAutoDefault(false);   // Return creates the report, not a preset
        const PdfReportOptions::Preset preset = p.second;
        connect(b, &QPushButton::clicked, this, [this, preset]() {
            PdfReportOptions o = options();
            o.apply(preset);
            setOptions(o);
        });
        presets->addWidget(b);
    }
    presets->addStretch();
    layout->addLayout(presets);

    // the sections
    auto *sections = new QGroupBox(tr("Include"));
    auto *sectionsLayout = new QVBoxLayout(sections);
    m_summary = new QCheckBox(tr("Summary page: settings, targets, nights, settings compared"));
    sectionsLayout->addWidget(m_summary);

    m_daily_ = new QCheckBox(tr("Daily: each night as the Daily tab prints it"));
    sectionsLayout->addWidget(m_daily_);
    auto *nights = new QHBoxLayout;
    m_nights = new QButtonGroup(this);
    const QStringList nightLabels { tr("Last night"), tr("Last 3"), tr("Last 7"), tr("All nights of the period") };
    for (int i = 0; i < nightLabels.size(); ++i) {
        auto *b = new QRadioButton(nightLabels[i]);
        m_nights->addButton(b, i);
        nights->addWidget(b);
        if (i == PdfReportOptions::AllNights) m_allNights = b;
    }
    nights->addStretch();
    sectionsLayout->addWidget(indented(nights));

    m_overview_ = new QCheckBox(tr("Overview: graphs of the period"));
    sectionsLayout->addWidget(m_overview_);
    auto *overviewRow = new QHBoxLayout;
    overviewRow->addWidget(new QLabel(tr("Graphs:")));
    m_overviewSet = new QComboBox;
    for (OverviewPresets::Preset p : OverviewPresets::presets()) m_overviewSet->addItem(OverviewPresets::title(p), int(p));
    overviewRow->addWidget(m_overviewSet);
    overviewRow->addStretch();
    sectionsLayout->addWidget(indented(overviewRow));

    m_statistics = new QCheckBox(tr("Statistics of the period"));
    sectionsLayout->addWidget(m_statistics);
    auto *stats = new QVBoxLayout;
    m_statsSettings = new QCheckBox(tr("Changes to device settings"));
    m_statsOximetry = new QCheckBox(tr("Oximetry (if there is data)"));
    m_statsDevices = new QCheckBox(tr("Device information"));
    stats->addWidget(m_statsSettings);
    stats->addWidget(m_statsOximetry);
    stats->addWidget(m_statsDevices);
    sectionsLayout->addWidget(indented(stats));
    layout->addWidget(sections);

    auto *privacy = new QGroupBox(tr("Personal data"));
    auto *privacyLayout = new QHBoxLayout(privacy);
    m_personal = new QCheckBox(tr("Name and date of birth"));
    m_serial = new QCheckBox(tr("Serial numbers"));
    privacyLayout->addWidget(m_personal);
    privacyLayout->addWidget(m_serial);
    privacyLayout->addStretch();
    layout->addWidget(privacy);

    auto *bottom = new QHBoxLayout;
    m_pages = new QLabel;
    bottom->addWidget(m_pages, 1);
    auto *buttons = new QDialogButtonBox;
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    m_create = buttons->addButton(tr("Create Report..."), QDialogButtonBox::ActionRole);
    cancel->setAutoDefault(false);   // focus on Cancel must not make Return cancel
    m_create->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &PdfReportDialog::create);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    // every change re-estimates the pages
    for (QCheckBox *c : findChildren<QCheckBox *>()) connect(c, &QCheckBox::toggled, this, &PdfReportDialog::update);
    connect(m_period, &QButtonGroup::idToggled, this, [this](int, bool on) { if (on) update(); });
    connect(m_nights, &QButtonGroup::idToggled, this, [this](int, bool on) { if (on) update(); });
    connect(m_from, &QDateEdit::dateChanged, this, &PdfReportDialog::update);
    connect(m_to, &QDateEdit::dateChanged, this, &PdfReportDialog::update);
    connect(m_overviewSet, qOverload<int>(&QComboBox::currentIndexChanged), this, &PdfReportDialog::update);

    PdfReportOptions o = PdfReportOptions::fromMap(p_profile->general->pdfReportOptions());
    if (!o.from.isValid() || !o.to.isValid()) {
        const QPair<QDate, QDate> last30 = PdfReportOptions().range(m_lastNight);
        o.from = last30.first;
        o.to = last30.second;
    }
    setOptions(o);
}

PdfReportOptions PdfReportDialog::options() const
{
    PdfReportOptions o;
    o.period = PdfReportOptions::PeriodKind(std::max(0, m_period->checkedId()));
    o.from = m_from->date();
    o.to = m_to->date();
    o.summary = m_summary->isChecked();
    o.daily = m_daily_->isChecked();
    o.nights = PdfReportOptions::NightsKind(std::max(0, m_nights->checkedId()));
    o.overview = m_overview_->isChecked();
    o.overviewPreset = OverviewPresets::Preset(m_overviewSet->currentData().toInt());
    o.statistics = m_statistics->isChecked();
    o.statsSettings = m_statsSettings->isChecked();
    o.statsOximetry = m_statsOximetry->isChecked();
    o.statsDevices = m_statsDevices->isChecked();
    o.personalData = m_personal->isChecked();
    o.serialNumbers = m_serial->isChecked();
    return o;
}

void PdfReportDialog::setOptions(const PdfReportOptions &o)
{
    m_loading = true;
    if (QAbstractButton *b = m_period->button(o.period)) b->setChecked(true);
    m_from->setDate(o.from);
    m_to->setDate(o.to);
    m_summary->setChecked(o.summary);
    m_daily_->setChecked(o.daily);
    if (QAbstractButton *b = m_nights->button(o.nights)) b->setChecked(true);
    m_overview_->setChecked(o.overview);
    m_overviewSet->setCurrentIndex(std::max(0, m_overviewSet->findData(int(o.overviewPreset))));
    m_statistics->setChecked(o.statistics);
    m_statsSettings->setChecked(o.statsSettings);
    m_statsOximetry->setChecked(o.statsOximetry);
    m_statsDevices->setChecked(o.statsDevices);
    m_personal->setChecked(o.personalData);
    m_serial->setChecked(o.serialNumbers);
    m_loading = false;
    update();
}

QList<QDate> PdfReportDialog::cpapNights(const QPair<QDate, QDate> &range) const
{
    QList<QDate> nights;
    for (QDate d = range.first; d.isValid() && d <= range.second; d = d.addDays(1)) {
        Day *day = p_profile->GetDay(d, MT_CPAP);
        if (day && day->hours(MT_CPAP) > 0) nights << d;
    }
    return nights;
}

void PdfReportDialog::update()
{
    if (m_loading) return;
    const PdfReportOptions o = options();
    const bool custom = o.period == PdfReportOptions::Custom;
    m_from->setEnabled(custom);
    m_to->setEnabled(custom);
    for (QAbstractButton *b : m_nights->buttons()) b->setEnabled(o.daily);
    m_overviewSet->setEnabled(o.overview);
    for (QCheckBox *c : { m_statsSettings, m_statsOximetry, m_statsDevices }) c->setEnabled(o.statistics);

    const QPair<QDate, QDate> range = o.range(m_lastNight);
    const QList<QDate> all = cpapNights(range);
    m_allNights->setText(tr("All nights of the period (%1)").arg(all.size()));

    int dailyGraphs = m_daily ? m_daily->graphView()->visibleGraphs() : 0;
    int overviewGraphs = 0;
    if (m_overview) {
        if (o.overviewPreset == OverviewPresets::All) {
            overviewGraphs = m_overview->graphView()->visibleGraphs();
        } else {
            const QStringList names = OverviewPresets::graphNames(o.overviewPreset);
            gGraphView *gv = m_overview->graphView();
            for (int i = 0; i < gv->size(); ++i) {
                if (names.contains((*gv)[i]->name()) && !(*gv)[i]->isEmpty()) ++overviewGraphs;
            }
        }
    }
    const int pages = estimatePages(o, int(o.nightsToPrint(all).size()), dailyGraphs, overviewGraphs,
                                    o.statsSettings ? 3 : 2);
    m_pages->setText(tr("about %n page(s)", "", pages));
    m_create->setEnabled(o.anySection() && !(custom && o.from > o.to));
}

void PdfReportDialog::reject()
{
    if (!m_writing) QDialog::reject();
}

void PdfReportDialog::create()
{
    if (m_writing) return;
    const PdfReportOptions o = options();
    const QPair<QDate, QDate> range = o.range(m_lastNight);
    // no name in the file name when the report leaves it out
    const QString from = range.first.toString(QStringLiteral("dd.MM"));
    const QString to = range.second.toString(QStringLiteral("dd.MM.yyyy"));
    const QString name = o.personalData ? tr("CPAP report %1 %2–%3.pdf").arg(p_profile->user->userName(), from, to)
                                        : tr("CPAP report %1–%2.pdf").arg(from, to);
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save PDF Report"), folder + QLatin1Char('/') + name,
                                                      tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;

    PdfReportWriter writer(m_daily, m_overview);
    ProgressDialog progress(this);
    progress.setMessage(tr("Creating the report..."));
    progress.addAbortButton();
    connect(&progress, &ProgressDialog::abortClicked, this, [&writer]() { writer.cancel(); });
    connect(&progress, &QDialog::rejected, this, [&writer]() { writer.cancel(); });   // Esc
    progress.open();
    // the writer processes events: no second report, no closing, no changes meanwhile
    m_writing = true;
    const QList<QWidget *> controls = findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *w : controls) if (!w->isWindow()) w->setEnabled(false);
    QString error;
    const bool written = writer.write(o, m_lastNight, path, &progress, &error);
    for (QWidget *w : controls) if (!w->isWindow()) w->setEnabled(true);
    m_writing = false;
    progress.allowClose();
    progress.close();
    if (!written) {
        if (!writer.cancelled()) QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    p_profile->general->setPdfReportOptions(o.toMap());

    QMessageBox done(QMessageBox::Information, windowTitle(), tr("The report is saved."), QMessageBox::Close, this);
    QPushButton *open = done.addButton(tr("Open"), QMessageBox::AcceptRole);
    done.exec();
    if (done.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    accept();
}
