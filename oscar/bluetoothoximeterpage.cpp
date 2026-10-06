/* Bluetooth Oximeter Import Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bluetoothoximeterpage.h"
#include "helptips.h"

#include "SleepLib/loader_plugins/contec_ble_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace ContecBle;

namespace {

QString startText(const RecordHeader &h)
{
    const QDateTime start = h.start();
    if (!start.isValid()) return BluetoothOximeterPage::tr("invalid");
    return QLocale().toString(start.date(), QLocale::ShortFormat) + QStringLiteral(" ")
         + start.time().toString(QStringLiteral("HH:mm:ss"));
}

QString lengthText(int s)
{
    return QString::asprintf("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
}

const char *const kChannelNames[] = {
    QT_TRANSLATE_NOOP("BluetoothOximeterPage", "SpO2"),
    QT_TRANSLATE_NOOP("BluetoothOximeterPage", "Pulse"),
    QT_TRANSLATE_NOOP("BluetoothOximeterPage", "PI"),
};

} // namespace

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
        if (i == StepDownload) {
            m_channelLabel = new QLabel(this);
            m_channelLabel->setContentsMargins(24, 0, 0, 0);
            layout->addWidget(m_channelLabel);
        }
    }

    m_deviceList = new QListWidget(this);
    m_deviceList->setObjectName(QStringLiteral("deviceList"));
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    layout->addWidget(m_deviceList);
    layout->addWidget(m_connectButton, 0, Qt::AlignLeft);

    m_progress = new QProgressBar(this);
    layout->addWidget(m_progress);
    m_detailLabel = new QLabel(this);
    layout->addWidget(m_detailLabel);
    m_signalLabel = new QLabel(this);
    m_signalLabel->setWordWrap(true);
    m_signalLabel->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #fff3cd; color: #664d03; padding: 4px 6px; border-radius: 4px; }"));
    layout->addWidget(m_signalLabel);

    m_syncClock = new QCheckBox(tr("Set the oximeter clock to this computer's time"), this);
    m_syncClock->setObjectName(QStringLiteral("syncClock"));
    m_syncClock->setChecked(p_profile->oxi->syncOximeterClock());
    connect(m_syncClock, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setSyncOximeterClock(on); });
    layout->addWidget(m_syncClock);

    m_eraseAfter = new QCheckBox(tr("Automatically erase the records on the oximeter after a successful import"), this);
    m_eraseAfter->setObjectName(QStringLiteral("eraseAfter"));
    m_eraseAfter->setToolTip(tr("The oximeter can only erase all of its records at once. OSCAR erases only when "
                                "every record on it is safely in OSCAR and it isn't still recording."));
    m_eraseAfter->setChecked(p_profile->oxi->bleEraseAfterImport());
    connect(m_eraseAfter, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setBleEraseAfterImport(on); });
    layout->addWidget(m_eraseAfter);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Start"), tr("Length"), tr("Result") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_scanner = new ContecBleScanner(this);
    connect(m_scanner, &ContecBleScanner::finished, this, &BluetoothOximeterPage::onScanFinished);
    connect(m_scanner, &ContecBleScanner::failed, this, &BluetoothOximeterPage::onFailed);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        const int row = m_deviceList->currentRow();
        if (row >= 0 && row < m_found.size()) connectTo(m_found.at(row));
    });
    HelpTips::attachAll(this, QStringLiteral("ble"));
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
    hideDownloadDetails();
    m_headersOnDevice = 0;
    m_currentRow = -1;
    m_downloadCompleted = false;
    m_importedAny = false;
    m_lastRecordEnd = QDateTime();
    m_clockText.clear();
    m_eraseText.clear();
    m_stepTexts[StepScan] = tr("Searching for oximeters...");
    m_stepTexts[StepRead] = tr("Reading the record list...");
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
    m_summary->setText(tr("Keep the oximeter close to the computer until the import finishes."));
    Machine *mach = p_profile->CreateMachine(ContecBleLoader::infoForModel(m_model));
    mach->setBrand(QObject::tr("Contec"));
    mach->setModel(m_model);
    m_importer.reset(new ContecBleImporter(mach));
    m_downloadStarted = QDateTime::currentDateTime();
    m_clockTrusted = p_profile->oxi->bleClockSynced();   // records are stamped before this run's sync

    m_downloader = new ContecBleDownloader(this);
    m_downloader->setWantRecord([this](const RecordHeader &h) { return wantRecord(h); });
    connect(m_downloader, &ContecBleDownloader::recordCountKnown, this, &BluetoothOximeterPage::onRecordCount);
    connect(m_downloader, &ContecBleDownloader::recordDownloaded, this, &BluetoothOximeterPage::onRecordDownloaded);
    connect(m_downloader, &ContecBleDownloader::channelProgress, this, &BluetoothOximeterPage::onChannelProgress);
    connect(m_downloader, &ContecBleDownloader::retrying, this, &BluetoothOximeterPage::onRetrying);
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
    const int index = m_rows.size();
    m_rows.append(row);
    setTableRow(index, row);
    m_stepTexts[StepRead] = recordListText();
    const int total = qMax(m_headersOnDevice, index + 1);
    if (row.pending) {
        m_currentRow = index;
        m_stepTexts[StepDownload] = tr("Downloading record %1 of %2 (%3, %4)")
                                        .arg(index + 1).arg(total).arg(startText(h), lengthText(h.samples));
        m_progress->setValue(0);
        m_progress->show();
        m_detailLabel->clear();
        m_recordClock.start();
    } else {
        m_stepTexts[StepDownload] = tr("Checking record %1 of %2...").arg(index + 1).arg(total);
    }
    setStep(StepDownload);
    return row.pending;
}

QString BluetoothOximeterPage::recordListText() const
{
    int fresh = 0, present = 0, skipped = 0;
    for (const Row &row : m_rows) {
        if (row.decision == Decision::Import || row.decision == Decision::ReplaceShorter) ++fresh;
        else if (row.decision == Decision::AlreadyPresent) ++present;
        else ++skipped;
    }
    QString text = m_headersOnDevice == 0 ? tr("No records on the oximeter")
                 : m_headersOnDevice == 1 ? tr("1 record on the oximeter")
                 : tr("%1 records on the oximeter").arg(m_headersOnDevice);
    if (!m_rows.isEmpty()) {
        QString tally = tr("%1 new, %2 already in OSCAR").arg(fresh).arg(present);
        if (skipped > 0) tally += QStringLiteral(", ") + tr("%1 skipped").arg(skipped);
        text += QStringLiteral(" (") + tally + QStringLiteral(")");
    }
    return text;
}

void BluetoothOximeterPage::onRecordCount(int count)
{
    m_headersOnDevice = count;
    m_stepTexts[StepRead] = recordListText();
    for (int i = m_table->rowCount(); i < count; ++i) {   // every record gets its line up front
        m_table->insertRow(i);
        m_table->setItem(i, 0, new QTableWidgetItem(QStringLiteral("-")));
        m_table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("-")));
        m_table->setItem(i, 2, new QTableWidgetItem(tr("Waiting")));
    }
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    setStep(count > 0 ? StepDownload : StepSave);
}

void BluetoothOximeterPage::onChannelProgress(int, int index, int count, int done, int total)
{
    if (!m_busy || m_currentRow < 0 || m_currentRow >= m_rows.size() || count <= 0 || total <= 0) return;
    const qint64 recordTotal = qint64(count) * total;
    const qint64 recordDone = qint64(index) * total + done;
    const int permille = int(recordDone * 1000 / recordTotal);
    m_progress->setValue(permille);
    Row &row = m_rows[m_currentRow];
    if (row.pending && row.percent != permille / 10) {
        row.percent = permille / 10;
        updateTableRow(m_currentRow);
    }

    QStringList parts;
    for (int i = 0; i < count && i < 3; ++i) {
        const QString name = tr(kChannelNames[i]);
        if (i < index) parts << name + QStringLiteral(" ") + QChar(0x2713);
        else if (i == index) parts << tr("%1 %2%").arg(name).arg(done * 100 / total);
        else parts << tr("%1 waiting").arg(name);
    }
    m_channelLabel->setText(parts.join(QStringLiteral("   ")));
    m_channelLabel->show();

    QString detail = tr("%1: %2 of %3 samples").arg(tr(kChannelNames[qMin(index, 2)]),
                                                     QLocale().toString(done), QLocale().toString(total));
    const int secs = secondsLeft(recordTotal - recordDone, recordDone, m_recordClock.elapsed());
    if (secs >= 0) {
        const bool more = m_currentRow + 1 < m_headersOnDevice;
        const int mins = (secs + 59) / 60;
        QString left;
        if (secs < 60) left = more ? tr("less than a minute left for this record") : tr("less than a minute left");
        else if (mins < 60) left = (more ? tr("about %1 min left for this record") : tr("about %1 min left")).arg(mins);
        else left = (more ? tr("about %1 h %2 min left for this record") : tr("about %1 h %2 min left"))
                        .arg(mins / 60).arg(mins % 60);
        detail += QStringLiteral("   ") + QChar(0x00B7) + QStringLiteral("   ") + left;
    }
    m_detailLabel->setText(detail);
    m_detailLabel->show();
    if (done > 0 && m_signalClock.isValid() && m_signalClock.elapsed() > 5000) m_signalLabel->hide();
}

void BluetoothOximeterPage::onRetrying(int attempt, int maxAttempts)
{
    if (!m_busy) return;
    m_signalLabel->setText(tr("Weak signal, repeating part of the data (attempt %1 of %2). "
                              "Keep the oximeter close to the computer.").arg(attempt).arg(maxAttempts));
    m_signalLabel->show();
    m_signalClock.start();
}

void BluetoothOximeterPage::hideDownloadDetails()
{
    m_channelLabel->hide();
    m_detailLabel->hide();
    m_signalLabel->hide();
}

void BluetoothOximeterPage::onRecordDownloaded(const Record &r)
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Row &row = m_rows[i];
        if (!row.pending || row.header.l != r.header.l || row.header.m != r.header.m) continue;
        row.pending = false;
        row.percent = -1;
        row.outcome = m_importer->save(r, row.decision);
        if (row.outcome == Outcome::ConflictOtherOximeter) row.otherDevice = m_importer->otherOximeterName(r.header);
        if (row.outcome == Outcome::Imported || row.outcome == Outcome::Updated) m_importedAny = true;
        updateTableRow(i);
        return;
    }
}

void BluetoothOximeterPage::onDownloadFinished()
{
    if (!m_busy) return;
    int fresh = 0;
    for (const Row &row : m_rows) {
        if (row.decision == Decision::Import || row.decision == Decision::ReplaceShorter) ++fresh;
    }
    m_stepTexts[StepDownload] = fresh == 0 ? tr("No new records to download")
                              : fresh == 1 ? tr("Downloaded 1 new record")
                              : tr("Downloaded %1 new records").arg(fresh);
    hideDownloadDetails();
    m_progress->hide();
    setStep(StepSave);
    m_downloadCompleted = true;
    m_importer->finish();
    if (m_syncClock->isChecked()) m_downloader->setClock(QDateTime::currentDateTime());
    else afterClock();
}

void BluetoothOximeterPage::onClockSet(bool ok)
{
    if (ok) p_profile->oxi->setBleClockSynced(true);
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
    in.clockTrusted = m_clockTrusted;
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
    case EraseVerdict::ClockNotSynced:
        m_eraseText = tr("The oximeter was not erased because OSCAR can't yet tell whether it is still "
                         "recording. Keep \"%1\" checked; erasing works from the next import on.")
                          .arg(m_syncClock->text());
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
    for (int i = 0; i < m_rows.size(); ++i) {   // an interrupted download leaves rows waiting
        if (!m_rows[i].pending) continue;
        m_rows[i].pending = false;               // outcome stays NotDownloaded
        updateTableRow(i);
    }
    for (int i = m_rows.size(); i < m_table->rowCount(); ++i) {   // never reached
        if (QTableWidgetItem *item = m_table->item(i, 2)) item->setText(tr("Not downloaded"));
    }
    hideDownloadDetails();
    if (error.isEmpty()) setStep(StepCount, true);   // on an error the reached step stays marked
    m_progress->hide();
    m_summary->setText(summaryText(error));
    emit ended(!error.isEmpty());
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

void BluetoothOximeterPage::setTableRow(int index, const Row &row)
{
    while (m_table->rowCount() <= index) m_table->insertRow(m_table->rowCount());
    m_table->setItem(index, 0, new QTableWidgetItem(startText(row.header)));
    m_table->setItem(index, 1, new QTableWidgetItem(lengthText(row.header.samples)));
    m_table->setItem(index, 2, new QTableWidgetItem(outcomeText(row)));
    m_table->scrollToItem(m_table->item(index, 0));
}

void BluetoothOximeterPage::updateTableRow(int index)
{
    if (QTableWidgetItem *item = m_table->item(index, 2)) item->setText(outcomeText(m_rows.at(index)));
}

QString BluetoothOximeterPage::outcomeText(const Row &row) const
{
    if (row.pending) return row.percent >= 0 ? tr("Downloading %1%").arg(row.percent) : tr("Downloading...");
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

QImage BluetoothOximeterPage::badgedIcon(const QImage &base, int size)
{
    QImage icon(size, size, QImage::Format_ARGB32_Premultiplied);
    icon.fill(Qt::transparent);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage scaled = base.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);

    // Badge: a Bluetooth-blue disc with a white rim, holding the white Bluetooth rune.
    const qreal d = size * 0.47;
    const QRectF disc(size - d, size - d, d, d);
    p.setPen(QPen(Qt::white, size * 0.025));
    p.setBrush(QColor(0x00, 0x82, 0xFC));
    p.drawEllipse(disc.adjusted(1, 1, -1, -1));

    const QPointF c = disc.center();
    const qreal u = d * 0.24;
    const QPointF rune[] = { c + QPointF(-0.7 * u, -0.65 * u), c + QPointF(0.7 * u, 0.65 * u),
                             c + QPointF(0, 1.3 * u), c + QPointF(0, -1.3 * u),
                             c + QPointF(0.7 * u, -0.65 * u), c + QPointF(-0.7 * u, 0.65 * u) };
    p.setPen(QPen(Qt::white, size * 0.045, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(rune, 6);
    p.end();
    return icon.convertToFormat(QImage::Format_ARGB32);
}

