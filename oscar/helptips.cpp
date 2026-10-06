/* Hover explanations
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helptips.h"

#include <QApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <QWidget>

#include "SleepLib/appsettings.h"
#include "glossary.h"

namespace {
const char *kKeyProperty = "helpKey";
}

HelpTips::HelpTips()
{
    if (qApp) qApp->installEventFilter(this);
}

HelpTips *HelpTips::instance()
{
    static HelpTips *tips = new HelpTips;
    return tips;
}

bool HelpTips::enabled() const
{
    return AppSetting ? AppSetting->hoverHelp() : m_enabled;
}

void HelpTips::setEnabled(bool on)
{
    if (AppSetting) AppSetting->setHoverHelp(on);
    m_enabled = on;
    emit enabledChanged(on);
}

void HelpTips::attach(QWidget *w, const QString &key)
{
    if (w) w->setProperty(kKeyProperty, key);
}

QString HelpTips::term(const QString &text, const QString &key)
{
    if (!instance()->enabled() || !Glossary::find(key)) return text;
    return QStringLiteral("<a href='help:%1' title=\"%2\" style='color:inherit;text-decoration:none'>%3</a>")
        .arg(key, Glossary::tooltip(key).toHtmlEscaped(), text);
}

QString HelpTips::keyOf(const QUrl &url)
{
    return url.scheme() == QLatin1String("help") ? url.path() : QString();
}

void HelpTips::hover(const QString &key)
{
    if (!key.isEmpty()) emit hovered(key);
}

void HelpTips::open(const QString &key)
{
    if (!key.isEmpty()) emit openRequested(key);
}

bool HelpTips::eventFilter(QObject *o, QEvent *e)
{
    if ((e->type() == QEvent::ToolTip || e->type() == QEvent::Enter) && o->isWidgetType() && enabled()) {
        const QString key = o->property(kKeyProperty).toString();
        if (!key.isEmpty()) {
            if (e->type() == QEvent::Enter) {
                hover(key);
            } else if (const QString tip = Glossary::tooltip(key); !tip.isEmpty()) {
                QToolTip::showText(static_cast<QHelpEvent *>(e)->globalPos(), tip, static_cast<QWidget *>(o));
                return true;
            }
        }
    }
    return QObject::eventFilter(o, e);
}
