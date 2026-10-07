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
#include <QBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QToolTip>
#include <QWidget>

#include "SleepLib/appsettings.h"
#include "glossary.h"

namespace {
const char *kKeyProperty = "helpKey";

//! The widget after \a label in its row (grid, form or horizontal box); null when there is none.
QWidget *rowNeighbour(QLabel *label, QLayout *layout)
{
    if (!layout) return nullptr;
    const int i = layout->indexOf(label);
    if (i < 0) {
        for (int j = 0; j < layout->count(); ++j) {
            if (QWidget *w = rowNeighbour(label, layout->itemAt(j)->layout())) return w;
        }
        return nullptr;
    }
    if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
        int row, col, rowSpan, colSpan;
        grid->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
        for (int c = col + colSpan; c < grid->columnCount(); ++c) {
            if (QLayoutItem *item = grid->itemAtPosition(row, c); item && item->widget()) return item->widget();
        }
        return nullptr;
    }
    if (auto *form = qobject_cast<QFormLayout *>(layout)) {
        int row;
        QFormLayout::ItemRole role;
        form->getItemPosition(i, &row, &role);
        QLayoutItem *field = role == QFormLayout::LabelRole ? form->itemAt(row, QFormLayout::FieldRole) : nullptr;
        return field ? field->widget() : nullptr;
    }
    auto *box = qobject_cast<QBoxLayout *>(layout);
    if (!box || (box->direction() != QBoxLayout::LeftToRight && box->direction() != QBoxLayout::RightToLeft)) return nullptr;
    // only the item right after the label: a spacer or stretch ends the row's label
    return i + 1 < box->count() ? box->itemAt(i + 1)->widget() : nullptr;
}
}

HelpTips::HelpTips() {}

HelpTips *HelpTips::instance()
{
    static HelpTips *tips = new HelpTips;
    // on the application in use (tests make a new one per test class)
    if (qApp && tips->m_filtered != qApp) {
        qApp->installEventFilter(tips);
        tips->m_filtered = qApp;
    }
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

int HelpTips::attachAll(QWidget *root, const QString &window)
{
    if (!root) return 0;
    const QString prefix = QStringLiteral("ui.") + window + QLatin1Char('.');
    auto keyOfName = [&prefix](const QObject *o) {
        const QString key = prefix + o->objectName();
        return !o->objectName().isEmpty() && Glossary::find(key) ? key : QString();
    };
    int tagged = 0;
    QList<QWidget *> widgets = root->findChildren<QWidget *>();
    widgets.prepend(root);
    for (QWidget *w : widgets) {
        if (!w->property(kKeyProperty).toString().isEmpty()) continue;
        QString key = keyOfName(w);
        if (key.isEmpty()) {
            // a label explains the field it names
            if (auto *label = qobject_cast<QLabel *>(w); label && label->buddy()) key = keyOfName(label->buddy());
        }
        if (!key.isEmpty()) {
            attach(w, key);
            ++tagged;
        }
    }
    // a label without a buddy explains the control after it in its row
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (!label->property(kKeyProperty).toString().isEmpty() || label->buddy() || !label->parentWidget()) continue;
        QWidget *next = rowNeighbour(label, label->parentWidget()->layout());
        const QString key = next ? next->property(kKeyProperty).toString() : QString();
        if (!key.isEmpty() && key.startsWith(prefix)) {
            attach(label, key);
            ++tagged;
        }
    }
    return tagged;
}

void HelpTips::attachMenus(QWidget *owner)
{
    if (!owner) return;
    for (QMenu *menu : owner->findChildren<QMenu *>()) {
        if (menu->property("helpMenuWatched").toBool()) continue;
        menu->setProperty("helpMenuWatched", true);
        connect(menu, &QMenu::hovered, instance(), [](QAction *action) {
            HelpTips *tips = instance();
            if (!tips->enabled() || !action) return;
            // a submenu is explained by its own name: its items may be made in code
            const QString name = action->menu() ? action->menu()->objectName() : action->objectName();
            if (name.isEmpty()) return;
            const QString key = QStringLiteral("ui.menu.") + name;
            if (Glossary::find(key)) tips->hover(key);
        });
    }
}

QString HelpTips::keyFor(QWidget *w)
{
    // the key of the widget or of the tile/frame/group it sits in
    QString key;
    for (; w && key.isEmpty(); w = w->isWindow() ? nullptr : w->parentWidget()) key = w->property(kKeyProperty).toString();
    return key;
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
    if (!key.isEmpty() && enabled()) emit hovered(key);
}

void HelpTips::open(const QString &key)
{
    if (!key.isEmpty()) emit openRequested(key);
}

bool HelpTips::eventFilter(QObject *o, QEvent *e)
{
    if ((e->type() == QEvent::ToolTip || e->type() == QEvent::Enter) && o->isWidgetType() && enabled()) {
        QString key = keyFor(static_cast<QWidget *>(o));
        if (auto *menu = qobject_cast<QMenu *>(o); menu && key.isEmpty() && menu->activeAction()) {
            // a menu drawn by Qt (Windows, Linux): the highlighted item or submenu
            QAction *a = menu->activeAction();
            const QString name = a->menu() ? a->menu()->objectName() : a->objectName();
            const QString itemKey = QStringLiteral("ui.menu.") + name;
            if (!name.isEmpty() && Glossary::find(itemKey)) key = itemKey;
        }
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
