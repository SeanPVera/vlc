/*****************************************************************************
 * help.cpp : Help and About dialogs
 ****************************************************************************
 * Copyright (C) 2007 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *          Rémi Duraffort <ivoire (at) via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "help.hpp"
#include "util/qt_dirs.hpp"
#include "maininterface/mainctx.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "widgets/native/searchlineedit.hpp"

#include <vlc_about.h>
#include <vlc_intf_strings.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>

#ifdef UPDATE_CHECK
# include <vlc_update.h>
#endif

#include <QTextBrowser>
#include <QString>
#include <QDialogButtonBox>
#include <QEvent>
#include <QDate>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>

#include <cassert>
#include <cstring>

#ifndef NDEBUG
// Uncomment the following line to make use a mock update for debugging purposes,
// it is only applicable when there is no new release found:
// #define UPDATE_MOCK
#endif

HelpDialog::HelpDialog( qt_intf_t *_p_intf ) : QVLCFrame( _p_intf )

{
    setWindowTitle( qtr( "Help" ) );
    setWindowRole( "vlc-help" );
    setMinimumSize( 350, 300 );

    QVBoxLayout *layout = new QVBoxLayout( this );

    QTextBrowser *helpBrowser = new QTextBrowser( this );
    helpBrowser->setOpenExternalLinks( true );
    helpBrowser->setHtml( qfut(I_LONGHELP) );

    QDialogButtonBox *closeButtonBox = new QDialogButtonBox( this );
    closeButtonBox->addButton(
        new QPushButton( qtr("&Close") ), QDialogButtonBox::RejectRole );
    closeButtonBox->setFocus();

    layout->addWidget( helpBrowser );
    layout->addWidget( closeButtonBox );

    connect( closeButtonBox, &QDialogButtonBox::rejected, this, &HelpDialog::close );
    restoreWidgetPosition( "Help", QSize( 500, 450 ) );
}

HelpDialog::~HelpDialog()
{
    saveWidgetPosition( "Help" );
}

/*****************************************************************************
 * ShortcutsDialog
 *****************************************************************************/

namespace {

/* Groups for the shortcut listing, matched against the config name by prefix.
 * First match wins, so longer prefixes are listed before the ones they
 * extend. Anything unmatched lands in the trailing catch-all. */
struct ShortcutGroup
{
    const char *prefix;
    const char *title;
};

const ShortcutGroup SHORTCUT_GROUPS[] =
{
    { "key-subsync",     N_("Subtitles")  },
    { "key-subdelay",    N_("Subtitles")  },
    { "key-subpos",      N_("Subtitles")  },
    { "key-subtitle",    N_("Subtitles")  },
    { "key-audiodelay",  N_("Audio")      },
    { "key-audiodevice", N_("Audio")      },
    { "key-audio",       N_("Audio")      },
    { "key-vol",         N_("Audio")      },
    { "key-jump",        N_("Navigation") },
    { "key-nav",         N_("Navigation") },
    { "key-title",       N_("Navigation") },
    { "key-chapter",     N_("Navigation") },
    { "key-disc",        N_("Navigation") },
    { "key-position",    N_("Navigation") },
    { "key-frame",       N_("Navigation") },
    { "key-set-bookmark",  N_("Bookmarks") },
    { "key-play-bookmark", N_("Bookmarks") },
    { "key-crop",        N_("Video")      },
    { "key-uncrop",      N_("Video")      },
    { "key-zoom",        N_("Video")      },
    { "key-unzoom",      N_("Video")      },
    { "key-aspect",      N_("Video")      },
    { "key-deinterlace", N_("Video")      },
    { "key-viewpoint",   N_("Video")      },
    { "key-projection",  N_("Video")      },
    { "key-wallpaper",   N_("Video")      },
    { "key-snapshot",    N_("Video")      },
    { "key-toggle-autoscale", N_("Video") },
    { "key-incr-scalefactor", N_("Video") },
    { "key-decr-scalefactor", N_("Video") },
    { "key-intf",        N_("Interface")  },
    { "key-toggle-fullscreen", N_("Interface") },
    { "key-leave-fullscreen",  N_("Interface") },
    { "key-quit",        N_("Interface")  },
    { "key-",            N_("Playback")   },
};

const char *groupForConfigName( const char *name )
{
    for( const ShortcutGroup &group : SHORTCUT_GROUPS )
        if( strncmp( name, group.prefix, strlen( group.prefix ) ) == 0 )
            return group.title;
    return N_("Other");
}

} // namespace

ShortcutsDialog::ShortcutsDialog( qt_intf_t *_p_intf ) : QVLCFrame( _p_intf )
{
    setWindowTitle( qtr( "Keyboard Shortcuts" ) );
    setWindowRole( "vlc-shortcuts" );
    setMinimumSize( 400, 350 );

    QVBoxLayout *layout = new QVBoxLayout( this );

    QLabel *intro = new QLabel(
        qtr( "These are the shortcuts currently in effect, including any you "
             "have changed." ), this );
    intro->setWordWrap( true );
    layout->addWidget( intro );

    searchEdit = new SearchLineEdit( this );
    searchEdit->setMinimumHeight( 26 );
    layout->addWidget( searchEdit );

    table = new QTreeWidget( this );
    table->setColumnCount( 2 );
    table->setAlternatingRowColors( true );
    table->setSelectionMode( QAbstractItemView::NoSelection );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->setRootIsDecorated( true );
    table->headerItem()->setText( 0, qtr( "Action" ) );
    table->headerItem()->setText( 1, qtr( "Shortcut" ) );
    layout->addWidget( table );

    populate();

    QDialogButtonBox *buttonBox = new QDialogButtonBox( this );
    QPushButton *editButton = new QPushButton( qtr( "&Edit Shortcuts..." ) );
    buttonBox->addButton( editButton, QDialogButtonBox::ActionRole );
    buttonBox->addButton( new QPushButton( qtr( "&Close" ) ),
                          QDialogButtonBox::RejectRole );
    layout->addWidget( buttonBox );

    connect( searchEdit, &SearchLineEdit::textChanged,
             this, &ShortcutsDialog::filter );
    connect( editButton, &QPushButton::clicked,
             this, &ShortcutsDialog::editShortcuts );
    connect( buttonBox, &QDialogButtonBox::rejected,
             this, &ShortcutsDialog::close );

    restoreWidgetPosition( "Shortcuts", QSize( 550, 500 ) );
}

ShortcutsDialog::~ShortcutsDialog()
{
    saveWidgetPosition( "Shortcuts" );
}

void ShortcutsDialog::populate()
{
    module_t *p_main = module_get_main();
    assert( p_main );

    unsigned confsize;
    module_config_t *p_config = module_config_get( p_main, &confsize );

    QHash<QString, QTreeWidgetItem *> groups;

    for( size_t i = 0; i < confsize; i++ )
    {
        module_config_t *p_item = p_config + i;

        if( p_item->i_type != CONFIG_ITEM_KEY )
            continue;

        /* "global-" duplicates each action for system-wide bindings; listing
         * both would double the table for little gain here. */
        if( strncmp( p_item->psz_name, "global-", 7 ) == 0 )
            continue;

        /* value.psz is the binding in force, not the compiled-in default, so
         * rebound keys and per-platform defaults both come out right. */
        const QString keys = qfu( p_item->value.psz );
        if( keys.isEmpty() )
            continue;

        const QString groupName = qfut( groupForConfigName( p_item->psz_name ) );

        QTreeWidgetItem *parent = groups.value( groupName, nullptr );
        if( parent == nullptr )
        {
            parent = new QTreeWidgetItem( table );
            parent->setText( 0, groupName );
            parent->setFirstColumnSpanned( true );
            parent->setExpanded( true );
            groups.insert( groupName, parent );
        }

        QTreeWidgetItem *item = new QTreeWidgetItem( parent );
        item->setText( 0, qfut( p_item->psz_text ) );
        item->setText( 1, keys );
        if( p_item->psz_longtext )
            item->setToolTip( 0, qfut( p_item->psz_longtext ) );
    }

    module_config_free( p_config );

    table->resizeColumnToContents( 0 );
}

void ShortcutsDialog::filter()
{
    const QString text = searchEdit->text().toLower();

    for( int i = 0; i < table->topLevelItemCount(); i++ )
    {
        QTreeWidgetItem *group = table->topLevelItem( i );
        int visibleChildren = 0;

        for( int j = 0; j < group->childCount(); j++ )
        {
            QTreeWidgetItem *item = group->child( j );
            const bool match = text.isEmpty()
                            || item->text( 0 ).toLower().contains( text )
                            || item->text( 1 ).toLower().contains( text );
            item->setHidden( !match );
            if( match )
                visibleChildren++;
        }

        /* Hide a heading with nothing under it, so a search does not leave
         * empty group rows behind. */
        group->setHidden( visibleChildren == 0 );
    }
}

void ShortcutsDialog::editShortcuts()
{
    /* The listing is read-only on purpose; rebinding stays in one place. */
    DialogsProvider::getInstance()->prefsDialog();
}

AboutDialog::AboutDialog( qt_intf_t *_p_intf)
            : QVLCDialog( nullptr, _p_intf ), b_advanced( false )
{
    /* Build UI */
    ui.setupUi( this );
    setWindowTitle( qtr( "About" ) );
    setWindowRole( "vlc-about" );
    setWindowModality( Qt::WindowModal );

    ui.version->setText(qfu( " " VERSION_MESSAGE ) );
    ui.title->setText("<html><head/><body><p><span style=\" font-size:26pt; color:#353535;\"> " + qtr( "VLC media player" ) + " </span></p></body></html>");

    ui.MainBlabla->setText("<html><head/><body>" +
    qtr( "<p>VLC media player is a free and open source media player, encoder, and streamer made by the volunteers of the <a href=\"https://www.videolan.org/\"><span style=\" text-decoration: underline; color:#0057ae;\">VideoLAN</span></a> community.</p><p>VLC uses its internal codecs, works on essentially every popular platform, and can read almost all files, CDs, DVDs, network streams, capture cards and other media formats!</p><p><a href=\"https://www.videolan.org/contribute/\"><span style=\" text-decoration: underline; color:#0057ae;\">Help and join us!</span></a>" ) +
    "</p></body> </html>");

    const QDate today = QDate::currentDate();
    if( today.month() == 4 && today.day() == 1
            && var_InheritBool( p_intf, "qt-icon-change" ) )
        ui.VLCcone->setPixmap( QPixmap( ":/logo/vlc128-aprilfools.png" ) );

    ui.update->hide();

    /* GPL License */
    ui.licensePage->setText( qfu( psz_license ) );

    /* People who helped */
    ui.creditPage->setText( qfu( psz_thanks ) );

    /* People who wrote the software */
    ui.authorsPage->setText( qfu( psz_authors ) );

    ui.licenseButton->setText( "<html><head/><body><p><span style=\" text-decoration: underline; color:#0057ae;\">"+qtr( "License" )+"</span></p></body></html>");
    ui.licenseButton->installEventFilter( this );

    ui.authorsButton->setText( "<html><head/><body><p><span style=\" text-decoration: underline; color:#0057ae;\">"+qtr( "Authors" )+"</span></p></body></html>");
    ui.authorsButton->installEventFilter( this );

    ui.creditsButton->setText( "<html><head/><body><p><span style=\" text-decoration: underline; color:#0057ae;\">"+qtr( "Credits" )+"</span></p></body></html>");
    ui.creditsButton->installEventFilter( this );

    ui.version->installEventFilter( this );
}

void AboutDialog::showLicense()
{
    ui.stackedWidget->setCurrentWidget( ui.licensePage );
}

void AboutDialog::showAuthors()
{
    ui.stackedWidget->setCurrentWidget( ui.authorsPage );
}

void AboutDialog::showCredit()
{
    ui.stackedWidget->setCurrentWidget( ui.creditPage );
}

bool AboutDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress )
    {
        if( obj == ui.version )
        {
            if( !b_advanced )
            {
                ui.version->setText(qfu( VLC_CompileBy() )+ "@" + qfu( VLC_CompileHost() )
                    + " " + __DATE__ + " " + __TIME__);
                b_advanced = true;
            }
            else
            {
                ui.version->setText(qfu( " " VERSION_MESSAGE ) );
                b_advanced = false;
            }
            return true;
        }
        else if( obj == ui.licenseButton )
            showLicense();
        else if( obj == ui.authorsButton )
            showAuthors();
        else if( obj == ui.creditsButton )
            showCredit();

        return false;
    }

    return QVLCDialog::eventFilter( obj, event);
}

void AboutDialog::showEvent( QShowEvent *event )
{
    ui.stackedWidget->setCurrentWidget( ui.blablaPage );
    QVLCDialog::showEvent( event );
}

#ifdef UPDATE_CHECK

class UpdateModelPrivate
{
public:
    Q_DECLARE_PUBLIC(UpdateModel)
    UpdateModelPrivate(UpdateModel * pub)
        : q_ptr(pub)
    {
    }

    update_t* m_update = nullptr;

    const update_release_t* m_release = nullptr;
    UpdateModel::Status m_status = UpdateModel::Unchecked;
    bool m_explicitCheck = false;

    UpdateModel* q_ptr = nullptr;
};

static void UpdateCallback(void *data, bool b_ret)
{
    auto that = (UpdateModelPrivate*)data;
    QMetaObject::invokeMethod(that->q_func(), [that, b_ret](){
        if (!b_ret)
        {
            that->m_status = UpdateModel::CheckFailed;
            that->m_release = nullptr;
        }
        else
        {
            bool needUpdate = update_NeedUpgrade( that->m_update );
            if (!needUpdate)
            {
#if defined(UPDATE_MOCK) && !defined(NDEBUG)
               static const update_release_t updateReleaseMock = {
                   9,
                   0,
                   99,
                   999,
                   "",
                   // Lorem ipsum with some "Security" to see if red coloring works:
                   "Lorem ipsum dolor sit amet, security consectetur adipiscing elit. Sed vitae ante lobortis," \
                   "condimentum sem et, auctor libero. Aliquam eget mi justo. <br /> <br /> Class aptent taciti sociosqu" \
                   "ad litora Security torquent per conubia nostra, per inceptos himenaeos. Nulla id pretium ante. Nam" \
                   "eu blandit lacus. Proin faucibus in risus quis condimentum. <br /> <br /> Pellentesque a tellus vitae" \
                   "massa tristique cursus. Phasellus fermentum euismod mauris, at ultricies ipsum volutpat eu." \
                   "Praesent arcu lacus, laoreet at lacinia quis, rhoncus at lectus. " \
                   // Repeat:
                   "Lorem ipsum dolor sit amet, security consectetur adipiscing elit. Sed vitae ante lobortis," \
                   "condimentum sem et, auctor libero. Aliquam eget mi justo. <br /> <br /> Class aptent taciti sociosqu" \
                   "ad litora Security torquent per conubia nostra, per inceptos himenaeos. Nulla id pretium ante. Nam" \
                   "eu blandit lacus. Proin faucibus in risus quis condimentum. <br /> <br /> Pellentesque a tellus vitae" \
                   "massa tristique cursus. Phasellus fermentum euismod mauris, at ultricies ipsum volutpat eu." \
                   "Praesent arcu lacus, laoreet at lacinia quis, rhoncus at lectus. " \
                   // Repeat:
                   "Lorem ipsum dolor sit amet, security consectetur adipiscing elit. Sed vitae ante lobortis," \
                   "condimentum sem et, auctor libero. Aliquam eget mi justo. <br /> <br /> Class aptent taciti sociosqu" \
                   "ad litora Security torquent per conubia nostra, per inceptos himenaeos. Nulla id pretium ante. Nam" \
                   "eu blandit lacus. Proin faucibus in risus quis condimentum. <br /> <br /> Pellentesque a tellus vitae" \
                   "massa tristique cursus. Phasellus fermentum euismod mauris, at ultricies ipsum volutpat eu." \
                   "Praesent arcu lacus, laoreet at lacinia quis, rhoncus at lectus. " \
               };

               that->m_release = &updateReleaseMock;
               that->m_status = UpdateModel::NeedUpdate;
#else
               that->m_status = UpdateModel::UpToDate;
               that->m_release = nullptr;
#endif
            }
            else
            {
                that->m_status = UpdateModel::NeedUpdate;
                that->m_release = update_GetRelease(that->m_update);
            }
        }
        emit that->q_func()->updateStatusChanged();
    });
}

UpdateModel::UpdateModel(qt_intf_t * p_intf)
    : d_ptr(new UpdateModelPrivate(this))
{
    Q_D(UpdateModel);
    d->m_update = update_New( p_intf );
}

UpdateModel::~UpdateModel()
{
    Q_D(UpdateModel);
    update_Delete( d->m_update );
}

void UpdateModel::checkUpdate(bool explicitCheck)
{
    Q_D(UpdateModel);
    if (d->m_status == Checking)
        return;

    if (d->m_explicitCheck != explicitCheck)
    {
        d->m_explicitCheck = explicitCheck;
        emit explicitCheckChanged();
    }

    d->m_release = nullptr;
    d->m_status = Checking;
    emit updateStatusChanged();
    update_Check( d->m_update, UpdateCallback, d );
}

bool UpdateModel::download(QString destDir)
{
    Q_D(UpdateModel);
    if (d->m_status != NeedUpdate)
        return false;
    update_Download( d->m_update, qtu( destDir ) );
    return true;
}

bool UpdateModel::download()
{
    QString dest_dir = QDir::tempPath();
    if (Q_UNLIKELY(dest_dir.isEmpty()))
        return false;

    dest_dir = toNativeSepNoSlash( std::move(dest_dir) ) + DIR_SEP;
    qDebug() << "Downloading to folder:" << qtu( dest_dir );

    return download(dest_dir);
}

void UpdateModel::resetStatus()
{
    Q_D(UpdateModel);
    if (d->m_status == Unchecked)
        return;
    d->m_status = Unchecked;
    emit updateStatusChanged();
}

UpdateModel::Status UpdateModel::updateStatus() const
{
    Q_D(const UpdateModel);
    return d->m_status;
}

int UpdateModel::getMajor() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return d->m_release->i_major;
}
int UpdateModel::getMinor() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return d->m_release->i_minor;
}
int UpdateModel::getRevision() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return d->m_release->i_revision;
}
int UpdateModel::getExtra() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return d->m_release->i_extra;
}
QString UpdateModel::getDescription() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return qfu( d->m_release->psz_desc );
}
QString UpdateModel::getUrl() const
{
    Q_D(const UpdateModel);
    if (!d->m_release) return 0;
    return qfu( d->m_release->psz_desc );
}

double UpdateModel::getProgress() const
{
    // TODO: Stub
    return 0.0;
}

bool UpdateModel::explicitCheck() const
{
    Q_D(const UpdateModel);
    return d->m_explicitCheck;
}

void UpdateModel::resetExplicitCheck()
{
    Q_D(UpdateModel);
    if (!d->m_explicitCheck)
        return;
    d->m_explicitCheck = false;
    emit explicitCheckChanged();
}

/*****************************************************************************
 * UpdateDialog
 *****************************************************************************/

UpdateDialog::UpdateDialog( qt_intf_t *_p_intf ) : QVLCFrame( _p_intf )
{
    /* build Ui */
    ui.setupUi( this );
    ui.updateDialogButtonBox->addButton( new QPushButton( qtr("&Close"), this ),
                                         QDialogButtonBox::RejectRole );
    QPushButton *recheckButton = new QPushButton( qtr("&Recheck version"), this );
    ui.updateDialogButtonBox->addButton( recheckButton, QDialogButtonBox::ActionRole );

    ui.updateNotifyButtonBox->addButton( new QPushButton( qtr("&Yes"), this ),
                                         QDialogButtonBox::AcceptRole );
    ui.updateNotifyButtonBox->addButton( new QPushButton( qtr("&No"), this ),
                                         QDialogButtonBox::RejectRole );

    setWindowTitle( qtr( "VLC media player updates" ) );
    setWindowRole( "vlc-update" );

    BUTTONACT( recheckButton, &UpdateDialog::checkOrDownload );
    connect( ui.updateDialogButtonBox, &QDialogButtonBox::rejected, this, &UpdateDialog::close );

    connect( ui.updateNotifyButtonBox, &QDialogButtonBox::accepted, this, &UpdateDialog::checkOrDownload );
    connect( ui.updateNotifyButtonBox, &QDialogButtonBox::rejected, this, &UpdateDialog::close );

    setMinimumSize( 300, 300 );
    setMaximumSize( 500, 300 );

    restoreWidgetPosition( "Update", maximumSize() );

    m_model = p_intf->p_mi->getUpdateModel();
    connect(m_model, &UpdateModel::updateStatusChanged, this, &UpdateDialog::updateUI);
    /* update status*/
    updateUI();
}

UpdateDialog::~UpdateDialog()
{
    saveWidgetPosition( "Update" );
}

bool UpdateDialog::event(QEvent* event)
{
    assert(event);

    if (Q_LIKELY(m_model))
    {
        switch(event->type())
        {
        case QEvent::Hide:
        case QEvent::Show:
        case QEvent::Close:
        case QEvent::Destroy:
            m_model->resetExplicitCheck();
            break;
        default:
            break;
        }
    }

    return QVLCFrame::event(event);
}

/* Check for updates */
void UpdateDialog::checkOrDownload()
{
    switch (m_model->updateStatus()) {
    case UpdateModel::Unchecked:
    case UpdateModel::UpToDate:
    case UpdateModel::CheckFailed:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateRequestPage );
        m_model->checkUpdate();
        break;
    }
    case UpdateModel::NeedUpdate:
    {
        if (m_model->download())
            toggleVisible();
        break;
    }
    default: // Checking
        break;
    }
}

/* Notify the end of the update_Check */
void UpdateDialog::updateUI( )
{
    switch (m_model->updateStatus()) {
    case UpdateModel::NeedUpdate:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateNotifyPage );
        int extra = m_model->getExtra();
        QString message = QString(
                              qtr( "A new version of VLC (%1.%2.%3%4) is available." ) )
                              .arg( m_model->getMajor() )
                              .arg( m_model->getMinor() )
                              .arg( m_model->getRevision()  )
                              .arg( extra == 0 ? QStringLiteral("") : QStringLiteral(".") + QString::number( extra ) );

        ui.updateNotifyLabel->setText( message );
        message = m_model->getDescription().replace( "\n", "<br/>" );

        /* Try to highlight releases featuring security changes */
        int i_index = message.indexOf( "security", Qt::CaseInsensitive );
        if ( i_index >= 0 )
        {
            message.insert( i_index + 8, "</font>" );
            message.insert( i_index, "<font style=\"color:red\">" );
        }
        ui.updateNotifyTextEdit->setHtml( message );
        break;
    }
    case UpdateModel::UpToDate:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr( "You have the latest version of VLC media player." ) );
        break;
    }
    case UpdateModel::CheckFailed:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr( "An error occurred while checking for updates..." ) );
        break;
    }
    case UpdateModel::Checking:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr( "Checking for updates..." ) );
        break;
    }
    case UpdateModel::Unchecked:
        // do nothing
        break;
    case UpdateModel::Downloading:
        // NOTE: It is not planned to implement this in the legacy dialog.
        //       The new update pane already respects it, we are only
        //       waiting for the core to provide this information.
        break;
    }
}

#endif
