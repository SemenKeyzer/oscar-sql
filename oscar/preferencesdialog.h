/* OSCAR Preferences Dialog Headers
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>
#include <QModelIndex>
#include <QListWidgetItem>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextEdit>
#include "SleepLib/profiles.h"
#include "preferencessearch.h"

namespace Ui {
class PreferencesDialog;
}


/*! \class MySortFilterProxyModel
    \brief Enables the Graph tabs view to be filtered
    */
class MySortFilterProxyModel: public QSortFilterProxyModel
{
    Q_OBJECT
  public:
    MySortFilterProxyModel(QObject *parent = 0);

  protected:
    //! \brief Simply extends filterAcceptRow to scan children as well
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;
};

/*! \class PreferencesDialog
    \brief OSCAR's Main Preferences Window

    This provides the Preferences form and logic to alter Preferences for OSCAR
*/
class AnalysisPreferencesPage;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit PreferencesDialog(QWidget *parent, Profile *_profile);
    ~PreferencesDialog();

    //! \brief Save the current preferences, called when Ok button is clicked on.
    bool Save();

    QString clinicalHelp();

  protected:
    //! Enter and the arrow keys work the search; Escape clears it before it closes the dialog.
    bool eventFilter(QObject *watched, QEvent *event) override;
#ifndef NO_CHECKUPDATES
    //! \brief Updates the date text of the last time updates where checked
    void RefreshLastChecked();
#endif

  private slots:
    //! Lists the settings matching what was typed in the search field.
    void searchChanged(const QString &text);
    //! Shows the setting at \a row of the list.
    void searchChosen(int row);
    void on_combineSlider_valueChanged(int value);

    void on_IgnoreSlider_valueChanged(int value);

    //void on_genOpWidget_itemActivated(QListWidgetItem *item);

    void on_createSDBackups_toggled(bool checked);
    void on_combineSimilarMachines_toggled(bool checked);

    void on_okButton_clicked();

    void on_scrollDampeningSlider_valueChanged(int value);

    void on_tooltipTimeoutSlider_valueChanged(int value);

    void on_gridLineOpacitySlider_valueChanged(int value);

    void on_createSDBackups_clicked(bool checked);

    void on_resetChannelDefaults_clicked();

    void on_channelSearch_textChanged(const QString &arg1);

    void on_chanView_doubleClicked(const QModelIndex &index);

    void on_waveSearch_textChanged(const QString &arg1);

    void on_resetWaveformChannels_clicked();

    void on_waveView_doubleClicked(const QModelIndex &index);

    void on_maskLeaks4Slider_valueChanged(int value);

    void on_maskLeaks20Slider_valueChanged(int value);

    void on_calculateUnintentionalLeaks_toggled(bool arg1);

    void on_resetOxiMetryDefaults_clicked();
    #if defined(STEADY_BREATHING_ENHANCED_TESTING)
        void on_resetSteadyBreathingDefaults_clicked();
    #endif

private:
    void InitChanInfo();
    void InitWaveInfo();

    void saveChanInfo();
    void saveWaveInfo();

    QHash<MachineType, QStandardItem *> toplevel;
    QHash<MachineType, QStandardItem *> machlevel;

    Ui::PreferencesDialog *ui;
    AnalysisPreferencesPage *m_analysisPage = nullptr;
    class QLineEdit *m_search = nullptr;
    class QListWidget *m_searchResults = nullptr;
    QList<PreferencesSearch::Entry> m_searchFound;
    Profile *profile;
    QHash<ChannelID, QColor> m_new_colors;

    QStringList importLocations;
    QStringListModel *importModel;

    MySortFilterProxyModel * chanFilterModel;
    QStandardItemModel *chanModel;

    MySortFilterProxyModel * waveFilterModel;
    QStandardItemModel *waveModel;
};


#endif // PREFERENCESDIALOG_H
