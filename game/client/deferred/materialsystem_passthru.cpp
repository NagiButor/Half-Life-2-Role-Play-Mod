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
#include "deferred/deferred_gi.h"
#include "deferred/deferred_taa.h"
#include "deferred/deferred_postfx.h"
#include "deferred/deferred_pom.h"

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
		// (asking for the shader or a variable loads the material: one left over from a map
		// played before has no file any more - "error loading vmt file")
		if ( !pMat->IsPrecached() )
		{
			Msg( "[matlist] %s | not loaded\n", pMat->GetName() );
			nShown++;
			continue;
		}
		bool bFound = false;
		IMaterialVar *pTranslucent = pMat->FindVar( "$translucent", &bFound, false );
		IMaterialVar *pAlphaTest = pMat->FindVar( "$alphatest", &bFound, false );
		Msg( "[matlist] %s | %s | group %s | precached %d | translucent %d (flag %d, $translucent %d) alphatest %d ($alphatest %d) alpha %.2f\n",
			pMat->GetName(), pMat->GetShaderName() ? pMat->GetShaderName() : "-",
			pMat->GetTextureGroupName() ? pMat->GetTextureGroupName() : "-", pMat->IsPrecached() ? 1 : 0,
			pMat->IsTranslucent() ? 1 : 0, pMat->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT ) ? 1 : 0,
			pTranslucent ? pTranslucent->GetIntValue() : -1, pMat->IsAlphaTested() ? 1 : 0,
			pAlphaTest ? pAlphaTest->GetIntValue() : -1, pMat->GetAlphaModulation() );
		// (what the renderer gave it: wind sway, light through it, two-sided light, relief)
		IMaterialVar *pSway = pMat->FindVar( "$treesway", &bFound, false );
		IMaterialVar *pTransmission = pMat->FindVar( "$pbr_transmission", &bFound, false );
		IMaterialVar *pLitface = pMat->FindVar( "$litface", &bFound, false );
		IMaterialVar *pRelief = pMat->FindVar( "$pom_heightmap", &bFound, false );
		if ( pSway || pTransmission || pLitface || pRelief )
		{
			Msg( "[matlist]     sway %d, transmission %.2f, litface %d, relief %s\n", pSway ? pSway->GetIntValue() : 0,
				pTransmission ? pTransmission->GetFloatValue() : 0.0f, pLitface ? pLitface->GetIntValue() : 0,
				( pRelief && pRelief->IsDefined() ) ? pRelief->GetStringValue() : "-" );
		}
		// (glass in the texture: its gloss map)
		IMaterialVar *pGlassMask = pMat->FindVar( "$pbr_glassmask", &bFound, false );
		IMaterialVar *pPhongMap = pMat->FindVar( "$phong_map", &bFound, false );
		if ( pGlassMask && pGlassMask->GetIntValue() )
			Msg( "[matlist]     glass in the texture, gloss map %s\n", ( pPhongMap && pPhongMap->IsDefined() && pPhongMap->IsTexture() ) ? pPhongMap->GetTextureValue()->GetName() : "-" );
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

//-----------------------------------------------------------------------------
// HL2RPM: blended panes of glass - windows, doors, the glass of vehicles and lamps
// (LightmappedGeneric / VertexLitGeneric with $translucent, glass by their name or their
// $surfaceprop). As translucents they fell back to the forward shaders: lit by the lightmap
// of one time of day, mirroring the engine's default cubemap on maps without built ones -
// half opaque grey, greenish, as bright at night as by day. DEFERRED_GLASS draws them as
// glass (stdshaders/deferred_glass.cpp).
//-----------------------------------------------------------------------------
static bool IsGlassMaterial( IMaterial *pMat, const char *pszShader )
{
	static ConVarRef r_deferred_glass( "r_deferred_glass" );
	if ( !r_deferred_glass.IsValid() || !r_deferred_glass.GetBool() )
		return false;
	if ( !V_stristr( pszShader, "LightmappedGeneric" ) && !V_stristr( pszShader, "VertexLitGeneric" ) )
		return false;
	if ( !pMat->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT ) || pMat->GetMaterialVarFlag( MATERIAL_VAR_ADDITIVE )
		|| pMat->GetMaterialVarFlag( MATERIAL_VAR_DECAL ) || pMat->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM )
		|| pMat->GetMaterialVarFlag( MATERIAL_VAR_ALPHATEST ) )
		return false;

	const char *pszName = pMat->GetName();
	static const char *const s_pszNot[] = { "effects/", "sprites/", "particle", "decals/", "overlays/", "glow", "beam", "water" };
	for ( const char *pszKey : s_pszNot )
	{
		if ( V_stristr( pszName, pszKey ) )
			return false;
	}
	if ( V_stristr( pszName, "glass" ) || V_stristr( pszName, "window" ) )
		return true;

	bool bFound = false;
	IMaterialVar *pSurfaceProp = pMat->FindVar( "$surfaceprop", &bFound, false );
	return bFound && pSurfaceProp && V_stristr( pSurfaceProp->GetStringValue(), "glass" ) != NULL;
}

//-----------------------------------------------------------------------------
// HL2RPM: what moves in the wind (stdshaders/hl2rpm_sway_fxc.h) - a narrower list than the
// vegetation above: a rigid prop taken for a plant by its name ("powerplant") only gets other
// lighting there, here it would wave.
//-----------------------------------------------------------------------------
static bool IsSwayMaterial( const char *pszName )
{
	if ( !pszName || V_strnicmp( pszName, "models", 6 ) != 0 )
		return false;
	static const char *const s_pszKeys[] = {
		"foliage", "grass", "bush", "shrub", "fern", "leaf", "leaves", "hedge", "ivy",
		"weed", "/tree", "tree_", "_tree", "trees",
	};
	// (what lies in the folders of the plants and is no plant)
	static const char *const s_pszRigid[] = { "driftwood", "rock", "stump", "log", "planter", "pot" };
	for ( const char *pszKey : s_pszRigid )
	{
		if ( V_stristr( pszName, pszKey ) )
			return false;
	}
	for ( const char *pszKey : s_pszKeys )
	{
		if ( V_stristr( pszName, pszKey ) )
			return true;
	}
	return false;
}

// HL2RPM: thin cloth the light shines through (two-sided model materials)
static bool IsClothMaterial( const char *pszName )
{
	if ( !pszName )
		return false;
	static const char *const s_pszKeys[] = { "curtain", "cloth", "fabric", "tarp", "awning", "banner", "flag", "canvas", "lampshade" };
	for ( const char *pszKey : s_pszKeys )
	{
		if ( V_stristr( pszName, pszKey ) )
			return true;
	}
	return false;
}

static ConVar r_deferred_pom_auto( "r_deferred_pom_auto", "1", 0,
	"Parallax occlusion mapping: brush materials get a height map made of their bump map (takes effect as materials load)" );
static ConVar r_deferred_pom_texture( "r_deferred_pom_texture", "1", FCVAR_ARCHIVE,
	"Brick, stone and tile materials without a bump map get a relief and a bump map made of their texture (takes effect as materials load)" );

//-----------------------------------------------------------------------------
// HL2RPM: what a relief can be guessed for from the texture alone (the material's or the texture's
// name): masonry, paving, rock, and - since the user found the relief missing on most of what a
// map is made of - concrete, plaster, wood and the facades of the building templates. Their small
// dark spots are holes, joints, cracks and gaps between boards (what is larger - a stain - the
// high pass of the relief leaves out; the depth of these is a few millimetres: PBR_GetReliefDepth).
// Not painted metal (rust and paint are no dents), glass, polished stone - and not the loose ground
// (dirt, grass, sand, gravel): its texture is noise, a relief of noise seen along the ground from a
// standing height smeared the ground under the feet into streaks (the ground of map_v11; with a
// bump map of its own - blends of the coast - it keeps a relief made of that).
//-----------------------------------------------------------------------------
static bool IsReliefMaterial( const char *pszName )
{
	if ( !pszName )
		return false;
	static const char *const s_pszKeys[] = { "brick", "stone", "cobble", "rock", "cliff", "tile", "pave", "paving", "rubble",
		"boulder", "concrete", "cement", "asphalt", "road", "sidewalk", "curb", "wood", "plank", "board", "shingle", "roof",
		"building_template", "storefront_template", "plaster", "stucco", "corrugated" };
	static const char *const s_pszNot[] = { "marble", "glass", "metal", "tools/", "dev/", "water", "sky", "decal", "overlay", "sign",
		"poster", "paint", "wallpaper", "carpet", "fabric", "lights/", "chrome", "mirror", "screen", "monitor",
		"dirt", "grass", "sand", "gravel", "mud", "ground", "nature/" };
	// (corrugated sheets: the light and dark of their folds is their shape)
	if ( V_stristr( pszName, "corrugated" ) )
		return true;
	for ( const char *pszKey : s_pszNot )
	{
		if ( V_stristr( pszName, pszKey ) )
			return false;
	}
	for ( const char *pszKey : s_pszKeys )
	{
		if ( V_stristr( pszName, pszKey ) )
			return true;
	}
	return false;
}

// HL2RPM: the higher resolution versions of the stock world textures (see ShaderReplaceReplMat)
static ConVar r_deferred_hd_textures( "r_deferred_hd_textures", "1", FCVAR_ARCHIVE,
	"World materials use the higher resolution versions of their textures in materials/hd (takes effect as materials load)" );

//-----------------------------------------------------------------------------
// HL2RPM: the fine grain of a surface up close. Half the stock world materials have a detail
// texture ($detail: noise, plaster, metal, rock, wood - the deferred composite draws it as the
// stock shaders do, "mod2x"); the others are a 512 texture over 128 units, soft as soon as a wall
// is a few steps away. They get the stock detail texture of what they are made of, weaker than the
// stock materials use theirs (0.6 .. 0.8): the grain of the surface, not a pattern of its own.
//-----------------------------------------------------------------------------
static ConVar r_deferred_detail_auto( "r_deferred_detail_auto", "0.4", FCVAR_ARCHIVE,
	"Brush materials without a detail texture get the stock one of what they are made of, this strong (0 = none; takes effect as materials load)", true, 0.0f, true, 1.0f );

struct AutoDetail_t
{
	const char *pszKey;
	const char *pszDetail;
	float flScale;
	float flStrength;
};

static const AutoDetail_t s_AutoDetails[] =
{
	// (what is no surface of a building: glass, water, tools, the sky, decals, light fixtures)
	{ "glass",		NULL, 0, 0 },
	{ "water",		NULL, 0, 0 },
	{ "tools/",		NULL, 0, 0 },
	{ "dev/",		NULL, 0, 0 },
	{ "sky",		NULL, 0, 0 },
	{ "decal",		NULL, 0, 0 },
	{ "lights/",	NULL, 0, 0 },
	{ "sign",		NULL, 0, 0 },
	{ "poster",		NULL, 0, 0 },
	{ "overlay",	NULL, 0, 0 },
	{ "plaster",	"detail/plaster_detail_01", 6.8f, 1.0f },
	{ "stucco",		"detail/plaster_detail_01", 6.8f, 1.0f },
	{ "wallpaper",	"detail/plaster_detail_01", 6.8f, 0.6f },
	{ "metal",		"detail/metal_detail_01", 4.3f, 1.0f },
	{ "steel",		"detail/metal_detail_01", 4.3f, 1.0f },
	{ "rust",		"detail/metal_detail_01", 4.3f, 1.0f },
	{ "pipe",		"detail/metal_detail_01", 4.3f, 1.0f },
	{ "wood",		"detail/wood_detail_01", 4.0f, 0.7f },
	{ "plank",		"detail/wood_detail_01", 4.0f, 0.7f },
	{ "rock",		"detail/rock_detail_01", 4.0f, 0.8f },
	{ "cliff",		"detail/rock_detail_01", 4.0f, 0.8f },
	{ "stone",		"detail/rock_detail_01", 6.0f, 0.6f },
	{ "brick",		"detail/noise_detail_01", 7.7f, 1.0f },
	{ "concrete",	"detail/noise_detail_01", 7.3f, 1.0f },
	{ "cement",		"detail/noise_detail_01", 7.3f, 1.0f },
	{ "asphalt",	"detail/noise_detail_01", 7.3f, 0.8f },
	{ "road",		"detail/noise_detail_01", 7.3f, 0.8f },
	{ "pavement",	"detail/noise_detail_01", 7.3f, 0.8f },
	{ "cobble",		"detail/noise_detail_01", 7.3f, 0.8f },
	{ "tile",		"detail/noise_detail_01", 7.3f, 0.5f },
	{ "dirt",		"detail/rock_detail_01", 5.0f, 0.5f },
	{ "mud",		"detail/rock_detail_01", 5.0f, 0.5f },
	{ "ground",		"detail/rock_detail_01", 5.0f, 0.5f },
	{ "gravel",		"detail/rock_detail_01", 5.0f, 0.6f },
	{ "sand",		"detail/noise_detail_01", 7.3f, 0.6f },
	{ "building_template", "detail/noise_detail_01", 7.3f, 0.8f },
};

static void ApplyAutoDetail( KeyValues *pMsg, const char *pszMatname )
{
	const float flStrength = r_deferred_detail_auto.GetFloat();
	if ( flStrength <= 0.0f || !pszMatname || pMsg->FindKey( "$detail" ) )
		return;
	// (a cubemap patch of a map: "maps/<map>/concrete/..._x_y_z")
	const char *pszName = pszMatname;
	if ( !V_strnicmp( pszName, "maps/", 5 ) )
	{
		const char *pszSlash = strchr( pszName + 5, '/' );
		if ( pszSlash )
			pszName = pszSlash + 1;
	}
	for ( const AutoDetail_t &d : s_AutoDetails )
	{
		if ( !V_stristr( pszName, d.pszKey ) )
			continue;
		if ( !d.pszDetail )
			return;
		pMsg->SetString( "$detail", d.pszDetail );
		pMsg->SetFloat( "$detailscale", d.flScale );
		pMsg->SetFloat( "$detailblendfactor", d.flStrength * flStrength );
		pMsg->SetInt( "$detailblendmode", 0 );
		return;
	}
}

// Copied from cdeferred_manager_client.cpp
//-----------------------------------------------------------------------------
// HL2RPM (PBR): what single materials are made of, where the guess of the shaders
// (stdshaders/hl2rpm_pbr.cpp: $surfaceprop, folder, name) is wrong or too coarse.
// scripts/hl2rpm_pbr.txt:  "<text in the material's name>"  "<roughness> <metalness> <specular>"
// (-1 = leave it to the guess; the first line that fits wins). Read when a map loads and by
// r_deferred_pbr_reload; a material that sets $pbr_roughness & co itself keeps its own.
//-----------------------------------------------------------------------------
struct PBROverride_t
{
	char szKey[128];
	float flRoughness, flMetalness, flSpecular;
};
static CUtlVector< PBROverride_t > s_PBROverrides;
static bool s_bPBROverridesLoaded = false;

static void LoadPBROverrides()
{
	s_bPBROverridesLoaded = true;
	s_PBROverrides.RemoveAll();
	KeyValues *pKV = new KeyValues( "hl2rpm_pbr" );
	if ( pKV->LoadFromFile( filesystem, "scripts/hl2rpm_pbr.txt", "MOD" ) )
	{
		for ( KeyValues *pSub = pKV->GetFirstValue(); pSub; pSub = pSub->GetNextValue() )
		{
			PBROverride_t o;
			V_strncpy( o.szKey, pSub->GetName(), sizeof( o.szKey ) );
			V_FixSlashes( o.szKey, '/' );
			o.flRoughness = o.flMetalness = o.flSpecular = -1.0f;
			sscanf( pSub->GetString(), "%f %f %f", &o.flRoughness, &o.flMetalness, &o.flSpecular );
			if ( o.szKey[0] )
				s_PBROverrides.AddToTail( o );
		}
	}
	pKV->deleteThis();
	DevMsg( "[pbr] %d material overrides (scripts/hl2rpm_pbr.txt)\n", s_PBROverrides.Count() );
}

static void ApplyPBROverride( KeyValues *pMsg, const char *pszMatname )
{
	// (read at the first material: the world's materials are loaded before any level init)
	if ( !s_bPBROverridesLoaded )
		LoadPBROverrides();
	if ( !pszMatname || s_PBROverrides.Count() == 0 )
		return;
	char szName[MAX_PATH];
	V_strncpy( szName, pszMatname, sizeof( szName ) );
	V_FixSlashes( szName, '/' );
	for ( int i = 0; i < s_PBROverrides.Count(); i++ )
	{
		const PBROverride_t &o = s_PBROverrides[i];
		if ( !V_stristr( szName, o.szKey ) )
			continue;
		if ( o.flRoughness >= 0.0f && !pMsg->FindKey( "$pbr_roughness" ) )
			pMsg->SetFloat( "$pbr_roughness", o.flRoughness );
		if ( o.flMetalness >= 0.0f && !pMsg->FindKey( "$pbr_metalness" ) )
			pMsg->SetFloat( "$pbr_metalness", o.flMetalness );
		if ( o.flSpecular >= 0.0f && !pMsg->FindKey( "$pbr_specular" ) )
			pMsg->SetFloat( "$pbr_specular", o.flSpecular );
		return;
	}
}

CON_COMMAND( r_deferred_pbr_reload, "Read scripts/hl2rpm_pbr.txt again and load the materials anew" )
{
	LoadPBROverrides();
	engine->ClientCmd_Unrestricted( "mat_reloadallmaterials\n" );
}

//-----------------------------------------------------------------------------
// HL2RPM: has the map cubemaps of its own? A map compiled and never opened with
// buildcubemaps has none: every $envmap material gets the engine's default cubemap. The
// shaders are told (mat_hl2rpm_map_cubemaps): the deferred composite mirrors the light at the
// pixel instead, the forward materials (weather_render.cpp) show less of it.
//-----------------------------------------------------------------------------
static void DetectMapCubemaps()
{
	char szMap[MAX_PATH];
	V_FileBase( engine->GetLevelName(), szMap, sizeof( szMap ) );
	char szPath[MAX_PATH];
	V_snprintf( szPath, sizeof( szPath ), "materials/maps/%s/c*.vtf", szMap );
	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const bool bFound = filesystem->FindFirstEx( szPath, "GAME", &hFind ) != NULL;
	filesystem->FindClose( hFind );

	ConVarRef mat_hl2rpm_map_cubemaps( "mat_hl2rpm_map_cubemaps" );
	if ( mat_hl2rpm_map_cubemaps.IsValid() )
		mat_hl2rpm_map_cubemaps.SetValue( bFound ? 1 : 0 );
	if ( !bFound )
		Msg( "[deferred] map %s has no built cubemaps: $envmap materials mirror the light around them instead\n", szMap );
}

//-----------------------------------------------------------------------------
// HL2RPM: what a decal of DecalModulate is (DEFERRED_DECAL), by its name: blood - a wet film,
// glossy, mirroring its surroundings; a bullet hole - a rough crater with a parallax of its depth
// (in metal a brighter, smoother rim); anything else rough and flat. A value the material names
// itself is kept.
//-----------------------------------------------------------------------------
static void ApplyDecalPBR( KeyValues *pMsg, const char *pszMatname )
{
	const char *pszBase = pMsg->GetString( "$basetexture", "" );
	const char *pszName = pszMatname ? pszMatname : "";
	auto Has = [&]( const char *psz ) { return V_stristr( pszName, psz ) != NULL || V_stristr( pszBase, psz ) != NULL; };

	int iKind = DECAL_RELIEF_NONE;
	float flGloss = 0.3f, flF0 = 0.03f, flWet = 0.0f, flRelief = 0.0f, flBump = 1.0f;
	if ( Has( "blood" ) || Has( "flesh" ) || Has( "gore" ) )
	{
		iKind = DECAL_RELIEF_FILM;
		// (fresh blood is a film of liquid: a mirror - the reflections pass, hl2rpm_mirror.cpp, gives it
		// the surroundings below a roughness of 0.12)
		flGloss = 0.9f;
		flF0 = 0.03f;
		flWet = 1.0f;
	}
	else if ( Has( "shot" ) || Has( "bullet" ) || Has( "impact" ) || Has( "hole" ) )
	{
		iKind = DECAL_RELIEF_HOLE;
		const bool bMetal = Has( "metal" );
		flGloss = bMetal ? 0.55f : 0.12f;
		flF0 = bMetal ? 0.25f : 0.035f;
		flRelief = bMetal ? 0.05f : 0.1f;
		flBump = 1.6f;
	}

	const char *pszRelief = NULL;
	if ( iKind != DECAL_RELIEF_NONE && !pMsg->FindKey( "$decalnormal" ) && DeferredPOM_GetDecalRelief( pszBase, iKind, &pszRelief ) )
		pMsg->SetString( "$decalnormal", pszRelief );

	// the higher resolution version of its texture (materials/hd, hd_textures.py): sampled in its place,
	// the size of the decal stays its stock texture's
	static ConVarRef s_mat_picmip( "mat_picmip" );
	if ( pszBase[0] && !pMsg->FindKey( "$hdtexture" ) && r_deferred_hd_textures.GetBool() && ( !s_mat_picmip.IsValid() || s_mat_picmip.GetInt() <= 0 ) )
	{
		char szHD[ MAX_PATH ], szFile[ MAX_PATH ];
		V_snprintf( szHD, sizeof( szHD ), "hd/%s", pszBase );
		V_FixSlashes( szHD, '/' );
		V_snprintf( szFile, sizeof( szFile ), "materials/%s.vtf", szHD );
		if ( filesystem->FileExists( szFile, "GAME" ) )
			pMsg->SetString( "$hdtexture", szHD );
	}
	if ( !pMsg->FindKey( "$decal_gloss" ) )
		pMsg->SetFloat( "$decal_gloss", flGloss );
	if ( !pMsg->FindKey( "$decal_f0" ) )
		pMsg->SetFloat( "$decal_f0", flF0 );
	if ( !pMsg->FindKey( "$decal_wet" ) )
		pMsg->SetFloat( "$decal_wet", flWet );
	if ( !pMsg->FindKey( "$decal_relief" ) )
		pMsg->SetFloat( "$decal_relief", flRelief );
	if ( !pMsg->FindKey( "$decal_bumpscale" ) )
		pMsg->SetFloat( "$decal_bumpscale", flBump );
}

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
		// HL2RPM: glass of models (the windshield of the APC, bottles, windows of vehicles): blended
		// with a cubemap. The forward fallback lights it with the lighting baked into the map and
		// mirrors the cubemap - the default one on a map without cubemaps: a white or greenish
		// "fullbright" pane in the rain and at night. Composited by the deferred model shader it is
		// lit by the light behind it and mirrors the light around it.
		else if ( !bSelfillum && !V_stricmp( szNewShadername, "DEFERRED_MODEL" ) && msg->FindKey( "$envmap" )
			&& !pMat->GetMaterialVarFlag( MATERIAL_VAR_ADDITIVE ) )
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

	// HL2RPM: where the envmap mask comes from - flags too, lost with them: the deferred composite
	// took the base alpha of every material without a mask texture, and not inverted as the stock
	// shaders have it - the dull parts mirrored, Metrocops were white and the APC blue
	if ( pMat->GetMaterialVarFlag( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
		msg->SetInt( "$basealphaenvmapmask", 1 );
	if ( pMat->GetMaterialVarFlag( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) )
		msg->SetInt( "$normalmapalphaenvmapmask", 1 );

	// HL2RPM: $nofog was lost with the flags as well. The skybox Citadel's materials are
	// $nofog (only its translucent top fades into the sky): the composite fogged the whole
	// tower into a flat sky-colored silhouette.
	if ( pMat->GetMaterialVarFlag( MATERIAL_VAR_NOFOG ) )
	{
		msg->SetInt( "$nofog", 1 );
	}

	ApplyPBROverride( msg, pszMatname );

	const bool bDeferredModel = !V_stricmp( szNewShadername, "DEFERRED_MODEL" );
	const bool bDeferredBrush = !V_stricmp( szNewShadername, "DEFERRED_BRUSH" );

	// HL2RPM: the grain of the surface up close (world materials only: a model's texture coordinates
	// are an unwrap, a detail repeat of it would be any size)
	if ( bDeferredBrush && !bDecal && !bAlphaBlending && !bVegetation )
		ApplyAutoDetail( msg, pszMatname );

	// HL2RPM: higher resolution versions of the stock world textures (materials/hd/<texture>.vtf, made
	// by hl2rpm_extras/tools/hd_textures.py). Not in place of the stock files: the size of a brush
	// texture's repeat is its file's resolution - a 1024 version of a 512 texture would draw every
	// brick twice as large. $basetexture stays (and maps the brushes), the composite samples this.
	// (only at the full texture quality: with mat_picmip 1 the HD version would load at the size of
	// the stock one - memory and load time for nothing)
	static ConVarRef s_mat_picmip( "mat_picmip" );
	if ( bDeferredBrush && !bDecal && !bAlphaBlending && !bAlphaTesting && r_deferred_hd_textures.GetBool()
		&& ( !s_mat_picmip.IsValid() || s_mat_picmip.GetInt() <= 0 ) )
	{
		static const char *const s_pszLayers[][2] = { { "$basetexture", "$hdtexture" }, { "$basetexture2", "$hdtexture2" } };
		for ( const auto &layer : s_pszLayers )
		{
			const char *pszBase = msg->GetString( layer[0], "" );
			if ( !pszBase[0] || msg->FindKey( layer[1] ) )
				continue;
			char szHD[ MAX_PATH ], szFile[ MAX_PATH ];
			V_snprintf( szHD, sizeof( szHD ), "hd/%s", pszBase );
			V_FixSlashes( szHD, '/' );
			V_snprintf( szFile, sizeof( szFile ), "materials/%s.vtf", szHD );
			if ( filesystem->FileExists( szFile, "GAME" ) )
				msg->SetString( layer[1], szHD );
		}
	}

	// HL2RPM: glass in the texture - the windows painted into a facade texture (building_template and
	// its kin mirror a cubemap through their envmap mask), a pane that is a texture of its own (opaque,
	// $surfaceprop glass). Without cubemaps they mirrored the flat light at the pixel: windows as dull
	// as the wall around them. Their glass gets the gloss of glass (a gloss map of the mask, or the
	// material's own gloss times it) and mirrors the sky and the scene around by the angle of the view
	// - the weather pass does that for the smooth pixels (weather_post_ps30); the composite leaves
	// its envmap out for them ($pbr_glassmask).
	if ( bDeferredBrush && !bDecal && !bAlphaBlending && !bAlphaTesting && msg->FindKey( "$envmap" ) && pszMatname )
	{
		static ConVarRef r_deferred_glass_facades( "r_deferred_glass_facades" );
		bool bFound = false;
		IMaterialVar *pSurfaceProp = pMat->FindVar( "$surfaceprop", &bFound, false );
		const bool bGlassProp = bFound && pSurfaceProp && V_stristr( pSurfaceProp->GetStringValue(), "glass" ) != NULL;
		const bool bFacade = V_stristr( pszMatname, "building_template" ) || V_stristr( pszMatname, "courtyard_template" )
			|| V_stristr( pszMatname, "storefront_template" ) || V_stristr( pszMatname, "trainstation_window" );
		if ( r_deferred_glass_facades.IsValid() && r_deferred_glass_facades.GetBool() && ( bGlassProp || bFacade ) )
		{
			bool bGlass = false;
			const char *pszMask = msg->GetString( "$envmapmask", "" );
			const char *pszBase = msg->GetString( "$basetexture", "" );
			const char *pszGloss = NULL;
			if ( pszMask[0] )
				bGlass = DeferredPOM_GetGlassGloss( pszMask, false, false, 0.12f, 0.95f, &pszGloss );
			else if ( msg->GetInt( "$basealphaenvmapmask", 0 ) && pszBase[0] )
				bGlass = DeferredPOM_GetGlassGloss( pszBase, true, true, 0.12f, 0.95f, &pszGloss );
			else if ( msg->GetInt( "$normalmapalphaenvmapmask", 0 ) && msg->GetString( "$bumpmap", "" )[0] )
			{
				// (the alpha of the bump map scales the gloss already: $pbr_glossmask)
				msg->SetFloat( "$pbr_roughness", 0.05f );
				msg->SetInt( "$pbr_glossmask", 1 );
				bGlass = true;
			}
			else if ( bGlassProp )
			{
				msg->SetFloat( "$pbr_roughness", 0.05f );
				bGlass = true;
			}
			if ( bGlass )
			{
				if ( pszGloss )
					msg->SetString( "$phong_map", pszGloss );
				if ( !msg->FindKey( "$pbr_specular" ) )
					msg->SetFloat( "$pbr_specular", 1.0f );
				msg->SetInt( "$pbr_glassmask", 1 );
			}
		}
	}

	// HL2RPM: toxic slime (nature/toxicslime002a & co - not a water: a lightmapped surface of a slime
	// volume) glows, as the radioactive waters do (water.cpp $water_glow): its own light, at night too
	if ( bDeferredBrush && !bDecal && pszMatname && ( V_stristr( pszMatname, "toxicslime" ) || V_stristr( pszMatname, "nature/slime" )
		|| V_stristr( pszMatname, "radioactive" ) ) && !msg->FindKey( "$selfillum" ) )
	{
		msg->SetInt( "$selfillum", 1 );
		msg->SetString( "$selfillumtint", "[0.8 2.2 0.6]" );
	}

	// HL2RPM: the relief of a brush material - a height map made of its bump map, or (none) of its
	// texture with a bump map of that too (deferred_pom.cpp; scripts/hl2rpm_relief.txt names the
	// exceptions; a material that names a $pom_heightmap itself keeps its own). A blend of two
	// textures (WorldVertexTransition: the ground of the maps) gets one for each of them - its
	// shaders march the blend of the two heights.
	if ( bDeferredBrush && !bDecal && !bAlphaBlending && r_deferred_pom_auto.GetBool() && !msg->FindKey( "$pom_heightmap" )
		&& !msg->GetInt( "$multiblend", 0 ) )
	{
		const int iRule = DeferredPOM_GetReliefRule( pszMatname );
		const bool bTextureRelief = r_deferred_pom_texture.GetBool() && iRule != RELIEF_RULE_NONE;
		// one layer: of its bump map or of its texture
		struct Layer_t
		{
			const char *pszHeight;
			const char *pszNormal;	// made of the texture (NULL: the bump map is the material's own)
		};
		auto MakeLayer = [&]( const char *pszBumpKey, const char *pszBaseKey, bool bSSBump ) -> Layer_t
		{
			Layer_t layer = { NULL, NULL };
			const char *pszBump = msg->GetString( pszBumpKey, "" );
			if ( pszBump[0] )
			{
				layer.pszHeight = DeferredPOM_GetHeightTexture( pszBump, bSSBump );
				return layer;
			}
			const char *pszBase = msg->GetString( pszBaseKey, "" );
			if ( bTextureRelief && pszBase[0] && ( IsReliefMaterial( pszMatname ) || IsReliefMaterial( pszBase ) ) )
				DeferredPOM_GetTextureRelief( pszBase, iRule, &layer.pszHeight, &layer.pszNormal );
			return layer;
		};

		const Layer_t layer1 = MakeLayer( "$bumpmap", "$basetexture", msg->GetInt( "$ssbump", 0 ) != 0 );
		if ( !msg->FindKey( "$basetexture2" ) )
		{
			if ( layer1.pszHeight )
			{
				msg->SetString( "$pom_heightmap", layer1.pszHeight );
				if ( layer1.pszNormal )
				{
					msg->SetString( "$bumpmap", layer1.pszNormal );
					msg->SetInt( "$pom_texturerelief", 1 );
				}
			}
		}
		else
		{
			const Layer_t layer2 = MakeLayer( "$bumpmap2", "$basetexture2", false );
			const char *pszFlatHeight = NULL, *pszFlatNormal = NULL;
			if ( ( layer1.pszHeight || layer2.pszHeight ) && DeferredPOM_GetFlatRelief( &pszFlatHeight, &pszFlatNormal ) )
			{
				msg->SetString( "$pom_heightmap", layer1.pszHeight ? layer1.pszHeight : pszFlatHeight );
				msg->SetString( "$pom_heightmap2", layer2.pszHeight ? layer2.pszHeight : pszFlatHeight );
				if ( layer1.pszNormal )
					msg->SetString( "$bumpmap", layer1.pszNormal );
				// (the second layer's bump map needs one of the first: a flat one where it has none)
				else if ( !msg->GetString( "$bumpmap", "" )[0] && layer2.pszHeight )
					msg->SetString( "$bumpmap", pszFlatNormal );
				if ( layer2.pszNormal )
				{
					msg->SetString( "$bumpmap2", layer2.pszNormal );
					// (made of $basetexture2: on its coordinates - $basetexturetransform2)
					msg->SetInt( "$pom_texturerelief2", 1 );
				}
				// (the G-buffer pass marches the blend only with a second bump map: the first one's,
				// as the stock shaders light the second texture of a blend without one)
				else if ( !msg->GetString( "$bumpmap2", "" )[0] )
					msg->SetString( "$bumpmap2", msg->GetString( "$bumpmap", "" ) );
				msg->SetInt( "$pom_texturerelief", ( layer1.pszNormal || layer2.pszNormal ) ? 1 : 0 );
			}
		}
	}

	// HL2RPM: leaves and grass - lit from both sides, glowing against the sun, moving in the wind
	if ( bVegetation && !bDecal )
	{
		const bool bLeaves = bAlphaTesting || bAlphaBlending;
		if ( bLeaves )
		{
			msg->SetInt( "$litface", 1 );
			if ( !msg->FindKey( "$pbr_transmission" ) )
				msg->SetFloat( "$pbr_transmission", bDeferredModel ? 0.6f : 0.45f );
		}
		// (Mapbase's copies of the stock tree materials say "$treesway 0" - its own sway, of the
		// forward shaders, is for the "_sway" skins a mapper picks. Here every tree moves: the
		// wind is the weather's. "$treesway -1" in a material keeps it still.)
		if ( bDeferredModel && IsSwayMaterial( pszMatname ) && msg->GetInt( "$treesway", 0 ) >= 0 )
			msg->SetInt( "$treesway", bLeaves ? 2 : 1 );
		else if ( msg->FindKey( "$treesway" ) )
			msg->SetInt( "$treesway", 0 );
	}
	else if ( bDeferredModel && !bDecal && !bAlphaBlending && pMat->IsTwoSided() && IsClothMaterial( pszMatname ) )
	{
		msg->SetInt( "$litface", 1 );
		if ( !msg->FindKey( "$pbr_transmission" ) )
			msg->SetFloat( "$pbr_transmission", 0.3f );
	}

	// HL2RPM: blood and bullet holes as surfaces of their own
	if ( !V_stricmp( szNewShadername, "DEFERRED_DECAL" ) )
		ApplyDecalPBR( msg, pszMatname );

	// HL2RPM: the glass of a model (DEFERRED_GLASS falls back to its stock shader without the
	// deferred renderer, and draws with the model's vertex format)
	if ( !V_stricmp( szNewShadername, "DEFERRED_GLASS" ) && pszOldShadername && V_stristr( pszOldShadername, "VertexLit" ) )
		msg->SetInt( "$glass_model", 1 );

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
static ConVar r_deferred_decal_replace( "r_deferred_decal_replace", "1", 0, "Blood and bullet holes (DecalModulate) get the deferred decal shader: their own normal, gloss and reflection (takes effect as materials load)" );
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

	// HL2RPM: blood and bullet holes (DecalModulate) as surfaces of their own - DEFERRED_DECAL: their
	// normal and gloss in the G-buffer, their reflection in the composite. Not the decals of glass:
	// the pane is drawn after the deferred passes - the G-buffer under it is the wall behind.
	if ( V_stristr( pShaderName, "DecalModulate" ) )
	{
		if ( !r_deferred_decal_replace.GetBool() || V_stristr( pMat->GetName(), "glass" ) )
			return false;
		ShaderReplaceReplMat( "DEFERRED_DECAL", pMat );
		matCount++;
		return true;
	}

	// HL2RPM: blended panes of glass as glass (see IsGlassMaterial)
	if ( IsGlassMaterial( pMat, pShaderName ) )
	{
		ShaderReplaceReplMat( "DEFERRED_GLASS", pMat );
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
// HL2RPM: soft particles - the stock SpriteCard fades particles where they cut into the
// scene ($depthblend), off by default on the PC. With the scene depth the deferred renderer
// writes for it (viewrender_deferred.cpp, DeferredSoftParticleDepth) it is turned on.
//-----------------------------------------------------------------------------
// (the engine's default of 50 units thinned out everything that hugs a floor: a puff 10 units
// above it, seen at a low angle, was already half gone)
static ConVar r_deferred_soft_particles_scale( "r_deferred_soft_particles_scale", "24", 0,
	"Soft particles: distance (units) over which a particle fades toward the surface behind it, for materials that keep the engine's default of 50 (for particle materials loaded afterwards)", true, 4.0f, true, 200.0f );

static void EnableSoftParticles( IMaterial *pMat )
{
	static ConVarRef r_deferred_soft_particles( "r_deferred_soft_particles" );
	if ( !pMat || !r_deferred_soft_particles.IsValid() || !r_deferred_soft_particles.GetBool() )
		return;

	const char *pszShader = pMat->GetShaderName();
	if ( !pszShader || V_stricmp( pszShader, "SpriteCard" ) != 0 )
		return;

	bool bFound = false;
	IMaterialVar *pVar = pMat->FindVar( "$depthblend", &bFound, false );
	if ( !bFound || !pVar || pVar->GetIntValue() != 0 )
		return;

	pVar->SetIntValue( 1 );

	bool bFoundScale = false;
	IMaterialVar *pScale = pMat->FindVar( "$depthblendscale", &bFoundScale, false );
	if ( bFoundScale && pScale && fabsf( pScale->GetFloatValue() - 50.0f ) < 0.01f )
		pScale->SetFloatValue( r_deferred_soft_particles_scale.GetFloat() );

	pMat->RecomputeStateSnapshots();
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

extern void DeferredSkyLUT_ForceRebuild();	// viewrender_deferred.cpp
extern void WeatherRender_OnMaterialsRestored();	// weather_render.cpp

static void DeferredMaterials_OnRestore( int nChangeFlags )
{
	DevMsg( 2, "[deferred] material system restores its resources (flags %d)\n", nChangeFlags );
	// other restore functions (the model cache) may come after this one: the flag is cleared
	// at the next level init or frame
	s_bMaterialsRestoring = true;
	s_bReplaceSweepPending = true;

	// HL2RPM: the render targets come back empty - what is kept in them between frames
	// starts over: the GI probe atlas is uploaded again, TAA drops its history, the eye
	// adapts anew
	DeferredGI_OnRTContentLost();
	DeferredTAA_Reset();
	DeferredPostFX_Reset();
	DeferredSkyLUT_ForceRebuild();
	// (the materials may have been loaded anew: the colors scaled with the light go back in)
	WeatherRender_OnMaterialsRestored();
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
		if ( pMat && pMat->IsPrecached() )
			EnableSoftParticles( pMat );
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
		DetectMapCubemaps();
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
	EnableSoftParticles( pMat );
	return pMat;
}
