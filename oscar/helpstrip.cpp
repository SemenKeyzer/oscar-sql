/* Help strip
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helpstrip.h"

#include "glossary.h"
#include "helptips.h"

HelpStrip::HelpStrip(const QStringList &prefixes, QWidget *parent)
    : QTextBrowser(parent), m_prefixes(prefixes)
{
    setObjectName(QStringLiteral("helpStrip"));
    setOpenLinks(false);
    setFixedHeight(fontMetrics().lineSpacing() * 5 + 2 * frameWidth() + 8);   // about four lines of text
    showEmpty();

    HelpTips *tips = HelpTips::instance();
    connect(tips, &HelpTips::hovered, this, [this](const QString &key) {
        for (const QString &p : m_prefixes) {
            if (key.startsWith(p)) {
                showKey(key);
                return;
            }
        }
    });
    connect(tips, &HelpTips::enabledChanged, this, &QWidget::setVisible);
    // "See also" inside the strip switches the strip
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { showKey(HelpTips::keyOf(url)); });
    setVisible(tips->enabled());
}

void HelpStrip::showKey(const QString &key)
{
    if (key.isEmpty() || key == m_key) return;
    const QString html = Glossary::panel(key);
    if (html.isEmpty()) return;
    m_key = key;
    setHtml(html);
}

void HelpStrip::showEmpty()
{
    setHtml(QStringLiteral("<p style='color:gray'>%1</p>").arg(tr("Hover over a setting to see what it does.").toHtmlEscaped()));
}
