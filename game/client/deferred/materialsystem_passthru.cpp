//====== Copyright � Sandern Corporation, All rights reserved. ===========//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "igamesystem.h"
#include "materialsystem_passthru.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "icommandline.h"

#include "deferred/deferred_shared_common.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static int matCount = 0;
CON_COMMAND(print_num_replaced_mats, "")
{
	ConColorMsg( COLOR_GREEN, "%d replaced materials\n", matCount );
}

// HL2RPM dev: list loaded materials whose name contains a substring
CON_COMMAND( r_deferred_list_materials, "Dev: list loaded materials containing <substring> (shader, precached, references)" )
{
	const char *pszFilter = ( args.ArgC() > 1 ) ? args[1] : "";
	int nShown = 0, nTotal = 0;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMat = materials->GetMaterial( h );
		nTotal++;
		if ( !pMat || ( *pszFilter && !V_stristr( pMat->GetName(), pszFilter ) ) )
			continue;
		bool bFound = false;
		IMaterialVar *pTranslucent = pMat->FindVar( "$translucent", &bFound, false );
		IMaterialVar *pAlphaTest = pMat->FindVar( "$alphatest", &bFound, false );
		Msg( "[matlist] %s | %s | group %s | precached %d | translucent %d (flag %d, $translucent %d) alphatest %d ($alphatest %d) alpha %.2f\n",
			pMat->GetName(), pMat->GetShaderName() ? pMat->GetShaderName() : "-",
			pMat->GetTextureGroupName() ? pMat->GetTextureGroupName() : "-", pMat->IsPrecached() ? 1 : 0,
			pMat->IsTranslucent() ? 1 : 0, pMat->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT ) ? 1 : 0,
			pTranslucent ? pTranslucent->GetIntValue() : -1, pMat->IsAlphaTested() ? 1 : 0,
			pAlphaTest ? pAlphaTest->GetIntValue() : -1, pMat->GetAlphaModulation() );
		nShown++;
	}
	Msg( "[matlist] %d of %d materials\n", nShown, nTotal );
}

//-----------------------------------------------------------------------------
// List of materials that should be replaced
//-----------------------------------------------------------------------------
static const char * const pszShaderReplaceDict[][2] = {
	{ "vertexlitgeneric",				"DEFERRED_MODEL" },
	{ "lightmappedgeneric",			"DEFERRED_BRUSH" },
	{ "worldvertextransition",	"DEFERRED_BRUSH" },
	{ "multiblend",					"DEFERRED_BRUSH" },
	{ "worldtwotextureblend",	"DEFERRED_BRUSH" },
	{ "lightmapped_4wayblend",	"DEFERRED_BRUSH" },
	{ "eyes",						"DEFERRED_EYES" },
	{ "teeth",						"DEFERRED_TEETH" },
	//{ "decalmodulate",					"DEFERRED_DECALMODULATE" }, //doesn't work
};

//-----------------------------------------------------------------------------
// HL2RPM: vegetation. Grass and leaf cards are often alpha *blended* ($translucent,
// UnlitGeneric or VertexLitGeneric): as translucents they fell back to the forward
// shaders, lit by the baked lighting or not lit at all - grass and distant tree cards
// glowed at night. As alpha-tested deferred models they get the real sun, shadows,
// lamps and time of day.
//-----------------------------------------------------------------------------
static bool IsVegetationMaterial( const char *pszName )
{
	if ( !pszName || V_strnicmp( pszName, "models", 6 ) != 0 )
		return false;

	static const char *const s_pszKeys[] = {
		"foliage", "grass", "bush", "shrub", "fern", "leaf", "leaves", "hedge", "ivy",
		"flower", "bramble", "weed", "vine", "branch", "/tree", "tree_", "_tree", "trees", "plant",
	};
	for ( const char *pszKey : s_pszKeys )
	{
		if ( V_stristr( pszName, pszKey ) )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// HL2RPM: hair cards - eyelashes, strands (Alyx's "hairbits"). Blended, they were
// composited with the lighting of the opaque surface *behind* them (the light buffer
// pixel): the lower lashes over a lit eyeball glowed orange and the eyelids looked
// see-through. Alpha-tested they go into the G-buffer and get their own lighting.
//-----------------------------------------------------------------------------
static bool IsHairMaterial( const char *pszName )
{
	if ( !pszName || V_strnicmp( pszName, "models", 6 ) != 0 )
		return false;

	for ( const char *p = V_stristr( pszName, "hair" ); p; p = V_stristr( p + 1, "hair" ) )
	{
		if ( p == pszName || ( p[-1] != 'c' && p[-1] != 'C' ) )	// not "chair"
			return true;
	}

	static const char *const s_pszKeys[] = { "eyelash", "lashes", "eyebrow", "beard", "mustache", "moustache" };
	for ( const char *pszKey : s_pszKeys )
	{
		if ( V_stristr( pszName, pszKey ) )
			return true;
	}
	return false;
}

// Copied from cdeferred_manager_client.cpp
static void ShaderReplaceReplMat( const char *szNewShadername, IMaterial *pMat, bool bVegetation = false, bool bHair = false )
{
	const char *pszOldShadername = pMat->GetShaderName();
	const char *pszMatname = pMat->GetName();

	KeyValuesAD msg( szNewShadername );

	const int nParams = pMat->ShaderParamCount();
	IMaterialVar **pParams = pMat->GetShaderParams();

	char str[ 512 ];

	for ( int i = 0; i < nParams; ++i )
	{
		IMaterialVar *pVar = pParams[ i ];
		const char *pVarName = pVar->GetName();

		if (!V_stricmp("$flags", pVarName) ||
			!V_stricmp("$flags_defined", pVarName) ||
			!V_stricmp("$flags2", pVarName) ||
			!V_stricmp("$flags_defined2", pVarName) )
			continue;

		switch ( pVar->GetType() )
		{
			case MATERIAL_VAR_TYPE_FLOAT:
				msg->SetFloat( pVarName, pVar->GetFloatValue() );
				break;

			case MATERIAL_VAR_TYPE_INT:
				msg->SetInt( pVarName, pVar->GetIntValue() );
				break;

			case MATERIAL_VAR_TYPE_STRING:
				msg->SetString( pVarName, pVar->GetStringValue() );
				break;

			case MATERIAL_VAR_TYPE_FOURCC:
				//Assert( 0 ); // JDTODO
				break;

			case MATERIAL_VAR_TYPE_VECTOR:
			{
				const float *pVal = pVar->GetVecValue();
				int dim = pVar->VectorSize();
				switch ( dim )
				{
				case 1:
					V_sprintf_safe( str, "[%f]", pVal[ 0 ] );
					break;
				case 2:
					V_sprintf_safe( str, "[%f %f]", pVal[ 0 ], pVal[ 1 ] );
					break;
				case 3:
					V_sprintf_safe( str, "[%f %f %f]", pVal[ 0 ], pVal[ 1 ], pVal[ 2 ] );
					break;
				case 4:
					V_sprintf_safe( str, "[%f %f %f %f]", pVal[ 0 ], pVal[ 1 ], pVal[ 2 ], pVal[ 3 ] );
					break;
				default:
					Assert( 0 );
					*str = 0;
				}
				msg->SetString( pVarName, str );
			}
				break;

			case MATERIAL_VAR_TYPE_MATRIX:
			{
				const float *pVal = pVar->GetMatrixValue().Base();
				V_sprintf_safe( str,
					"[%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f]",
					pVal[ 0 ],  pVal[ 1 ],  pVal[ 2 ],  pVal[ 3 ],
					pVal[ 4 ],  pVal[ 5 ],  pVal[ 6 ],  pVal[ 7 ],
					pVal[ 8 ],  pVal[ 9 ],  pVal[ 10 ], pVal[ 11 ],
					pVal[ 12 ], pVal[ 13 ], pVal[ 14 ], pVal[ 15 ] );
				msg->SetString( pVarName, str );
			}
			break;

			case MATERIAL_VAR_TYPE_TEXTURE:
				msg->SetString( pVarName, pVar->GetTextureValue()->GetName() );
				break;

			case MATERIAL_VAR_TYPE_MATERIAL:
				msg->SetString( pVarName, pVar->GetMaterialValue()->GetName() );
				break;
		}
	}

	const bool bAlphaBlending = pMat->IsTranslucent() || pMat->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT );
	const bool bAlphaTesting = pMat->IsAlphaTested() || pMat->GetMaterialVarFlag( MATERIAL_VAR_ALPHATEST );
	const bool bSelfillum = pMat->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM );
	const bool bDecal = pszOldShadername != NULL && Q_stristr( pszOldShadername,"decal" ) != NULL ||
		pszMatname != NULL && Q_stristr( pszMatname, "decal" ) != NULL ||
		pMat->GetMaterialVarFlag( MATERIAL_VAR_DECAL );

	if ( bDecal )
	{
		msg->SetInt( "$decal", 1 );
	}

	if ( bAlphaTesting )
	{
		msg->SetInt( "$alphatest", 1 );
	}
	else if ( bAlphaBlending && ( bVegetation || bHair ) && !bDecal )
	{
		// blended grass/leaf card or hair card -> alpha test (see IsVegetationMaterial, IsHairMaterial)
		msg->SetInt( "$alphatest", 1 );
		if ( !msg->FindKey( "$alphatestreference" ) )
			msg->SetFloat( "$alphatestreference", 0.4f );
	}
	else if ( bAlphaBlending )
	{
		msg->SetInt( "$translucent", 1 );

		// HL2RPM: the translucent top of the Citadel (Combine_Citadel001b, its cables): a blended
		// model material falls back to VertexLitGeneric, lit by the light baked into the skybox -
		// a bluish strip by day next to the deferred-lit opaque part, and the time-of-day scale
		// of the forward materials turned it into a black band at night. Composited by the
		// deferred model shader instead, it is lit like the sky behind it.
		if ( pszMatname && V_stristr( pszMatname, "props_combine/" ) && V_stristr( pszMatname, "citadel" ) )
			msg->SetInt( "$deferredtranslucent", 1 );
	}

	if ( pMat->IsTwoSided() )
	{
		msg->SetInt( "$nocull", 1 );
	}

	// HL2RPM: the flags aren't copied above - half-lambert got lost, so characters and
	// props made for it were lit much harsher than designed. Vegetation cards wrap the
	// light as well (their normals are arbitrary).
	if ( pMat->GetMaterialVarFlag( MATERIAL_VAR_HALFLAMBERT ) || bVegetation )
	{
		msg->SetInt( "$halflambert", 1 );
	}

	if ( bSelfillum )
	{
		msg->SetInt( "$selfillum", 1 );
	}

	// HL2RPM: $nofog was lost with the flags as well. The skybox Citadel's materials are
	// $nofog (only its translucent top fades into the sky): the composite fogged the whole
	// tower into a flat sky-colored silhouette.
	if ( pMat->GetMaterialVarFlag( MATERIAL_VAR_NOFOG ) )
	{
		msg->SetInt( "$nofog", 1 );
	}

	pMat->SetShaderAndParams( msg );
	pMat->RefreshPreservingMaterialVars();
}

IMaterial* CDeferredMaterialSystem::FindProceduralMaterial( const char* pMaterialName, const char* pTextureGroupName,
                                                        KeyValues* pVMTKeyValues )
{
	const char* pShaderName = pVMTKeyValues->GetName();
	for ( const char* const* row : pszShaderReplaceDict )
	{
		if ( FStrEq( pShaderName, row[0] ) )
		{
			pVMTKeyValues->SetName( row[1] );
			matCount++;
			break;
		}
	}
	return BaseClass::FindProceduralMaterial( pMaterialName, pTextureGroupName, pVMTKeyValues );
}

IMaterial* CDeferredMaterialSystem::CreateMaterial( const char* pMaterialName, KeyValues* pVMTKeyValues )
{
	const char* pShaderName = pVMTKeyValues->GetName();
	for ( const char* const* row : pszShaderReplaceDict )
	{
		if ( FStrEq( pShaderName, row[0] ) )
		{
			pVMTKeyValues->SetName( row[1] );
			matCount++;
			break;
		}
	}
	return BaseClass::CreateMaterial( pMaterialName, pVMTKeyValues );
}

static ConVar r_deferred_material_replace( "r_deferred_material_replace", "1", 0, "Dev: replace stock shaders by the deferred ones (0 = off, for crash hunting)" );
// (read at map load, when the materials are replaced)
ConVar r_deferred_detail_sprites( "r_deferred_detail_sprites", "1", 0, "Detail sprites (grass) lit by the deferred lights: sun shadows, lamps (takes effect on map load)" );

// Replaces the shader of a material that still has a stock one; true if it did.
static bool ReplaceMaterialShader( IMaterial *pMat )
{
	if ( !pMat || pMat->IsErrorMaterial() || !r_deferred_material_replace.GetBool() )
		return false;

	//FIXME: subrect decals don't work at all
	if ( V_stristr( pMat->GetName(), "_subrect" ) )
	{
		DevMsg( 2, "Decal %s skipped due to subrect issues\n", pMat->GetName() );
		return false;
	}

	const char* pShaderName = pMat->GetShaderName();

	// HL2RPM: already replaced. "eyes" and "teeth" are substrings of DEFERRED_EYES and
	// DEFERRED_TEETH, so every FindMaterial of an NPC's eye/mouth material rebuilt its
	// shader again (SetShaderAndParams + Refresh destroy its variables) while models were
	// using it - studiorender writes the eye parameters into those variables every frame.
	// Heap corruption: crashes on map change, on quit, in mat_reloadallmaterials.
	if ( !pShaderName || V_strnicmp( pShaderName, "DEFERRED_", 9 ) == 0 )
		return false;

	const bool bVegetation = IsVegetationMaterial( pMat->GetName() );

	// unlit grass / tree LOD cards (HL2's grass3, tree_*_cards) become lit deferred models
	if ( bVegetation && V_stristr( pShaderName, "UnlitGeneric" ) )
	{
		ShaderReplaceReplMat( "DEFERRED_MODEL", pMat, true );
		matCount++;
		return true;
	}

	// HL2RPM: the sprites of the map's detail props (grass tufts, "detail/detailsprites"):
	// blended UnlitGeneric lit by a color baked per sprite - the same in the shade of a wall as
	// in the sun. As alpha-tested deferred brushes they go into the G-buffer too
	// (CGBufferView draws them, their vertices carry an up normal) and get the real light:
	// the sun with its shadows, the sky, lamps.
	if ( !V_strnicmp( pMat->GetName(), "detail/", 7 ) && V_stristr( pShaderName, "UnlitGeneric" ) && r_deferred_detail_sprites.GetBool() )
	{
		ShaderReplaceReplMat( "DEFERRED_BRUSH", pMat, true );
		matCount++;
		return true;
	}

	for ( const char* const* row : pszShaderReplaceDict )
	{
		if ( V_stristr( pShaderName, row[0] ) )
		{
			const bool bModel = !V_stricmp( row[1], "DEFERRED_MODEL" );
			ShaderReplaceReplMat( row[1], pMat, bVegetation && bModel, bModel && IsHairMaterial( pMat->GetName() ) );
			matCount++;
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// HL2RPM: a reload of the materials (mat_reloadallmaterials, a changed material config
// applied on map load) brings them back from their .vmt with the stock shader, and world
// materials aren't looked up again - they are swept after the material system restored
// its resources. No replacing while it releases and restores them (the model cache
// rebuilds the meshes of the loaded models then): those are swept as well.
//-----------------------------------------------------------------------------
static bool s_bMaterialsRestoring = false;
static bool s_bReplaceSweepPending = false;

static void DeferredMaterials_OnRelease()
{
	DevMsg( 2, "[deferred] material system releases its resources\n" );
	s_bMaterialsRestoring = true;
	s_bReplaceSweepPending = true;
}

static void DeferredMaterials_OnRestore( int nChangeFlags )
{
	DevMsg( 2, "[deferred] material system restores its resources (flags %d)\n", nChangeFlags );
	// other restore functions (the model cache) may come after this one: the flag is cleared
	// at the next level init or frame
	s_bMaterialsRestoring = true;
	s_bReplaceSweepPending = true;
}

static void DeferredMaterials_ReplaceSweep( const char *pszWhen )
{
	s_bReplaceSweepPending = false;
	if ( CommandLine() && CommandLine()->FindParm( "-nodeferred" ) != 0 )
		return;

	int nReplaced = 0;
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMat = materials->GetMaterial( h );
		if ( pMat && pMat->IsPrecached() && ReplaceMaterialShader( pMat ) )
			nReplaced++;
	}
	if ( nReplaced > 0 )
		DevMsg( "[deferred] %d materials back with a stock shader replaced (%s)\n", nReplaced, pszWhen );
}

void DeferredMaterials_InstallCallbacks()
{
	materials->AddReleaseFunc( DeferredMaterials_OnRelease );
	materials->AddRestoreFunc( DeferredMaterials_OnRestore );
}

void DeferredMaterials_RemoveCallbacks()
{
	materials->RemoveReleaseFunc( DeferredMaterials_OnRelease );
	materials->RemoveRestoreFunc( DeferredMaterials_OnRestore );
}

static class CDeferredMaterialSweep : public CAutoGameSystemPerFrame
{
public:
	CDeferredMaterialSweep() : CAutoGameSystemPerFrame( "CDeferredMaterialSweep" ) {}

	virtual void LevelInitPreEntity()
	{
		// the new map's models are loaded now: their materials are replaced as found
		s_bMaterialsRestoring = false;
	}

	virtual void LevelInitPostEntity()
	{
		DeferredMaterials_ReplaceSweep( "level init" );
	}

	virtual void Update( float frametime )
	{
		if ( s_bMaterialsRestoring )
		{
			s_bMaterialsRestoring = false;
			s_bReplaceSweepPending = true;
		}
		if ( s_bReplaceSweepPending )
			DeferredMaterials_ReplaceSweep( "after a resource restore" );
	}
} s_DeferredMaterialSweep;

IMaterial* CDeferredMaterialSystem::ReplaceMaterialInternal( IMaterial* pMat ) const
{
	if ( !pMat || pMat->IsErrorMaterial() )
		return pMat;

	if (CommandLine() && CommandLine()->FindParm("-nodeferred") != 0) {
		return pMat;
	}

	if ( s_bMaterialsRestoring )
	{
		s_bReplaceSweepPending = true;
		return pMat;
	}

	ReplaceMaterialShader( pMat );
	return pMat;
}
