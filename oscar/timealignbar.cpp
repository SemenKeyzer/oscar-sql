/* Time Alignment Bar Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignbar.h"
#include "timealignsession.h"
#include "SleepLib/machine.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

TimeAlignBar::TimeAlignBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TimeAlignBar"));
    setStyleSheet(QStringLiteral(
        "#TimeAlignBar { background: #E6F1FB; border: 1px solid #85B7EB; border-radius: 4px; }"));

    auto *top = new QHBoxLayout;
    top->setSpacing(4);
    top->addWidget(new QLabel(tr("Align device time:"), this));

    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setToolTip(tr("Device whose time is being aligned to the CPAP data"));
    top->addWidget(m_deviceCombo);

    m_offsetLabel = new QLabel(TimeAlignSession::formatOffset(0), this);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(mono.pointSize() + 3);
    mono.setBold(true);
    m_offsetLabel->setFont(mono);
    m_offsetLabel->setAlignment(Qt::AlignCenter);
    m_offsetLabel->setMinimumWidth(QFontMetrics(mono).horizontalAdvance(QStringLiteral("+00:00:00")) + 12);
    m_offsetLabel->setToolTip(tr("Time shift applied to this device for this night"));
    top->addWidget(m_offsetLabel);

    struct Step { qint64 ms; const char *text; };
    static const Step steps[] = {
        { -3600000, QT_TR_NOOP("-1h")  }, { -600000, QT_TR_NOOP("-10m") },
        {   -60000, QT_TR_NOOP("-1m")  }, {  -10000, QT_TR_NOOP("-10s") },
        {    10000, QT_TR_NOOP("+10s") }, {   60000, QT_TR_NOOP("+1m")  },
        {   600000, QT_TR_NOOP("+10m") }, { 3600000, QT_TR_NOOP("+1h")  },
    };
    for (const Step &step : steps) {
        auto *button = new QPushButton(tr(step.text), this);
        button->setAutoDefault(false);
        const qint64 delta = step.ms;
        connect(button, &QPushButton::clicked, this, [this, delta]() { emit nudgeRequested(delta); });
        top->addWidget(button);
        if (step.ms == -10000) top->addSpacing(8);   // gap between the minus and plus groups
    }
    top->addStretch(1);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setToolTip(tr("Discard the change (Esc)"));
    connect(cancel, &QPushButton::clicked, this, &TimeAlignBar::cancelRequested);
    top->addWidget(cancel);

    auto *save = new QPushButton(tr("Save"), this);
    save->setToolTip(tr("Save the shift for this night (Enter)"));
    connect(save, &QPushButton::clicked, this, &TimeAlignBar::saveRequested);
    top->addWidget(save);

    auto *bottom = new QHBoxLayout;
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    bottom->addWidget(m_statusLabel, 1);

    m_sameAsLastNight = new QPushButton(tr("Same as last night"), this);
    m_sameAsLastNight->setToolTip(tr("Use the shift saved for the nearest earlier night"));
    connect(m_sameAsLastNight, &QPushButton::clicked, this, &TimeAlignBar::sameAsLastNightRequested);
    bottom->addWidget(m_sameAsLastNight);

    auto *more = new QPushButton(tr("More options..."), this);
    more->setToolTip(tr("Open Time Corrections for date ranges and other correction types"));
    connect(more, &QPushButton::clicked, this, &TimeAlignBar::moreOptionsRequested);
    bottom->addWidget(more);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);
    layout->addLayout(top);
    layout->addLayout(bottom);

    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        emit deviceChosen(reinterpret_cast<Machine *>(m_deviceCombo->itemData(index).value<quintptr>()));
    });
}

void TimeAlignBar::setDevices(const QList<Machine *> &devices, Machine *current)
{
    QSignalBlocker block(m_deviceCombo);
    m_deviceCombo->clear();
    for (Machine *mach : devices) {
        m_deviceCombo->addItem(deviceLabel(mach), QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
        if (mach == current) m_deviceCombo->setCurrentIndex(m_deviceCombo->count() - 1);
    }
}

void TimeAlignBar::setOffset(qint64 ms)
{
    m_offsetLabel->setText(TimeAlignSession::formatOffset(ms));
}

void TimeAlignBar::setStatus(const QString &text, Severity severity)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(severity == Severity::Warning ? QStringLiteral("color: #cc6600;") : QString());
}

void TimeAlignBar::setSameAsLastNightEnabled(bool enabled)
{
    m_sameAsLastNight->setEnabled(enabled);
}

QString TimeAlignBar::deviceLabel(Machine *mach)
{
    if (!mach) return QString();
    QString label = (mach->brand() + " " + mach->model()).trimmed();
    if (label.isEmpty()) label = mach->loaderName();
    if (!mach->serial().isEmpty()) label += " (" + mach->serial() + ")";
    return label;
}
