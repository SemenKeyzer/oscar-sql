/* Help strip
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPSTRIP_H
#define HELPSTRIP_H

#include <QStringList>
#include <QTextBrowser>

class QBoxLayout;

//! A few lines at the bottom of a modal dialog with the explanation of the control last hovered
//! in it: the help panel of the main window is hidden behind the dialog.
class HelpStrip : public QTextBrowser
{
    Q_OBJECT
  public:
    //! Shows the entries whose keys start with one of \a prefixes ("ui.prefs.").
    HelpStrip(const QStringList &prefixes, QWidget *parent);
    //! The key of the entry shown; empty before anything was hovered.
    QString key() const { return m_key; }
    //! Puts \a strip into \a layout right above \a buttons, the dialog's OK/Cancel row.
    static void placeAbove(QBoxLayout *layout, QWidget *buttons, HelpStrip *strip);

  private:
    void showKey(const QString &key);
    void showEmpty();
    QStringList m_prefixes;
    QString m_key;
};

#endif // HELPSTRIP_H
