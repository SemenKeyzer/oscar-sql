/* Help panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPPANEL_H
#define HELPPANEL_H

#include <QDockWidget>

class QLineEdit;
class QListWidget;
class QTextBrowser;

//! The full explanation of the figure or term last hovered, with "see also" and a search of
//! the glossary.
class HelpPanel : public QDockWidget
{
    Q_OBJECT
  public:
    explicit HelpPanel(QWidget *parent = nullptr);

    //! Shows the glossary entry \a key; an unknown key leaves the panel as it is.
    void show(const QString &key);
    QString currentKey() const { return m_key; }

  private:
    void render();
    void search(const QString &text);
    void showEmpty();

    QLineEdit *m_search = nullptr;
    QListWidget *m_results = nullptr;
    QTextBrowser *m_text = nullptr;
    QString m_key;
    bool m_stale = false;   // m_key changed while the panel was closed
};

#endif // HELPPANEL_H
