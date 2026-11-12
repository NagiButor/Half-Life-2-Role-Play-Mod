//========= HL2RPM ============================================================//
//
// The arms of the stock HL2 view models in the suit the player wears.
//
// The view models of HL2 (pistol, SMG, crowbar, gravity gun...) carry their own arms - the gloves
// and sleeves of the H.E.V. - and most of them have no arm skeleton a hand model could be
// bone-merged onto (the right arm of the SMG, both arms of the .357, the crossbow, the RPG... are
// bound to the weapon's bones). The melee weapons of the mod draw the suit's own hands
// (Mapbase's hand_viewmodel, "uses_hands" - hl2rpm_char.cpp sets the model); for the others the one
// material of their arms, models/weapons/v_hand/v_hand_sheet, gets the textures painted for the
// suit (hl2rpm_extras/tools/make_vm_hands.py -> materials/models/weapons/v_hand/hl2rpm/
// v_hand_sheet_<name> and _normal): the colours of the suit's own hands on the creases and seams
// of the original. Which one: RPMItems::SuitViewHands (the suit worn, "suit_vmhands" of a suit in
// the database, else by the theme of its HUD); no suit - a citizen's sleeves and bare hands.
//
// The textures are set into the material's variables (the deferred shaders rebuild their cached
// commands when a variable changes); a reload of the materials puts the originals back - so it is
// checked every frame (a pointer compare).
//
//   cl_rpm_vmhands 0 - the stock H.E.V. arms always    cl_rpm_vmhands_debug 1 - print the changes
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_items.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const char *RPMMenu_WornSuit();
extern int RPMUI_SuitTheme();

static ConVar cl_rpm_vmhands( "cl_rpm_vmhands", "1", FCVAR_ARCHIVE, "HL2RPM: the arms of the stock view models in the colours of the suit worn" );
static ConVar cl_rpm_vmhands_debug( "cl_rpm_vmhands_debug", "0", 0, "HL2RPM: print which arms the stock view models get" );
static ConVar cl_rpm_vmhands_force( "cl_rpm_vmhands_force", "", 0, "HL2RPM: show these arms whatever is worn (citizen, hev, pcv, cp, soldier, prison, elite, worker, hazmat)" );

#define VMHANDS_MATERIAL	"models/weapons/v_hand/v_hand_sheet"

class CRPMViewModelHands : public CAutoGameSystemPerFrame
{
public:
	CRPMViewModelHands() : CAutoGameSystemPerFrame( "CRPMViewModelHands" ) {}

	virtual void LevelInitPostEntity()
	{
		m_strWant.Clear();
	}

	virtual void Update( float frametime )
	{
		if ( !engine->IsInGame() )
			return;
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer )
			return;

		const char *name = "hev";
		if ( cl_rpm_vmhands.GetBool() )
		{
			const char *suit = pPlayer->IsSuitEquipped() ? RPMMenu_WornSuit() : "";
			name = cl_rpm_vmhands_force.GetString()[0] ? cl_rpm_vmhands_force.GetString() : RPMItems::SuitViewHands( suit, RPMUI_SuitTheme() );
		}

		IMaterial *pMat = materials->FindMaterial( VMHANDS_MATERIAL, TEXTURE_GROUP_MODEL, false );
		if ( !pMat || pMat->IsErrorMaterial() )
			return;

		// the textures of that suit (the H.E.V. - the originals); found once per change
		if ( Q_stricmp( m_strWant.Get(), name ) )
		{
			m_strWant = name;
			m_pBase = m_pBump = NULL;
			if ( Q_stricmp( name, "hev" ) )
			{
				ITexture *b = materials->FindTexture( CFmtStr( "models/weapons/v_hand/hl2rpm/v_hand_sheet_%s", name ), TEXTURE_GROUP_MODEL, false );
				ITexture *n = materials->FindTexture( CFmtStr( "models/weapons/v_hand/hl2rpm/v_hand_sheet_%s_normal", name ), TEXTURE_GROUP_MODEL, false );
				m_pBase = ( b && !b->IsError() ) ? b : NULL;
				m_pBump = ( n && !n->IsError() ) ? n : NULL;
			}
			if ( !m_pBase )
				m_pBase = materials->FindTexture( "models/weapons/v_hand/v_hand_sheet", TEXTURE_GROUP_MODEL, false );
			if ( !m_pBump )
				m_pBump = materials->FindTexture( "models/weapons/v_hand/v_hand_sheet_normal", TEXTURE_GROUP_MODEL, false );
			if ( cl_rpm_vmhands_debug.GetBool() )
				Msg( "[vmhands] %s: %s / %s\n", name, m_pBase ? m_pBase->GetName() : "-", m_pBump ? m_pBump->GetName() : "-" );
		}

		SetTexture( pMat, "$basetexture", m_pBase );
		SetTexture( pMat, "$bumpmap", m_pBump );
	}

private:
	static void SetTexture( IMaterial *pMat, const char *var, ITexture *pTex )
	{
		if ( !pTex || pTex->IsError() )
			return;
		bool found = false;
		IMaterialVar *v = pMat->FindVar( var, &found, false );
		if ( !found || !v )
			return;
		if ( !v->IsTexture() || v->GetTextureValue() != pTex )
		{
			v->SetTextureValue( pTex );
			if ( cl_rpm_vmhands_debug.GetBool() )
				Msg( "[vmhands] %s %s <- %s\n", pMat->GetName(), var, pTex->GetName() );
		}
	}

	CUtlString m_strWant;
	ITexture *m_pBase = NULL;
	ITexture *m_pBump = NULL;
};

static CRPMViewModelHands g_RPMViewModelHands;

CON_COMMAND( cl_rpm_vmhands_dump, "HL2RPM: the material of the stock view models' arms and what its textures are now" )
{
	IMaterial *pMat = materials->FindMaterial( VMHANDS_MATERIAL, TEXTURE_GROUP_MODEL, false );
	if ( !pMat || pMat->IsErrorMaterial() )
	{
		Msg( "[vmhands] no material %s\n", VMHANDS_MATERIAL );
		return;
	}
	Msg( "[vmhands] %s, shader %s\n", pMat->GetName(), pMat->GetShaderName() );
	static const char *vars[] = { "$basetexture", "$bumpmap", "$hdtexture" };
	for ( int i = 0; i < ARRAYSIZE( vars ); i++ )
	{
		bool found = false;
		IMaterialVar *v = pMat->FindVar( vars[i], &found, false );
		Msg( "    %s: found %d type %d texture %s\n", vars[i], found, v ? (int)v->GetType() : -1,
			( v && v->IsTexture() && v->GetTextureValue() ) ? v->GetTextureValue()->GetName() : "-" );
	}
}
