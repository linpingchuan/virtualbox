/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsDlg class implementation
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include "VBoxVMSettingsDlg.h"
#include "VBoxVMSettingsGeneral.h"
#include "VBoxVMSettingsHD.h"
#include "VBoxVMSettingsCD.h"
#include "VBoxVMSettingsFD.h"
#include "VBoxVMSettingsAudio.h"
#include "VBoxVMSettingsNetwork.h"
#include "VBoxVMSettingsSerial.h"
#include "VBoxVMSettingsParallel.h"
#include "VBoxVMSettingsUSB.h"
#include "VBoxVMSettingsSF.h"
#include "VBoxVMSettingsVRDP.h"

#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "QIWidgetValidator.h"

/* Qt includes */
#include <QTimer>

/**
 *  Returns the path to the item in the form of 'grandparent > parent > item'
 *  using the text of the first column of every item.
 */
static QString path (QTreeWidgetItem *aItem)
{
    static QString sep = ": ";
    QString p;
    QTreeWidgetItem *cur = aItem;
    while (cur)
    {
        if (!p.isNull())
            p = sep + p;
        p = cur->text (0).simplified() + p;
        cur = cur->parent();
    }
    return p;
}

static QTreeWidgetItem* findItem (QTreeWidget *aView,
                                  const QString &aMatch, int aColumn)
{
    QList<QTreeWidgetItem*> list =
        aView->findItems (aMatch, Qt::MatchExactly, aColumn);

    return list.count() ? list [0] : 0;
}

class VBoxWarnIconLabel: public QWidget
{
public:
    VBoxWarnIconLabel (QWidget *aParent = NULL)
      : QWidget (aParent)
    {
        QHBoxLayout *layout = new QHBoxLayout (this);
        VBoxGlobal::setLayoutMargin (layout, 0);
        layout->addWidget (&mIcon);
        layout->addWidget (&mLabel);
    }
    void setWarningPixmap (const QPixmap& aPixmap) { mIcon.setPixmap (aPixmap); }
    void setWarningText (const QString& aText) { mLabel.setText (aText); }

private:
    QLabel mIcon;
    QLabel mLabel;
};

VBoxVMSettingsDlg::VBoxVMSettingsDlg (QWidget *aParent,
                                      const QString &aCategory,
                                      const QString &aControl)
    : QIWithRetranslateUI<QIMainDialog> (aParent)
    , mPolished (false)
    , mAllowResetFirstRunFlag (false)
    , mValid (true)
    , mWhatsThisTimer (new QTimer (this))
    , mWhatsThisCandidate (NULL)
{
    /* Apply UI decorations */
    Ui::VBoxVMSettingsDlg::setupUi (this);

#ifndef Q_WS_MAC
    setWindowIcon (QIcon (":/settings_16px.png"));
#endif /* Q_WS_MAC */

    mWarnIconLabel = new VBoxWarnIconLabel();

    /* Setup warning icon */
    QIcon icon = vboxGlobal().standardIcon (QStyle::SP_MessageBoxWarning, this);
    if (!icon.isNull())
        mWarnIconLabel->setWarningPixmap (icon.pixmap (16, 16));

    mButtonBox->addExtraWidget (mWarnIconLabel);

    /* Page title font is derived from the system font */
    QFont f = font();
    f.setBold (true);
    f.setPointSize (f.pointSize() + 2);
    mLbTitle->setFont (f);

    /* Setup the what's this label */
    qApp->installEventFilter (this);
    mWhatsThisTimer->setSingleShot (true);
    connect (mWhatsThisTimer, SIGNAL (timeout()), this, SLOT (updateWhatsThis()));

    mLbWhatsThis->setFixedHeight (mLbWhatsThis->frameWidth() * 2 +
                                  6 /* seems that RichText adds some margin */ +
                                  mLbWhatsThis->fontMetrics().lineSpacing() * 4);
    mLbWhatsThis->setMinimumWidth (mLbWhatsThis->frameWidth() * 2 +
                                   6 /* seems that RichText adds some margin */ +
                                   mLbWhatsThis->fontMetrics().width ('m') * 40);

    /*
     *  Setup connections and set validation for pages
     *  ----------------------------------------------------------------------
     */

    /* Common connections */

    connect (mButtonBox, SIGNAL (accepted()), this, SLOT (accept()));
    connect (mButtonBox, SIGNAL (rejected()), this, SLOT (reject()));
    connect (mButtonBox, SIGNAL (helpRequested()), &vboxProblem(), SLOT (showHelpHelpDialog()));
    connect (mTwSelector, SIGNAL (currentItemChanged (QTreeWidgetItem*, QTreeWidgetItem*)),
             this, SLOT (settingsGroupChanged (QTreeWidgetItem *, QTreeWidgetItem*)));
    connect (&vboxGlobal(), SIGNAL (mediaEnumFinished (const VBoxMediaList &)),
             this, SLOT (onMediaEnumerationDone()));

    /* Parallel Port Page (currently disabled) */
    //QTreeWidgetItem *item = findItem (mTwSelector, "#parallelPorts", listView_Link);
    //Assert (item);
    //if (item) item->setHidden (true);

    /*
     *  Set initial values
     *  ----------------------------------------------------------------------
     */

    /* Common settings */

    /* Hide unnecessary columns and header */
    mTwSelector->header()->hide();
    mTwSelector->hideColumn (listView_Id);
    mTwSelector->hideColumn (listView_Link);

    /* Adjust selector list */
    int minWid = 0;
    for (int i = 0; i < mTwSelector->topLevelItemCount(); ++ i)
    {
        QTreeWidgetItem *item = mTwSelector->topLevelItem (i);
        QFontMetrics fm (item->font (0));
        int wid = fm.width (item->text (0)) +
                  16 /* icon */ + 2 * 8 /* 2 margins */;
        minWid = wid > minWid ? wid : minWid;
        int hei = fm.height() > 16 ?
                  fm.height() /* text height */ :
                  16 /* icon */ + 2 * 2 /* 2 margins */;
        item->setSizeHint (0, QSize (wid, hei));
    }
    mTwSelector->setFixedWidth (minWid);

    /* Sort selector by the id column (to have pages in the desired order) */
    mTwSelector->sortItems (listView_Id, Qt::AscendingOrder);

    /* Initially select the first settings page */
    mTwSelector->setCurrentItem (mTwSelector->topLevelItem (0));

    /* Setup Settings Dialog */
    if (!aCategory.isNull())
    {
        /* Search for a list view item corresponding to the category */
        QTreeWidgetItem *item = findItem (mTwSelector, aCategory, listView_Link);
        if (item)
        {
            mTwSelector->setCurrentItem (item);

            /* Search for a widget with the given name */
            if (!aControl.isNull())
            {
                QObject *obj = mPageStack->currentWidget()->findChild<QWidget*> (aControl);
                if (obj && obj->isWidgetType())
                {
                    QWidget *w = static_cast<QWidget*> (obj);
                    QList<QWidget*> parents;
                    QWidget *p = w;
                    while ((p = p->parentWidget()) != NULL)
                    {
                        if (p->inherits ("QTabWidget"))
                        {
                            /* The tab contents widget is two steps down
                             * (QTabWidget -> QStackedWidget -> QWidget) */
                            QWidget *c = parents [parents.count() - 1];
                            if (c)
                                c = parents [parents.count() - 2];
                            if (c)
                                static_cast<QTabWidget*> (p)->setCurrentWidget (c);
                        }
                        parents.append (p);
                    }

                    w->setFocus();
                }
            }
        }
    }
    /* Applying language settings */
    retranslateUi();
}

void VBoxVMSettingsDlg::getFromMachine (const CMachine &aMachine)
{
    mMachine = aMachine;

    setWindowTitle (dialogTitle());

    CVirtualBox vbox = vboxGlobal().virtualBox();

    /* General Page */
    VBoxVMSettingsGeneral::getFromMachine (aMachine, mPageGeneral,
                                           this, pagePath (mPageGeneral));

    /* HD */
    VBoxVMSettingsHD::getFromMachine (aMachine, mPageHD,
                                      this, pagePath (mPageHD));

    /* CD */
    VBoxVMSettingsCD::getFromMachine (aMachine, mPageCD,
                                      this, pagePath (mPageCD));

    /* FD */
    VBoxVMSettingsFD::getFromMachine (aMachine, mPageFD,
                                      this, pagePath (mPageFD));

    /* Audio */
    VBoxVMSettingsAudio::getFromMachine (aMachine, mPageAudio);

    /* Network */
    VBoxVMSettingsNetwork::getFromMachine (aMachine, mPageNetwork,
                                           this, pagePath (mPageNetwork));

    /* Serial Ports */
    VBoxVMSettingsSerial::getFromMachine (aMachine, mPageSerial,
                                          this, pagePath (mPageSerial));

    /* Parallel Ports */
    VBoxVMSettingsParallel::getFromMachine (aMachine, mPageParallel,
                                            this, pagePath (mPageParallel));

    /* USB */
    VBoxVMSettingsUSB::getFromMachine (aMachine, mPageUSB,
                                       this, pagePath (mPageUSB));

    /* Shared Folders */
    VBoxVMSettingsSF::getFromMachineEx (aMachine, mPageShared, this);

    /* Vrdp */
    VBoxVMSettingsVRDP::getFromMachine (aMachine, mPageVrdp,
                                        this, pagePath (mPageVrdp));

    /* Finally set the reset First Run Wizard flag to "false" to make sure
     * user will see this dialog if he hasn't change the boot-order
     * and/or mounted images configuration */
    mResetFirstRunFlag = false;
}

COMResult VBoxVMSettingsDlg::putBackToMachine()
{
    CVirtualBox vbox = vboxGlobal().virtualBox();

    /* General Page */
    VBoxVMSettingsGeneral::putBackToMachine();

    /* HD */
    VBoxVMSettingsHD::putBackToMachine();

    /* CD */
    VBoxVMSettingsCD::putBackToMachine();

    /* FD */
    VBoxVMSettingsFD::putBackToMachine();

    /* Clear the "GUI_FirstRun" extra data key in case if the boot order
     * and/or disk configuration were changed */
    if (mResetFirstRunFlag)
        mMachine.SetExtraData (VBoxDefs::GUI_FirstRun, QString::null);

    /* Audio */
    VBoxVMSettingsAudio::putBackToMachine();

    /* Network */
    VBoxVMSettingsNetwork::putBackToMachine();

    /* Serial ports */
    VBoxVMSettingsSerial::putBackToMachine();

    /* Parallel ports */
    VBoxVMSettingsParallel::putBackToMachine();

    /* USB */
    VBoxVMSettingsUSB::putBackToMachine();

    /* Shared folders */
    VBoxVMSettingsSF::putBackToMachineEx();

    /* Vrdp */
    VBoxVMSettingsVRDP::putBackToMachine();

    return COMResult();
}


void VBoxVMSettingsDlg::retranslateUi()
{
    /* Unfortunately retranslateUi clears the QTreeWidget to do the
     * translation. So save the current selected index. */
    int ci = mPageStack->currentIndex();
    /* Translate uic generated strings */
    Ui::VBoxVMSettingsDlg::retranslateUi (this);
    /* Set the old index */
    mTwSelector->setCurrentItem (mTwSelector->topLevelItem (ci));

    mWarnIconLabel->setWarningText (tr ("Invalid settings detected"));
    mButtonBox->button (QDialogButtonBox::Ok)->setWhatsThis (tr ("Accepts (saves) changes and closes the dialog."));
    mButtonBox->button (QDialogButtonBox::Cancel)->setWhatsThis (tr ("Cancels changes and closes the dialog."));
    mButtonBox->button (QDialogButtonBox::Help)->setWhatsThis (tr ("Displays the dialog help."));

    setWindowTitle (dialogTitle());
    
    /* We have to make sure that the Serial & Network subpages are retranslated
     * before they are revalidated. Cause: They do string comparing within
     * vboxGlobal which is retranslated at that point already. */
    QEvent* event = new QEvent (QEvent::LanguageChange);
    qApp->sendEvent (mPageSerial, event);
    qApp->sendEvent (mPageNetwork, event);

    /* Revalidate all pages to retranslate the warning messages also. */
    QList<QIWidgetValidator*> l = this->findChildren<QIWidgetValidator*>();
    foreach (QIWidgetValidator *wval, l)
        if (!wval->isValid())
            revalidate (wval);
}

void VBoxVMSettingsDlg::enableOk (const QIWidgetValidator*)
{
    setWarning (QString::null);
    QString wvalWarning;

    /* Detect the overall validity */
    bool newValid = true;
    {
        QList<QIWidgetValidator*> l = this->findChildren<QIWidgetValidator*>();
        foreach (QIWidgetValidator *wval, l)
        {
            newValid = wval->isValid();
            if (!newValid)
            {
                wvalWarning = wval->warningText();
                break;
            }
        }
    }

    if (mWarnString.isNull() && !wvalWarning.isNull())
    {
        /* Try to set the generic error message when invalid but no specific
         * message is provided */
        setWarning (wvalWarning);
    }

    if (mValid != newValid)
    {
        mValid = newValid;
        mButtonBox->button (QDialogButtonBox::Ok)->setEnabled (mValid);
        mWarnIconLabel->setVisible (!mValid);
    }
}

void VBoxVMSettingsDlg::revalidate (QIWidgetValidator *aWval)
{
    /* do individual validations for pages */
    QWidget *pg = aWval->widget();
    bool valid = aWval->isOtherValid();

    QString warningText;
    QString pageTitle = pagePath (pg);

    if (pg == mPageHD)
        valid = VBoxVMSettingsHD::revalidate (warningText);
    else if (pg == mPageCD)
        valid = VBoxVMSettingsCD::revalidate (warningText);
    else if (pg == mPageFD)
        valid = VBoxVMSettingsFD::revalidate (warningText);
    else if (pg == mPageNetwork)
        valid = VBoxVMSettingsNetwork::revalidate (warningText, pageTitle);
    else if (pg == mPageSerial)
        valid = VBoxVMSettingsSerial::revalidate (warningText, pageTitle);
    else if (pg == mPageParallel)
        valid = VBoxVMSettingsParallel::revalidate (warningText, pageTitle);

    if (!valid)
        setWarning (tr ("%1 on the <b>%2</b> page.")
                    .arg (warningText, pageTitle));

    aWval->setOtherValid (valid);
}

void VBoxVMSettingsDlg::onMediaEnumerationDone()
{
    mAllowResetFirstRunFlag = true;
}

void VBoxVMSettingsDlg::settingsGroupChanged (QTreeWidgetItem *aItem,
                                              QTreeWidgetItem *)
{
    if (aItem)
    {
        int id = aItem->text (1).toInt();
        Assert (id >= 0);
        mLbTitle->setText (::path (aItem));
        mPageStack->setCurrentIndex (id);
    }
}

void VBoxVMSettingsDlg::updateWhatsThis (bool gotFocus /* = false */)
{
    QString text;

    QWidget *widget = 0;
    if (!gotFocus)
    {
        if (mWhatsThisCandidate && mWhatsThisCandidate != this)
            widget = mWhatsThisCandidate;
    }
    else
    {
        widget = QApplication::focusWidget();
    }
    /* If the given widget lacks the whats'this text, look at its parent */
    while (widget && widget != this)
    {
        text = widget->whatsThis();
        if (!text.isEmpty())
            break;
        widget = widget->parentWidget();
    }

    if (text.isEmpty() && !mWarnString.isEmpty())
        text = mWarnString;
    if (text.isEmpty())
        text = whatsThis();

    mLbWhatsThis->setText (text);
}

void VBoxVMSettingsDlg::resetFirstRunFlag()
{
    if (mAllowResetFirstRunFlag)
        mResetFirstRunFlag = true;
}

void VBoxVMSettingsDlg::whatsThisCandidateDestroyed (QObject *aObj /*= NULL*/)
{
    /* sanity */
    Assert (mWhatsThisCandidate == aObj);

    if (mWhatsThisCandidate == aObj)
        mWhatsThisCandidate = NULL;
}

bool VBoxVMSettingsDlg::eventFilter (QObject *aObject, QEvent *aEvent)
{
    if (!aObject->isWidgetType())
        return QIMainDialog::eventFilter (aObject, aEvent);

    QWidget *widget = static_cast<QWidget*> (aObject);
    if (widget->topLevelWidget() != this)
        return QIMainDialog::eventFilter (aObject, aEvent);

    switch (aEvent->type())
    {
        case QEvent::Enter:
        case QEvent::Leave:
        {
            if (aEvent->type() == QEvent::Enter)
            {
                /* What if Qt sends Enter w/o Leave... */
                if (mWhatsThisCandidate)
                    disconnect (mWhatsThisCandidate, SIGNAL (destroyed (QObject *)),
                                this, SLOT (whatsThisCandidateDestroyed (QObject *)));

                mWhatsThisCandidate = widget;
                /* make sure we don't reference a deleted object after the
                 * timer is shot */
                connect (mWhatsThisCandidate, SIGNAL (destroyed (QObject *)),
                         this, SLOT (whatsThisCandidateDestroyed (QObject *)));
            }
            else
            {
                /* cleanup */
                if (mWhatsThisCandidate)
                    disconnect (mWhatsThisCandidate, SIGNAL (destroyed (QObject *)),
                                this, SLOT (whatsThisCandidateDestroyed (QObject *)));
                mWhatsThisCandidate = NULL;
            }

            mWhatsThisTimer->start (100);
            break;
        }
        case QEvent::FocusIn:
        {
            updateWhatsThis (true /* gotFocus */);
            break;
        }
        default:
            break;
    }

    return QIMainDialog::eventFilter (aObject, aEvent);
}

void VBoxVMSettingsDlg::showEvent (QShowEvent *aEvent)
{
    QIMainDialog::showEvent (aEvent);

    /* One may think that QWidget::polish() is the right place to do things
     * below, but apparently, by the time when QWidget::polish() is called,
     * the widget style & layout are not fully done, at least the minimum
     * size hint is not properly calculated. Since this is sometimes necessary,
     * we provide our own "polish" implementation. */

    if (mPolished)
        return;

    mPolished = true;

    /* resize to the minimum possible size */
    resize (minimumSize());

    VBoxGlobal::centerWidget (this, parentWidget());
}

/**
 *  Returns a path to the given page of this settings dialog. See ::path() for
 *  details.
 */
QString VBoxVMSettingsDlg::pagePath (QWidget *aPage)
{
    QTreeWidgetItem *li =
        findItem (mTwSelector,
                  QString ("%1")
                      .arg (mPageStack->indexOf (aPage), 2, 10, QChar ('0')),
                  1);
    return ::path (li);
}

void VBoxVMSettingsDlg::setWarning (const QString &aWarning)
{
    mWarnString = aWarning;
    if (!aWarning.isEmpty())
        mWarnString = QString ("<font color=red>%1</font>").arg (aWarning);

    if (!mWarnString.isEmpty())
        mLbWhatsThis->setText (mWarnString);
    else
        updateWhatsThis (true);
}

QString VBoxVMSettingsDlg::dialogTitle() const
{
    QString dialogTitle;
    if (!mMachine.isNull())
        dialogTitle = tr ("%1 - Settings").arg (mMachine.GetName());
    return dialogTitle;
}

