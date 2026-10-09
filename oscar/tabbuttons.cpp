/* Tab buttons
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tabbuttons.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "overviewpresets.h"

TabButtons::TabButtons(QTabWidget *tabs, const QList<QList<QWidget *>> &rows, QWidget *parent)
    : QWidget(parent), m_tabs(tabs), m_group(new QButtonGroup(this))
{
    setObjectName(QStringLiteral("tabButtons"));
    m_group->setExclusive(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    for (const QList<QWidget *> &row : rows) {
        auto *line = new QHBoxLayout;
        line->setSpacing(2);
        for (QWidget *page : row) {
            auto *button = new QToolButton(this);
            button->setObjectName(QStringLiteral("tabButton_") + page->objectName());
            button->setText(tabs->tabText(tabs->indexOf(page)));
            button->setCheckable(true);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setStyleSheet(OverviewPresets::buttonStyle());
            m_group->addButton(button);
            m_buttons.insert(page, button);
            m_order << page;
            connect(button, &QToolButton::clicked, this, [this, page]() { m_tabs->setCurrentWidget(page); });
            line->addWidget(button);
        }
        layout->addLayout(line);
    }
    tabs->tabBar()->hide();
    connect(tabs, &QTabWidget::currentChanged, this, &TabButtons::follow);
    follow(tabs->currentIndex());
}

void TabButtons::follow(int index)
{
    if (QAbstractButton *button = m_buttons.value(m_tabs->widget(index))) button->setChecked(true);
}

void TabButtons::setPageVisible(QWidget *page, bool visible)
{
    QAbstractButton *button = m_buttons.value(page);
    if (!button) return;
    button->setVisible(visible);
    m_tabs->setTabVisible(m_tabs->indexOf(page), visible);
    if (!visible && m_tabs->currentWidget() == page) {
        for (QWidget *other : std::as_const(m_order)) {
            if (other != page && !m_buttons.value(other)->isHidden()) {
                m_tabs->setCurrentWidget(other);
                break;
            }
        }
    }
    follow(m_tabs->currentIndex());
}
