
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/vgui/vgui_deferred.h"
#include "deferred/deferred_verbose.h"

#include "vgui_controls/Button.h"
#include "vgui_controls/RadioButton.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/FileOpenDialog.h"
#include "vgui_controls/QueryBox.h"
#include "vgui_controls/ComboBox.h"

#include "filesystem.h"

#include "tier0/memdbgon.h"

using namespace vgui;

ConVar r_deferred_light_editor_defaultvmfpath( "r_deferred_light_editor_defaultvmfpath", "", FCVAR_ARCHIVE );

namespace
{
	void SetChildBoundsIfFound( Panel *pParent, const char *pszName, int x, int y, int w, int h )
	{
		Panel *pChild = pParent->FindChildByName( pszName );
		if ( pChild != NULL )
			pChild->SetBounds( x, y, w, h );
	}
}

CVGUILightEditor_Controls::CVGUILightEditor_Controls( Panel *pParent )
	: BaseClass( pParent, "LightEditorControls" )
{
	m_pFileVmf = NULL;
	m_bFileDialogForSave = false;
	m_pButtonTimecycleWeather = NULL;

	m_pCBoxDbg = new ComboBox( this, "cboxdbg", 6, false );
	m_pCBoxDbg->AddItem( "None", NULL );
	m_pCBoxDbg->AddItem( "Lighting", NULL );
	m_pCBoxDbg->AddItem( "Depth", NULL );
	m_pCBoxDbg->AddItem( "Normals", NULL );

	LoadControlSettings( "resource/deferred/lighteditor_controls.res" );

	m_pButtonTimecycleWeather = new Button( this, "button_timecycle_weather", "Timecycle and weather editor", this, "edittimecycleweather" );

	SetSize( 220, 446 );
	SetCloseButtonVisible( false );
	SetSizeable( false );
	SetTitle( "Main controls", false );

	OnLevelSpawn();
}

CVGUILightEditor_Controls::~CVGUILightEditor_Controls()
{
}

void CVGUILightEditor_Controls::OnLevelSpawn()
{
	m_pCBoxDbg->ActivateItem( 0 );

	CheckButton *pCheckEnabled = assert_cast<CheckButton*>( FindChildByName( "check_editor_enable" ) );
	if ( pCheckEnabled != NULL )
		pCheckEnabled->SetSelected( false );

	RadioButton *pSelectLight = assert_cast<RadioButton*>( FindChildByName( "action_select" ) );
	if ( pSelectLight != NULL )
		pSelectLight->SetSelected( true );

	CheckButton *pCheckDebugStats = assert_cast<CheckButton*>( FindChildByName( "check_def_stats" ) );
	if ( pCheckDebugStats != NULL )
		pCheckDebugStats->SetSelected( r_deferred_light_stats.GetBool() );
}

void CVGUILightEditor_Controls::PerformLayout()
{
	const int iFrameWide = 220;
	const int iFrameTall = 446;
	const int iControlX = 10;
	const int iControlWide = iFrameWide - iControlX * 2;
	const int iControlTall = 24;

	if ( GetWide() != iFrameWide || GetTall() != iFrameTall )
		SetSize( iFrameWide, iFrameTall );

	BaseClass::PerformLayout();

	SetChildBoundsIfFound( this, "check_editor_enable", iControlX, 25, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Label1", iControlX, 45, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "action_select", iControlX, 65, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "action_add", iControlX, 85, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "action_translate", iControlX, 105, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "action_rotate", iControlX, 125, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Button3", iControlX, 165, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Button4", iControlX, 195, iControlWide, iControlTall );

	if ( m_pButtonTimecycleWeather != NULL )
		m_pButtonTimecycleWeather->SetBounds( iControlX, 225, iControlWide, iControlTall );

	SetChildBoundsIfFound( this, "Label2", iControlX, 265, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Button1", iControlX, 295, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Button2", iControlX, 325, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "Label3", iControlX, 360, iControlWide, iControlTall );
	SetChildBoundsIfFound( this, "check_def_stats", iControlX, 385, iControlWide, iControlTall );
	m_pCBoxDbg->SetBounds( iControlX, 410, iControlWide, iControlTall );

	int sw, sh;
	engine->GetScreenSize( sw, sh );

	int w, h;
	GetSize( w, h );

	DevMsg(1, "CVGUILightEditor_Controls::PerformLayout: %dx%d %dx%d\n", sw, sh, w, h );

	SetPos( 5, sh / 2 - h / 2 );
}

void CVGUILightEditor_Controls::OnRadioButtonChecked( Panel *panel )
{
	RadioButton *pRadioButton = assert_cast< RadioButton* >( panel );

	int iSubPos = pRadioButton->GetSubTabPosition();

	Assert( iSubPos >= 0 && iSubPos < CLightingEditor::EDITORINTERACTION_COUNT );

	DevMsg(1, "CVGUILightEditor_Controls::OnRadioButtonChecked: %d\n", iSubPos );

	const CLightingEditor::EDITORINTERACTION_MODE mode = (CLightingEditor::EDITORINTERACTION_MODE)iSubPos;
	GetLightingEditor()->SetEditorInteractionMode( mode );

	if ( mode != CLightingEditor::EDITORINTERACTION_SELECT )
	{
		CheckButton *pCheckEnabled = assert_cast<CheckButton*>( FindChildByName( "check_editor_enable" ) );
		if ( pCheckEnabled && !pCheckEnabled->IsSelected() )
			pCheckEnabled->SetSelected( true );

		GetLightingEditor()->SetEditorActive( true, true, true );
	}
}

void CVGUILightEditor_Controls::OnCheckButtonChecked( Panel *panel )
{
	if ( !panel )
		return;

	const char *pszName = panel->GetName();
	CheckButton *pCheck = assert_cast< CheckButton* >( panel );
	bool bChecked = pCheck->IsSelected();

	if ( !Q_stricmp( pszName, "check_editor_enable" ) )
	{
		GetLightingEditor()->SetEditorActive( bChecked, true, true );
	}
	else if ( !Q_stricmp( pszName, "check_def_stats" ) )
	{
		r_deferred_light_stats.SetValue( ( bChecked ? 1 : 0 ) );
	}
}

void CVGUILightEditor_Controls::OnTextChanged( Panel *panel )
{
	if ( m_pCBoxDbg == panel )
	{
		CLightingEditor::EDITOR_DBG_MODES dbgMode =
			(CLightingEditor::EDITOR_DBG_MODES)m_pCBoxDbg->GetActiveItem();
		GetLightingEditor()->SetDebugMode( dbgMode );

		RequestFocus();
	}
}

void CVGUILightEditor_Controls::OnCommand( const char *pCmd )
{
	if ( DeferredVerboseLevel() >= 2 )
		DevMsg( "CVGUILightEditor_Controls::OnCommand: %s\n", pCmd );

	if ( !Q_stricmp( pCmd, "loadvmf" ) )
	{
		OnLoadVmf();
	}
	else if ( !Q_stricmp( pCmd, "savevmf" ) )
	{
		if ( GetLightingEditor()->GetCurrentVmfPath() &&
			*GetLightingEditor()->GetCurrentVmfPath() )
		{
			QueryBox *pQueryBox = new QueryBox( "Save VMF",
				VarArgs( "Do you want to overwrite: %s?", GetLightingEditor()->GetCurrentVmfPath() ), this );

			pQueryBox->AddActionSignalTarget( this );
			pQueryBox->SetOKCommand( new KeyValues( "Command", "command", "savevmf_confirm" ) );
			pQueryBox->SetCancelCommand( new KeyValues( "Command", "command", "savevmf_abort" ) );

			pQueryBox->DoModal();
		}
		else
		{
			OpenVmfFileDialogForSave();
		}
	}
	else if ( !Q_stricmp( pCmd, "savevmf_confirm" ) )
	{
		if ( GetLightingEditor()->GetCurrentVmfPath() &&
			*GetLightingEditor()->GetCurrentVmfPath() )
		{
			GetLightingEditor()->SaveToVmf( GetLightingEditor()->GetCurrentVmfPath() );
			PostActionSignal( new KeyValues( "VmfPathChanged" ) );
		}
	}
	else if ( !Q_stricmp( pCmd, "savevmf_abort" ) )
	{
		// no-op
	}
	else if ( !Q_stricmp( pCmd, "autoload_confirm" ) )
	{
		char tmp[MAX_PATH*4];
		BuildCurrentVmfPath( tmp, sizeof(tmp) );

		AssertMsg( g_pFullFileSystem->FileExists( tmp ), "Expected file unavailable (moved, deleted)." );

		LoadVmf( tmp );
	}
	else if ( !Q_stricmp( pCmd, "autoload_abort" ) )
	{
		OpenVmfFileDialog();
	}
	else if ( !Q_stricmp( pCmd, "loadlast_confirm" ) )
	{
		GetLightingEditor()->LoadLastSavedState();
	}
	else if ( !Q_stricmp( pCmd, "loadlast_abort" ) )
	{
		// no-op
	}
	else if ( !Q_stricmp( pCmd, "toggleprops" ) )
	{
		PostActionSignal( new KeyValues( "ToggleEditorProperties" ) );
	}
	else if ( !Q_stricmp( pCmd, "editglobal" ) )
	{
		PostActionSignal( new KeyValues( "EditGlobalLight" ) );
	}
	else if ( !Q_stricmp( pCmd, "edittimecycleweather" ) )
	{
		PostActionSignal( new KeyValues( "EditTimecycleWeather" ) );
	}
	else if ( !Q_stricmp( pCmd, "select" ) ||
		!Q_stricmp( pCmd, "add" ) ||
		!Q_stricmp( pCmd, "translate" ) ||
		!Q_stricmp( pCmd, "rotate" ) )
	{
		CLightingEditor::EDITORINTERACTION_MODE mode = CLightingEditor::EDITORINTERACTION_SELECT;
		if ( !Q_stricmp( pCmd, "add" ) )
			mode = CLightingEditor::EDITORINTERACTION_ADD;
		else if ( !Q_stricmp( pCmd, "translate" ) )
			mode = CLightingEditor::EDITORINTERACTION_TRANSLATE;
		else if ( !Q_stricmp( pCmd, "rotate" ) )
			mode = CLightingEditor::EDITORINTERACTION_ROTATE;

		GetLightingEditor()->SetEditorInteractionMode( mode );
		if ( mode != CLightingEditor::EDITORINTERACTION_SELECT )
			GetLightingEditor()->SetEditorActive( true, true, true );
	}
	else
		BaseClass::OnCommand( pCmd );
}

void CVGUILightEditor_Controls::OnLoadVmf()
{
	if ( GetLightingEditor()->HasLastSavedState() )
	{
		QueryBox *pQueryBox = new QueryBox( "Load",
			"Do you want to revert all editor lights to the last saved state?", this );

		pQueryBox->AddActionSignalTarget( this );
		pQueryBox->SetOKCommand( new KeyValues( "Command", "command", "loadlast_confirm" ) );
		pQueryBox->SetCancelCommand( new KeyValues( "Command", "command", "loadlast_abort" ) );

		pQueryBox->DoModal();
		return;
	}

	char tmp[MAX_PATH*4];
	bool bValidAutoPath = false;

	if ( BuildCurrentVmfPath( tmp, sizeof(tmp) ) )
	{
		bValidAutoPath = g_pFullFileSystem->FileExists( tmp );
	}

	if ( bValidAutoPath )
	{
		QueryBox *pQueryBox = new QueryBox( "Load VMF", VarArgs( "Do you want to load: %s?", tmp ), this );

		pQueryBox->AddActionSignalTarget( this );
		pQueryBox->SetOKCommand( new KeyValues( "Command", "command", "autoload_confirm" ) );
		pQueryBox->SetCancelCommand( new KeyValues( "Command", "command", "autoload_abort" ) );

		pQueryBox->DoModal();
	}
	else
	{
		OpenVmfFileDialog();
	}
}

void CVGUILightEditor_Controls::BuildVmfPath( char *pszOut, int maxlen, bool bMakeRelative )
{
	char tmp[MAX_PATH*4];
	char tmp2[MAX_PATH*4];

	if( Q_strcmp( r_deferred_light_editor_defaultvmfpath.GetString(), "" ) != 0 &&
		g_pFullFileSystem->IsDirectory( r_deferred_light_editor_defaultvmfpath.GetString() ) )
	{
		Q_strcpy( tmp, r_deferred_light_editor_defaultvmfpath.GetString() );
	}
	else
	{
		Q_snprintf( tmp, sizeof(tmp), "%s/mapsrc", engine->GetGameDirectory() );
		Q_FixSlashes( tmp );
	}

	if ( bMakeRelative && g_pFullFileSystem->FullPathToRelativePath( tmp, tmp2, sizeof(tmp2) ) )
		Q_strcpy( tmp, tmp2 );
	else if ( !bMakeRelative && g_pFullFileSystem->RelativePathToFullPath( tmp, "MOD", tmp2, sizeof(tmp2) ) )
		Q_strcpy( tmp, tmp2 );

	Q_snprintf( pszOut, maxlen, "%s", tmp );
}

bool CVGUILightEditor_Controls::BuildCurrentVmfPath( char *pszOut, int maxlen )
{
	const char *pszLevelname = engine->GetLevelName();

	if ( !pszLevelname || !*pszLevelname )
		return false;

	char szBaseLevelName[MAX_PATH];
	Q_FileBase( pszLevelname, szBaseLevelName, sizeof(szBaseLevelName) );

	char tmp[MAX_PATH*4];
	char tmp2[MAX_PATH*4];

	BuildVmfPath( tmp2, sizeof(tmp2) );
	Q_snprintf( tmp, sizeof(tmp), "%s/%s.vmf", tmp2, szBaseLevelName );
	Q_FixSlashes( tmp );

	if ( g_pFullFileSystem->FullPathToRelativePath( tmp, tmp2, sizeof(tmp2) ) )
		Q_strcpy( tmp, tmp2 );

	Q_snprintf( pszOut, maxlen, "%s", tmp );
	return true;
}

void CVGUILightEditor_Controls::OpenVmfFileDialog()
{
	m_bFileDialogForSave = false;

	char szVmfPath[MAX_PATH*4];
	BuildVmfPath( szVmfPath, sizeof( szVmfPath ), false );

	if ( m_pFileVmf != NULL )
		m_pFileVmf->DeletePanel();

	m_pFileVmf = new FileOpenDialog( this, "Open vmf", FOD_OPEN );

	m_pFileVmf->SetDeleteSelfOnClose( false );
	m_pFileVmf->AddFilter( "*.vmf", "*.vmf", true );
	m_pFileVmf->AddActionSignalTarget( this );

	m_pFileVmf->SetStartDirectoryContext( "VMFContext", szVmfPath );
	m_pFileVmf->DoModal( false );
	m_pFileVmf->Activate();
}

void CVGUILightEditor_Controls::OpenVmfFileDialogForSave()
{
	m_bFileDialogForSave = true;

	char szVmfPath[MAX_PATH*4];
	BuildVmfPath( szVmfPath, sizeof( szVmfPath ), false );

	if ( m_pFileVmf != NULL )
		m_pFileVmf->DeletePanel();

	m_pFileVmf = new FileOpenDialog( this, "Save vmf", FOD_SAVE );

	m_pFileVmf->SetDeleteSelfOnClose( false );
	m_pFileVmf->AddFilter( "*.vmf", "*.vmf", true );
	m_pFileVmf->AddActionSignalTarget( this );

	m_pFileVmf->SetStartDirectoryContext( "VMFContext", szVmfPath );
	m_pFileVmf->DoModal( false );
	m_pFileVmf->Activate();
}

void CVGUILightEditor_Controls::OnFileSelected( KeyValues *pKV )
{
	const char *pszFullpath = pKV->GetString( "fullpath" );

	if ( m_bFileDialogForSave )
	{
		m_bFileDialogForSave = false;
		GetLightingEditor()->SaveToVmf( pszFullpath );
		PostActionSignal( new KeyValues( "VmfPathChanged" ) );
	}
	else
	{
		LoadVmf( pszFullpath );
	}
}

void CVGUILightEditor_Controls::LoadVmf( const char *pszPath )
{
	GetLightingEditor()->LoadVmf( pszPath );
	PostActionSignal( new KeyValues( "VmfPathChanged" ) );
}
