/* Hover explanations
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef HELPTIPS_H
#define HELPTIPS_H

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QWidget;

//! The explanations shown when hovering figures and terms: the on/off switch, the widgets
//! tagged with a glossary key, the terms of HTML pages, and the "hovered" signal the help
//! panel follows.
class HelpTips : public QObject
{
    Q_OBJECT
  public:
    static HelpTips *instance();

    bool enabled() const;
    void setEnabled(bool on);

    //! Tags \a w with the glossary entry \a key.
    static void attach(QWidget *w, const QString &key);
    //! \a text as a link to the glossary entry \a key, with the short explanation as its tooltip;
    //! \a text as it is when the explanations are off or the key is unknown.
    static QString term(const QString &text, const QString &key);
    //! The key of a "help:key" link; empty for any other link.
    static QString keyOf(const QUrl &url);
    //! Emits hovered(\a key) when it is not empty.
    void hover(const QString &key);
    //! A "help:" link was clicked: shows its entry.
    void open(const QString &key);

  signals:
    void hovered(const QString &key);
    void openRequested(const QString &key);
    void enabledChanged(bool on);

  protected:
    bool eventFilter(QObject *o, QEvent *e) override;

  private:
    HelpTips();
    bool m_enabled = true;   // used when there are no application settings (tests)
    QPointer<QCoreApplication> m_filtered;   // the application the filter is installed on
};

#endif // HELPTIPS_H
