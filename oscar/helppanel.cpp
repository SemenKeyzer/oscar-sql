/* Help panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "helppanel.h"

#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "glossary.h"
#include "helptips.h"

HelpPanel::HelpPanel(QWidget *parent) : QDockWidget(tr("Help Panel"), parent)
{
    setObjectName(QStringLiteral("helpPanel"));   // for the main window's saved state
    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    m_search = new QLineEdit(body);
    m_search->setObjectName(QStringLiteral("helpSearch"));   // the key of its own explanation
    m_search->setPlaceholderText(tr("Search the explanations"));
    m_search->setClearButtonEnabled(true);
    m_results = new QListWidget(body);
    m_results->hide();
    m_text = new QTextBrowser(body);
    m_text->setOpenLinks(false);
    layout->addWidget(m_search);
    layout->addWidget(m_results);
    layout->addWidget(m_text, 1);
    setWidget(body);
    setMinimumWidth(220);

    connect(m_search, &QLineEdit::textChanged, this, &HelpPanel::search);
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        show(item->data(Qt::UserRole).toString());
    });
    connect(m_results, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {   // arrow keys
        if (item) show(item->data(Qt::UserRole).toString());
    });
    connect(m_text, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { show(HelpTips::keyOf(url)); });
    // the article of the term last hovered stays until another one is hovered
    connect(HelpTips::instance(), &HelpTips::hovered, this, &HelpPanel::show);
    connect(HelpTips::instance(), &HelpTips::openRequested, this, [this](const QString &key) {
        QDockWidget::show();
        raise();
        show(key);
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) render();
    });
    showEmpty();
}

void HelpPanel::show(const QString &key)
{
    if (key == m_key || !Glossary::find(key)) return;   // hovering the same graph keeps sending its key
    m_key = key;
    m_stale = true;
    if (isVisible()) render();   // a closed panel draws the article when it is opened
}

void HelpPanel::render()
{
    if (!m_stale) return;
    m_stale = false;
    m_text->setHtml(Glossary::panel(m_key));
}

void HelpPanel::search(const QString &text)
{
    m_results->clear();
    const QStringList keys = Glossary::search(text);
    for (const QString &key : keys) {
        const GlossaryEntry *e = Glossary::find(key);
        const QString detail = !e->place.isEmpty() ? e->place : e->expansion;   // a control shows where it is
        auto *item = new QListWidgetItem(detail.isEmpty() ? e->term : e->term + QStringLiteral(" — ") + detail, m_results);
        item->setData(Qt::UserRole, key);
    }
    m_results->setVisible(!text.trimmed().isEmpty());
    if (!text.trimmed().isEmpty() && keys.isEmpty()) m_results->addItem(tr("Nothing found"));
}

void HelpPanel::showEmpty()
{
    m_text->setHtml(QStringLiteral("<p>%1</p><p>%2</p>")
                        .arg(tr("Hover over a figure or a term to see what it means.").toHtmlEscaped(),
                             tr("Or search the explanations above.").toHtmlEscaped()));
}
