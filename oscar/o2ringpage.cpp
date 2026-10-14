/* O2Ring Import Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "o2ringpage.h"
#include "helptips.h"

#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

//! "yyyyMMddHHmmss" as the record's local start date and time.
QString startText(const QString &name)
{
    const QDateTime start = QDateTime::fromString(name, QStringLiteral("yyyyMMddHHmmss"));
    if (!start.isValid()) return name;
    return QLocale().toString(start.date(), QLocale::ShortFormat) + QStringLiteral(" ")
         + start.time().toString(QStringLiteral("HH:mm:ss"));
}

} // namespace

O2RingPage::O2RingPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("o2ringImportPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#o2ringImportPage { background-color: #f0f0f0; }"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("O2Ring (Bluetooth)")), this));
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_deviceList = new QListWidget(this);
    m_deviceList->setObjectName(QStringLiteral("deviceList"));
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    layout->addWidget(m_deviceList);
    layout->addWidget(m_connectButton, 0, Qt::AlignLeft);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    m_setClock = new QCheckBox(tr("Set the ring's clock to this computer's time"), this);
    m_setClock->setObjectName(QStringLiteral("setClock"));
    m_setClock->setChecked(p_profile->oxi->o2ringSetClock());
    connect(m_setClock, &QCheckBox::toggled, this, [](bool on) { p_profile->oxi->setO2RingSetClock(on); });
    layout->addWidget(m_setClock);

    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("o2ringRecords"));
    m_table->setHorizontalHeaderLabels({ tr("Record start"), tr("Status") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    m_downloadButton = new QPushButton(tr("Download"), this);
    m_downloadButton->setObjectName(QStringLiteral("downloadButton"));
    layout->addWidget(m_downloadButton, 0, Qt::AlignLeft);

    m_progress = new QProgressBar(this);
    layout->addWidget(m_progress);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("o2ringSummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_scanner = new O2RingScanner(this);
    connect(m_scanner, &O2RingScanner::finished, this, &O2RingPage::onScanFinished);
    connect(m_scanner, &O2RingScanner::failed, this, &O2RingPage::finish);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        const int row = m_deviceList->currentRow();
        if (row >= 0 && row < m_found.size()) connectTo(m_found.at(row));
    });
    connect(m_downloadButton, &QPushButton::clicked, this, &O2RingPage::onDownloadClicked);
    reset();
    HelpTips::attachAll(this, QStringLiteral("o2ring"));
}

O2RingPage::~O2RingPage()
{
    m_busy = false;
    stopDevice();
}

void O2RingPage::reset()
{
    stopDevice();
    m_importer.reset();
    m_found.clear();
    m_names.clear();
    m_table->setRowCount(0);
    m_deviceList->clear();
    m_deviceList->hide();
    m_connectButton->hide();
    m_downloadButton->hide();
    m_progress->hide();
    m_info->clear();
    m_summary->clear();
    m_imported = m_skipped = m_failed = 0;
    m_error.clear();
    m_clockError.clear();
}

void O2RingPage::start()
{
    reset();
    m_busy = true;
    m_status->setText(tr("Searching for the ring..."));
    m_summary->setText(tr("Close the ViHealth app on the phone: the ring keeps one connection at a time. "
                          "Wear the ring so that it wakes up."));
    m_scanner->start(15000);
}

void O2RingPage::startWithLink(O2RingLink *link, const QString &name)
{
    reset();
    m_busy = true;
    m_deviceName = name;
    begin(link);
}

void O2RingPage::onScanFinished(const QList<O2RingFoundDevice> &devices)
{
    if (!m_busy) return;
    m_found = devices;
    if (devices.isEmpty()) {
        finish(tr("No ring found. Close the ViHealth app on the phone, wear the ring so that it wakes up and try again."));
        return;
    }
    if (devices.size() == 1) {
        connectTo(devices.first());
        return;
    }
    for (const O2RingFoundDevice &d : devices) {
        m_deviceList->addItem(tr("%1 (signal %2 dBm)").arg(d.name).arg(d.rssi));
    }
    m_deviceList->setCurrentRow(0);
    m_deviceList->show();
    m_connectButton->show();
    m_status->setText(tr("Several rings are nearby. Choose yours."));
}

void O2RingPage::connectTo(const O2RingFoundDevice &device)
{
    m_deviceList->hide();
    m_connectButton->hide();
    m_deviceName = device.name;
    m_status->setText(tr("Connecting to %1...").arg(device.name));
    m_ownLink = new QtO2RingLink(this);
    connect(m_ownLink, &QtO2RingLink::ready, this, [this]() {
        if (m_busy && m_ownLink) begin(m_ownLink);
    });
    connect(m_ownLink, &QtO2RingLink::failed, this, &O2RingPage::finish);
    m_ownLink->connectTo(device.info, 15000);
}

void O2RingPage::begin(O2RingLink *link)
{
    m_status->setText(tr("Reading the records on %1...").arg(m_deviceName));
    m_summary->setText(tr("Keep the ring close to the computer until the import finishes."));
    m_downloader = new O2RingDownloader(this);
    m_downloader->setResponseTimeout(m_timeoutMs);
    connect(m_downloader, &O2RingDownloader::infoReceived, this, &O2RingPage::onInfo);
    connect(m_downloader, &O2RingDownloader::clockSet, this, [this](bool ok) {
        if (!ok) m_clockError = tr("The ring's clock could not be set.");
        if (m_busy) downloadChosen();
    });
    connect(m_downloader, &O2RingDownloader::fileProgress, this, &O2RingPage::onFileProgress);
    // Queued: the Viatom loader lets events through (and may show a message box) while it imports,
    // so the downloader must not be on the stack when a click there stops it.
    connect(m_downloader, &O2RingDownloader::fileDownloaded, this, &O2RingPage::onFileDownloaded, Qt::QueuedConnection);
    connect(m_downloader, &O2RingDownloader::fileFailed, this, &O2RingPage::onFileFailed);
    connect(m_downloader, &O2RingDownloader::finished, this, [this]() {
        finish(m_failed > 0 ? tr("Some records could not be downloaded.") : QString());
    });
    connect(m_downloader, &O2RingDownloader::failed, this, &O2RingPage::finish);
    m_downloader->start(link);
}

void O2RingPage::onInfo(const O2Ring::Info &info)
{
    if (!m_busy) return;
    if (!O2RingImporter::validSerial(info.serial)) {
        finish(tr("The ring reports an unexpected serial number (%1): this ring is not supported yet.").arg(info.serial.toHtmlEscaped()));
        return;
    }
    m_importer = std::make_shared<O2RingImporter>(info.serial);
    QString ringTime = info.time;
    ringTime.replace(QLatin1Char(','), QLatin1Char(' '));
    m_status->setText(tr("Connected to %1").arg(m_deviceName));
    m_info->setText(tr("Serial number %1, battery %2, firmware %3.").arg(info.serial, info.battery, info.firmware)
                    + QStringLiteral("\n")
                    + tr("Ring clock: %1; this computer: %2.")
                          .arg(ringTime, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    m_names = info.files;
    m_table->setRowCount(m_names.size());
    for (int i = 0; i < m_names.size(); ++i) {
        const bool already = m_importer->alreadyImported(m_names[i]);
        auto *item = new QTableWidgetItem(startText(m_names[i]));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(already ? Qt::Unchecked : Qt::Checked);
        m_table->setItem(i, 0, item);
        m_table->setItem(i, 1, new QTableWidgetItem(already ? tr("already in OSCAR") : tr("new")));
    }
    m_summary->setText(m_names.isEmpty() ? tr("The ring has no records.")
                                         : tr("Tick the records to download, then press Download."));
    m_downloadButton->setEnabled(true);
    m_downloadButton->show();
}

void O2RingPage::onDownloadClicked()
{
    if (!m_busy || !m_downloader) return;
    m_downloadButton->setEnabled(false);
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (QTableWidgetItem *item = m_table->item(i, 0)) item->setFlags(Qt::ItemIsEnabled);
    }
    if (m_setClock->isChecked()) {
        m_status->setText(tr("Setting the ring's clock..."));
        m_downloader->setClock(QDateTime::currentDateTime());
    } else {
        downloadChosen();
    }
}

void O2RingPage::downloadChosen()
{
    QStringList chosen;
    for (int i = 0; i < m_names.size(); ++i) {
        const QTableWidgetItem *item = m_table->item(i, 0);
        if (item && item->checkState() == Qt::Checked) chosen << m_names[i];
        else ++m_skipped;
    }
    if (chosen.isEmpty()) {
        finish(QString());
        return;
    }
    m_status->setText(tr("Downloading from %1...").arg(m_deviceName));
    m_progress->setValue(0);
    m_progress->show();
    m_downloader->download(chosen);
}

int O2RingPage::rowOf(const QString &name) const
{
    return m_names.indexOf(name);
}

void O2RingPage::setStatus(const QString &name, const QString &text)
{
    const int row = rowOf(name);
    if (row < 0) return;
    m_table->setItem(row, 1, new QTableWidgetItem(text));
}

void O2RingPage::onFileProgress(const QString &name, qint64 done, qint64 total)
{
    if (!m_busy || total <= 0) return;
    const int percent = int(done * 100 / total);
    m_progress->setValue(percent);
    setStatus(name, tr("downloading, %1%").arg(percent));
}

void O2RingPage::onFileDownloaded(const QString &name, const QByteArray &data)
{
    // a whole record is imported even when the import was stopped meanwhile
    const std::shared_ptr<O2RingImporter> importer = m_importer;
    if (!importer) return;
    QString error;
    switch (importer->importFile(name, data, &error)) {
    case O2RingImporter::Result::Imported:
        ++m_imported;
        setStatus(name, tr("imported"));
        break;
    case O2RingImporter::Result::AlreadyThere:
        ++m_skipped;
        setStatus(name, tr("already in OSCAR"));
        break;
    case O2RingImporter::Result::Failed:
        ++m_failed;
        setStatus(name, tr("failed: %1").arg(error));
        break;
    }
    if (!m_busy) showSummary();
}

void O2RingPage::onFileFailed(const QString &name, const QString &error)
{
    if (!m_busy) return;
    ++m_failed;
    setStatus(name, tr("not downloaded: %1").arg(error));
}

void O2RingPage::finish(const QString &error)
{
    if (!m_busy) return;
    m_busy = false;
    stopDevice();
    m_progress->hide();
    m_downloadButton->setEnabled(false);
    m_error = error;
    m_status->setText(error.isEmpty() ? tr("Done.") : tr("Stopped."));
    showSummary();
    emit ended(!error.isEmpty());
}

void O2RingPage::showSummary()
{
    QString counts = tr("%1 imported, %2 skipped").arg(m_imported).arg(m_skipped);
    if (m_failed > 0) counts += tr(", %1 failed").arg(m_failed);
    if (!m_clockError.isEmpty()) counts += QStringLiteral("\n") + m_clockError;
    m_summary->setText(m_error.isEmpty() ? counts : m_error + QStringLiteral("\n") + counts);
}

void O2RingPage::cancel()
{
    if (m_busy) finish(tr("The import was cancelled."));
}

void O2RingPage::stopDevice()
{
    if (m_scanner) m_scanner->stop();
    if (m_downloader) {
        m_downloader->cancel();
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }
    if (m_ownLink) {
        m_ownLink->disconnectFromDevice();
        m_ownLink->deleteLater();
        m_ownLink = nullptr;
    }
}
