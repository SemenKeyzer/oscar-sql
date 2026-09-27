/* Bluetooth Oximeter Import Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bluetoothoximeterpage.h"

#include "SleepLib/loader_plugins/contec_ble_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace ContecBle;

BluetoothOximeterPage::BluetoothOximeterPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bluetoothImportPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#bluetoothImportPage { background-color: #f0f0f0; }"));

    m_stepTexts[StepScan] = tr("Searching for oximeters...");
    m_stepTexts[StepRead] = tr("Reading the record list...");
    m_stepTexts[StepDownload] = tr("Downloading new records...");
    m_stepTexts[StepSave] = tr("Saving to OSCAR...");

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Bluetooth import")), this));
    for (int i = 0; i < StepCount; ++i) {
        m_stepLabels[i] = new QLabel(this);
        layout->addWidget(m_stepLabels[i]);
    }

    m_deviceList = new QListWidget(this);
    m_connectButton = new QPushButton(tr("Connect"), this);
    layout->addWidget(m_deviceList);
    layout->addWidget(m_connectButton, 0, Qt::AlignLeft);

    m_progress = new QProgressBar(this);
    layout->addWidget(m_progress);

    m_syncClock = new QCheckBox(tr("Set the oximeter clock to this computer's time"), this);
    m_syncClock->setChecked(p_profile->oxi->syncOximeterClock());
    connect(m_syncClock, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setSyncOximeterClock(on); });
    layout->addWidget(m_syncClock);

    m_eraseAfter = new QCheckBox(tr("Automatically erase the records on the oximeter after a successful import"), this);
    m_eraseAfter->setToolTip(tr("The oximeter can only erase all of its records at once. OSCAR erases only when "
                                "every record on it is safely in OSCAR and it isn't still recording."));
    m_eraseAfter->setChecked(p_profile->oxi->bleEraseAfterImport());
    connect(m_eraseAfter, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setBleEraseAfterImport(on); });
    layout->addWidget(m_eraseAfter);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Start"), tr("Length"), tr("Result") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_retryButton = new QPushButton(tr("Retry"), this);
    m_doneButton = new QPushButton(tr("Done"), this);
    buttons->addWidget(m_retryButton);
    buttons->addWidget(m_doneButton);
    layout->addLayout(buttons);

    m_scanner = new ContecBleScanner(this);
    connect(m_scanner, &ContecBleScanner::finished, this, &BluetoothOximeterPage::onScanFinished);
    connect(m_scanner, &ContecBleScanner::failed, this, &BluetoothOximeterPage::onFailed);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        const int row = m_deviceList->currentRow();
        if (row >= 0 && row < m_found.size()) connectTo(m_found.at(row));
    });
    connect(m_retryButton, &QPushButton::clicked, this, &BluetoothOximeterPage::start);
    connect(m_doneButton, &QPushButton::clicked, this, [this]() { emit finished(m_importedAny); });
}

BluetoothOximeterPage::~BluetoothOximeterPage()
{
    m_busy = false;
    stopDevice();
}

void BluetoothOximeterPage::setStep(int step, bool allDone)
{
    for (int i = 0; i < StepCount; ++i) {
        const QString mark = (allDone || i < step) ? QString(QChar(0x2713))           // done
                           : (i == step ? QString(QChar(0x25B6)) : QStringLiteral(" "));
        m_stepLabels[i]->setText(mark + QStringLiteral("  ") + m_stepTexts[i]);
        m_stepLabels[i]->setEnabled(allDone || i <= step);
    }
}

void BluetoothOximeterPage::start()
{
    stopDevice();
    m_importer.reset();
    m_found.clear();
    m_rows.clear();
    m_table->setRowCount(0);
    m_deviceList->clear();
    m_deviceList->hide();
    m_connectButton->hide();
    m_progress->hide();
    m_retryButton->hide();
    m_doneButton->hide();
    m_headersOnDevice = 0;
    m_downloadCompleted = false;
    m_importedAny = false;
    m_lastRecordEnd = QDateTime();
    m_clockText.clear();
    m_eraseText.clear();
    m_stepTexts[StepScan] = tr("Searching for oximeters...");
    m_stepTexts[StepDownload] = tr("Downloading new records...");
    m_busy = true;
    setStep(StepScan);
    m_summary->setText(tr("Turn on Bluetooth in the oximeter's menu and close the Contec phone app first."));
    m_scanner->start(15000);
}

void BluetoothOximeterPage::onScanFinished(const QList<ContecBleFoundDevice> &devices)
{
    if (!m_busy) return;
    m_found = devices;
    if (devices.isEmpty()) {
        onFailed(tr("No oximeter found. Turn on Bluetooth in the oximeter's menu, close the Contec phone app and try again."));
        return;
    }
    if (devices.size() == 1) {
        connectTo(devices.first());
        return;
    }
    for (const ContecBleFoundDevice &d : devices) {
        m_deviceList->addItem(tr("%1 - %2 (signal %3 dBm)").arg(d.name, d.model).arg(d.rssi));
    }
    m_deviceList->setCurrentRow(0);
    m_deviceList->show();
    m_connectButton->show();
    m_summary->setText(tr("Several oximeters are nearby. Choose yours."));
}

void BluetoothOximeterPage::connectTo(const ContecBleFoundDevice &device)
{
    m_deviceList->hide();
    m_connectButton->hide();
    m_deviceName = device.name;
    m_model = device.model;
    m_stepTexts[StepScan] = tr("Connecting to %1 (%2)...").arg(device.name, device.model);
    setStep(StepScan);
    m_link = new QtContecBleLink(this);
    connect(m_link, &QtContecBleLink::ready, this, &BluetoothOximeterPage::onLinkReady);
    connect(m_link, &QtContecBleLink::failed, this, &BluetoothOximeterPage::onFailed);
    m_link->connectTo(device.info);
}

void BluetoothOximeterPage::onLinkReady()
{
    if (!m_busy) return;
    m_stepTexts[StepScan] = tr("Connected to %1 (%2)").arg(m_deviceName, m_model);
    setStep(StepRead);
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(m_model));
    mach->setBrand(QObject::tr("Contec"));
    mach->setModel(m_model);
    m_importer.reset(new ContecBleImporter(mach));
    m_downloadStarted = QDateTime::currentDateTime();

    m_downloader = new ContecBleDownloader(this);
    m_downloader->setWantRecord([this](const RecordHeader &h) { return wantRecord(h); });
    connect(m_downloader, &ContecBleDownloader::recordCountKnown, this, &BluetoothOximeterPage::onRecordCount);
    connect(m_downloader, &ContecBleDownloader::recordDownloaded, this, &BluetoothOximeterPage::onRecordDownloaded);
    connect(m_downloader, &ContecBleDownloader::progress, this, &BluetoothOximeterPage::onProgress);
    connect(m_downloader, &ContecBleDownloader::downloadFinished, this, &BluetoothOximeterPage::onDownloadFinished);
    connect(m_downloader, &ContecBleDownloader::clockSet, this, &BluetoothOximeterPage::onClockSet);
    connect(m_downloader, &ContecBleDownloader::eraseFinished, this, &BluetoothOximeterPage::onEraseFinished);
    connect(m_downloader, &ContecBleDownloader::failed, this, &BluetoothOximeterPage::onFailed);
    connect(m_downloader, &ContecBleDownloader::deviceIdentified, this,
            [this](const QString &model, const QString &firmware, int) {
        m_stepTexts[StepScan] = tr("Connected to %1 (%2, firmware %3)").arg(m_deviceName, model, firmware);
        setStep(StepRead);
    });
    m_downloader->start(m_link, m_deviceName);
}

bool BluetoothOximeterPage::wantRecord(const RecordHeader &h)
{
    Row row;
    row.header = h;
    row.decision = m_importer->decide(h);
    switch (row.decision) {
    case Decision::Import:
    case Decision::ReplaceShorter:
        row.pending = true;
        break;
    case Decision::AlreadyPresent:
        row.outcome = Outcome::AlreadyPresent;
        break;
    case Decision::ConflictOtherOximeter:
        row.outcome = Outcome::ConflictOtherOximeter;
        row.otherDevice = m_importer->otherOximeterName(h);
        break;
    case Decision::InvalidStart:
        row.outcome = Outcome::InvalidStart;
        break;
    }
    const QDateTime start = h.start();
    if (start.isValid()) {
        const QDateTime end = start.addSecs(h.samples);
        if (!m_lastRecordEnd.isValid() || end > m_lastRecordEnd) m_lastRecordEnd = end;
    }
    m_rows.append(row);
    addTableRow(row);
    return row.pending;
}

void BluetoothOximeterPage::onRecordCount(int count)
{
    m_headersOnDevice = count;
    setStep(StepDownload);
    m_progress->setRange(0, qMax(count, 1));
    m_progress->setValue(0);
    m_progress->show();
}

void BluetoothOximeterPage::onRecordDownloaded(const Record &r)
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Row &row = m_rows[i];
        if (!row.pending || row.header.l != r.header.l || row.header.m != r.header.m) continue;
        row.pending = false;
        row.outcome = m_importer->save(r, row.decision);
        if (row.outcome == Outcome::ConflictOtherOximeter) row.otherDevice = m_importer->otherOximeterName(r.header);
        if (row.outcome == Outcome::Imported || row.outcome == Outcome::Updated) m_importedAny = true;
        updateTableRow(i);
        return;
    }
}

void BluetoothOximeterPage::onProgress(int done, int total)
{
    m_progress->setValue(done);
    m_stepTexts[StepDownload] = tr("Downloading new records... (%1 of %2)").arg(done).arg(total);
    setStep(StepDownload);
}

void BluetoothOximeterPage::onDownloadFinished()
{
    if (!m_busy) return;
    setStep(StepSave);
    m_downloadCompleted = true;
    m_importer->finish();
    if (m_syncClock->isChecked()) m_downloader->setClock(QDateTime::currentDateTime());
    else afterClock();
}

void BluetoothOximeterPage::onClockSet(bool ok)
{
    m_clockText = ok ? tr("The oximeter clock was set to this computer's time.")
                     : tr("The oximeter didn't confirm the new clock time.");
    afterClock();
}

void BluetoothOximeterPage::afterClock()
{
    EraseInput in;
    in.eraseEnabled = m_eraseAfter->isChecked();
    in.downloadCompleted = m_downloadCompleted;
    for (const Row &row : m_rows) in.outcomes.append(row.outcome);
    in.headersOnDevice = m_headersOnDevice;
    in.lastRecordEnd = m_lastRecordEnd;
    in.downloadStarted = m_downloadStarted;
    switch (canErase(in)) {
    case EraseVerdict::Erase:
        m_downloader->allowDestructive(true);
        m_downloader->eraseAllRecords();
        return;
    case EraseVerdict::DownloadIncomplete:
        m_eraseText = tr("The oximeter was not erased because the download didn't finish.");
        break;
    case EraseVerdict::NotAllSaved:
        m_eraseText = tr("The oximeter was not erased because some of its records are not in OSCAR.");
        break;
    case EraseVerdict::StillRecording:
        m_eraseText = tr("The oximeter was not erased because it seems to be still recording.");
        break;
    case EraseVerdict::Disabled:
    case EraseVerdict::NothingToErase:
        break;
    }
    finish(QString());
}

void BluetoothOximeterPage::onEraseFinished(bool ok)
{
    m_eraseText = ok ? tr("All records were erased from the oximeter.")
                     : tr("The oximeter didn't confirm the erase, so its records may still be there.");
    finish(QString());
}

void BluetoothOximeterPage::onFailed(const QString &message)
{
    if (!m_busy) return;
    if (m_importer) m_importer->finish();     // keep what was saved
    finish(message);
}

void BluetoothOximeterPage::cancel()
{
    if (m_busy) onFailed(tr("The import was cancelled."));
}

void BluetoothOximeterPage::finish(const QString &error)
{
    m_busy = false;
    stopDevice();
    if (error.isEmpty()) setStep(StepCount, true);   // on an error the reached step stays marked
    m_progress->hide();
    m_summary->setText(summaryText(error));
    m_retryButton->setVisible(!error.isEmpty());
    m_doneButton->show();
}

void BluetoothOximeterPage::stopDevice()
{
    if (m_scanner) m_scanner->stop();
    if (m_downloader) {
        m_downloader->cancel();
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }
    if (m_link) {
        m_link->disconnectFromDevice();
        m_link->deleteLater();
        m_link = nullptr;
    }
}

void BluetoothOximeterPage::addTableRow(const Row &row)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    const QDateTime start = row.header.start();
    const QString when = start.isValid()
        ? QLocale().toString(start.date(), QLocale::ShortFormat) + QStringLiteral(" ") + start.time().toString(QStringLiteral("HH:mm:ss"))
        : tr("invalid");
    const int s = row.header.samples;
    m_table->setItem(r, 0, new QTableWidgetItem(when));
    m_table->setItem(r, 1, new QTableWidgetItem(QString::asprintf("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)));
    m_table->setItem(r, 2, new QTableWidgetItem(outcomeText(row)));
    m_table->scrollToBottom();
}

void BluetoothOximeterPage::updateTableRow(int index)
{
    if (QTableWidgetItem *item = m_table->item(index, 2)) item->setText(outcomeText(m_rows.at(index)));
}

QString BluetoothOximeterPage::outcomeText(const Row &row) const
{
    if (row.pending) return tr("Downloading...");
    switch (row.outcome) {
    case Outcome::Imported: return tr("Imported");
    case Outcome::Updated: return tr("Updated (was shorter)");
    case Outcome::AlreadyPresent: return tr("Already in OSCAR");
    case Outcome::ConflictOtherOximeter: return tr("Skipped: this night already has oximetry from %1").arg(row.otherDevice);
    case Outcome::InvalidStart: return tr("Skipped: invalid start time");
    case Outcome::NotDownloaded: return tr("Not downloaded");
    case Outcome::SaveFailed: return tr("Save failed");
    }
    return QString();
}

QString BluetoothOximeterPage::summaryText(const QString &error) const
{
    int imported = 0, updated = 0, present = 0, skipped = 0;
    for (const Row &row : m_rows) {
        switch (row.outcome) {
        case Outcome::Imported: ++imported; break;
        case Outcome::Updated: ++updated; break;
        case Outcome::AlreadyPresent: ++present; break;
        default: ++skipped; break;
        }
    }
    QStringList lines;
    if (!error.isEmpty()) lines << QStringLiteral("<b>%1</b>").arg(error.toHtmlEscaped());
    if (!m_rows.isEmpty()) {
        lines << tr("%1 imported, %2 updated, %3 already in OSCAR, %4 skipped.")
                     .arg(imported).arg(updated).arg(present).arg(skipped);
    } else if (error.isEmpty()) {
        lines << tr("The oximeter has no stored records.");
    }
    if (!m_clockText.isEmpty()) lines << m_clockText.toHtmlEscaped();
    if (!m_eraseText.isEmpty()) lines << m_eraseText.toHtmlEscaped();
    return lines.join(QStringLiteral("<br>"));
}
