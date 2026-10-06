/* OSCAR 
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MYTEXTBROWSER_H
#define MYTEXTBROWSER_H

#include <QTextBrowser>

class MyTextBrowser:public QTextBrowser
{
    Q_OBJECT
public:
    MyTextBrowser(QWidget * parent);
    virtual ~MyTextBrowser() {}
    virtual QVariant loadResource(int type, const QUrl &url) Q_DECL_OVERRIDE;
protected:
    //! A click on a "help:" term opens its explanation and does not reach the page's links.
    void mouseReleaseEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
};


#endif // MYTEXTBROWSER_H
