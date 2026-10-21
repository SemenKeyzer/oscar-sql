/* Contec RS01 Import Page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rs01page.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStorageInfo>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "SleepLib/loader_plugins/rs01_import.h"
#include "SleepLib/loader_plugins/rs01_reader.h"

namespace {

const QString kVolumeName = QStringLiteral("SLEEP METER");
constexpr int kPathRole = Qt::UserRole + 1;

// The recording's length from its header and the segment sizes, without reading the samples.
int recordingSeconds(const QString &folder, QDateTime *start)
{
    QFile hd(QDir(folder).filePath(QFileInfo(folder).fileName() + QStringLiteral(".HD")));
    if (!hd.open(QIODevice::ReadOnly)) return -1;
    const RS01Header h = rs01ParseHeader(hd.readAll());
    if (!h.valid) return -1;
    if (start) *start = h.start;
    qint64 records = 0;
    for (const QFileInfo &seg : QDir(folder).entryInfoList({ QStringLiteral("*.SEG"), QStringLiteral("*.seg") }, QDir::Files))
        records += qMax<qint64>(0, (seg.size() - h.segHeaderBytes) / h.recordBytes());
    return int(records * h.recordSeconds);
}

} // namespace

RS01Page::RS01Page(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_warning = new QLabel(this);
    m_warning->setObjectName(QStringLiteral("rs01Warning"));
    m_warning->setWordWrap(true);
    m_warning->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_warning->setStyleSheet(QStringLiteral("QLabel { background: #FFF3CD; border: 1px solid #E0B252; padding: 6px; }"));
    m_warning->hide();
    layout->addWidget(m_warning);
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("rs01Recordings"));
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({ tr("Recording"), tr("Status") });
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->setRootIsDecorated(false);
    layout->addWidget(m_list, 1);
    auto *buttons = new QHBoxLayout;
    m_folderButton = new QPushButton(tr("Choose folder…"), this);
    m_folderButton->setObjectName(QStringLiteral("rs01Folder"));
    m_folderButton->setToolTip(tr("A folder with the device's recordings (MMDDhhmm folders), or the device itself"));
    m_importButton = new QPushButton(tr("Import"), this);
    m_importButton->setObjectName(QStringLiteral("rs01Import"));
    m_doneButton = new QPushButton(tr("Done"), this);
    m_doneButton->setObjectName(QStringLiteral("rs01Done"));
    buttons->addWidget(m_folderButton);
    buttons->addStretch(1);
    buttons->addWidget(m_importButton);
    buttons->addWidget(m_doneButton);
    layout->addLayout(buttons);
    connect(m_folderButton, &QPushButton::clicked, this, &RS01Page::chooseFolder);
    connect(m_importButton, &QPushButton::clicked, this, &RS01Page::importChosen);
    connect(m_doneButton, &QPushButton::clicked, this, [this]() { emit finished(m_imported > 0); });
}

QString RS01Page::writableWarning(const QString &mountPoint, const QString &device)
{
    return tr("The device is mounted with write access. macOS may then write its own files to it, and the "
              "device damages its recordings after that. Unplugging is not enough: unmount it and mount it "
              "read-only in Terminal, then press Choose folder…:")
         + QStringLiteral("\n  diskutil unmount \"%1\"\n  diskutil mount readOnly %2").arg(mountPoint, device)
         + QStringLiteral("\n") + tr("OSCAR itself never writes to the device.");
}

void RS01Page::start()
{
    m_imported = 0;
    m_list->clear();
    m_warning->hide();
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.name() != kVolumeName) continue;
#ifdef Q_OS_MACOS
        if (!v.isReadOnly()) {
            m_warning->setText(writableWarning(v.rootPath(), QString::fromLocal8Bit(v.device())));
            m_warning->show();
        }
#endif
        listRecordings(v.rootPath());
        return;
    }
    m_status->setText(tr("Plug the RS01 in (switched on before the cable), or choose a folder with a copy of its recordings."));
    m_importButton->setEnabled(false);
}

void RS01Page::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("RS01 recordings"), QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;
    // the folder of one recording, or the folder that holds them
    const QString root = QFileInfo::exists(QDir(dir).filePath(QFileInfo(dir).fileName() + QStringLiteral(".HD")))
                       ? QFileInfo(dir).absolutePath() : dir;
    listRecordings(root);
}

void RS01Page::listRecordings(const QString &root)
{
    m_list->clear();
    RS01Importer importer;
    const QLocale locale;
    for (const QString &folder : rs01FindRecordings(root)) {
        QDateTime start;
        const int seconds = recordingSeconds(folder, &start);
        auto *item = new QTreeWidgetItem(m_list);
        item->setData(0, kPathRole, folder);
        if (seconds < 0) {
            item->setText(0, QFileInfo(folder).fileName());
            item->setText(1, tr("the header can't be read"));
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            continue;
        }
        item->setText(0, tr("%1, %2").arg(locale.toString(start, QLocale::ShortFormat),
                                          QTime(0, 0).addSecs(seconds).toString(QStringLiteral("H:mm:ss"))));
        const bool there = importer.alreadyImported(folder);
        item->setCheckState(0, there ? Qt::Unchecked : Qt::Checked);
        if (there) item->setText(1, tr("already in OSCAR"));
    }
    const int n = m_list->topLevelItemCount();
    m_status->setText(n ? tr("%n recording(s) in %1.", nullptr, n).arg(QDir::toNativeSeparators(root))
                        : tr("No RS01 recordings in %1.").arg(QDir::toNativeSeparators(root)));
    m_importButton->setEnabled(n > 0);
}

void RS01Page::importChosen()
{
    RS01Importer importer;
    m_importButton->setEnabled(false);
    m_folderButton->setEnabled(false);
    int failed = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) continue;
        item->setText(1, tr("importing…"));
        QApplication::processEvents();
        QString error;
        switch (importer.import(item->data(0, kPathRole).toString(), &error)) {
        case RS01Importer::Result::Imported:
            ++m_imported;
            item->setText(1, tr("imported"));
            break;
        case RS01Importer::Result::AlreadyThere:
            item->setText(1, tr("already in OSCAR"));
            break;
        case RS01Importer::Result::Failed:
            ++failed;
            item->setText(1, error);
            break;
        }
        item->setCheckState(0, Qt::Unchecked);
    }
    m_folderButton->setEnabled(true);
    m_status->setText(failed ? tr("Imported %1, failed %2: see the list.").arg(m_imported).arg(failed)
                             : tr("Imported %1. Press Done to see them.").arg(m_imported));
}
