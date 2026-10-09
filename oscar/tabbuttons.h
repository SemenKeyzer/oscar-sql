/* Tab buttons
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TABBUTTONS_H
#define TABBUTTONS_H

#include <QHash>
#include <QList>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QTabWidget;

//! The pages of a tab widget as rows of buttons, the current one lit, in place of its tab bar (which
//! is hidden): every page is in sight however narrow the panel.
class TabButtons : public QWidget
{
    Q_OBJECT
  public:
    //! One button per page of \a rows, in that order; the buttons say the pages' tab texts.
    TabButtons(QTabWidget *tabs, const QList<QList<QWidget *>> &rows, QWidget *parent = nullptr);

    //! Shows or hides \a page with its button; hiding the current page selects the first one shown.
    void setPageVisible(QWidget *page, bool visible);
    QAbstractButton *button(QWidget *page) const { return m_buttons.value(page); }

  private:
    void follow(int index);
    QTabWidget *m_tabs;
    QButtonGroup *m_group;
    QList<QWidget *> m_order;
    QHash<QWidget *, QAbstractButton *> m_buttons;
};

#endif // TABBUTTONS_H
