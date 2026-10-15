/* Analysis mark
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "analysismark.h"

#include <QCoreApplication>
#include <QObject>
#include <QPixmap>
#include <QTreeWidgetItem>

namespace AnalysisMark {

const char *const BarColor = "#7F77DD";
const char *const PillBackground = "#EEEDFE";
const char *const PillText = "#3C3489";

namespace {
QString word() { return QCoreApplication::translate("AnalysisMark", "analysis"); }
QString hoverText() { return QCoreApplication::translate("AnalysisMark", "OSCAR's second opinion, experimental"); }
}

QString pill()
{
    return QStringLiteral("<span title='%1' style='background-color:%2; color:%3; font-size:small;'>&nbsp;%4&nbsp;</span>")
        .arg(hoverText().toHtmlEscaped().replace(QLatin1Char('\''), QStringLiteral("&#39;")),
             QLatin1String(PillBackground), QLatin1String(PillText), word().toHtmlEscaped());
}

QString printPill()
{
    return QStringLiteral("<span style='color:%1;'>[%2]</span>").arg(QLatin1String(PillText), word().toHtmlEscaped());
}

QString barMark()
{
    return QStringLiteral("<font color='%1'>%2</font>&nbsp;").arg(QLatin1String(BarColor), QString(QChar(0x258C)));
}

QString headingRow(const QString &title, int colspan)
{
    return QStringLiteral("<tr><td colspan=%1>%2<b>%3</b> %4</td></tr>\n").arg(colspan).arg(barMark(), title, pill());
}

QString title(const QString &base)
{
    return QObject::tr("%1 · analysis").arg(base);
}

QTreeWidgetItem *treeHeading(const QString &text, bool analysis)
{
    auto *item = new QTreeWidgetItem(QStringList(text));
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, Qt::UserRole + 1, QStringLiteral("heading"));
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    if (analysis) {
        item->setBackground(0, QColor(QLatin1String(PillBackground)));
        item->setForeground(0, QColor(QLatin1String(PillText)));
        QPixmap bar(4, 14);
        bar.fill(QColor(QLatin1String(BarColor)));
        item->setIcon(0, QIcon(bar));
        item->setToolTip(0, hoverText());
    }
    return item;
}

} // namespace AnalysisMark
