// HL2RPM: physically based materials for the deferred renderer - what a stock material is made
// of (see hl2rpm_pbr.h)

#include "deferred_includes.h"
#include "hl2rpm_pbr.h"
#include "tier1/fmtstr.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_pbr_list( "r_deferred_pbr_list", "", 0,
	"Dev: print the roughness / metalness / specular level given to the materials whose name holds this text, as they load (* = all)" );

bool PBR_ShouldReport( const char *pszMaterialName )
{
	const char *pszFilter = r_deferred_pbr_list.GetString();
	if ( !pszFilter || !pszFilter[0] || !pszMaterialName )
		return false;
	return pszFilter[0] == '*' || V_stristr( pszMaterialName, pszFilter ) != NULL;
}

struct pbrSurface_t
{
	float flRoughness;
	float flMetalness;
	float flSpecular;
};

struct pbrKeyword_t
{
	const char *pszKey;
	pbrSurface_t surface;
};

// Metals of the stock games are painted, rusty, dusty: half a metal. What is left bare
// (chrome, pipes, weapons) is said by name.
#define PBR_METAL_PAINTED	{ 0.45f, 0.50f, 1.0f }
#define PBR_METAL_BARE		{ 0.40f, 0.75f, 1.0f }
#define PBR_METAL_RUSTY		{ 0.70f, 0.30f, 1.0f }
#define PBR_GLASS			{ 0.12f, 0.00f, 1.3f }
#define PBR_TILE			{ 0.35f, 0.00f, 1.3f }
#define PBR_WOOD			{ 0.62f, 0.00f, 1.0f }
#define PBR_WOOD_FINISHED	{ 0.42f, 0.00f, 1.1f }
#define PBR_MASONRY			{ 0.80f, 0.00f, 1.0f }
#define PBR_ASPHALT			{ 0.72f, 0.00f, 1.0f }
#define PBR_GROUND			{ 0.90f, 0.00f, 0.45f }
#define PBR_MUD				{ 0.62f, 0.00f, 1.1f }
#define PBR_PLASTIC			{ 0.48f, 0.00f, 1.0f }
#define PBR_RUBBER			{ 0.85f, 0.00f, 0.8f }
#define PBR_PAPER			{ 0.92f, 0.00f, 0.8f }
#define PBR_CLOTH			{ 0.96f, 0.00f, 0.4f }
#define PBR_FLESH			{ 0.58f, 0.00f, 0.8f }
#define PBR_WET				{ 0.15f, 0.00f, 1.0f }
#define PBR_LEAVES			{ 0.80f, 0.00f, 0.3f }

// $surfaceprop: the first key the name starts with or holds (see the flag)
static const pbrKeyword_t s_SurfaceProps[] =
{
	{ "combine_glass",		PBR_GLASS },
	{ "glass",				PBR_GLASS },
	{ "ice",				{ 0.18f, 0.0f, 1.0f } },
	{ "ceiling_tile",		PBR_MASONRY },
	{ "tile",				PBR_TILE },
	{ "porcelain",			{ 0.25f, 0.0f, 1.3f } },
	{ "pottery",			{ 0.40f, 0.0f, 1.1f } },
	{ "combine_metal",		{ 0.40f, 0.65f, 1.0f } },
	{ "slipperymetal",		{ 0.30f, 0.80f, 1.0f } },
	{ "solidmetal",			{ 0.45f, 0.60f, 1.0f } },
	{ "metalvehicle",		{ 0.42f, 0.45f, 1.0f } },
	{ "metalgrate",			PBR_METAL_BARE },
	{ "metalvent",			{ 0.45f, 0.60f, 1.0f } },
	{ "chainlink",			PBR_METAL_BARE },
	{ "chain",				PBR_METAL_BARE },
	{ "crowbar",			PBR_METAL_BARE },
	{ "weapon",				{ 0.42f, 0.60f, 1.0f } },
	{ "popcan",				{ 0.35f, 0.80f, 1.0f } },
	{ "metal",				PBR_METAL_PAINTED },
	{ "canister",			PBR_METAL_PAINTED },
	{ "paintcan",			PBR_METAL_PAINTED },
	{ "grenade",			PBR_METAL_PAINTED },
	{ "gunship",			{ 0.48f, 0.35f, 1.0f } },
	{ "strider",			{ 0.50f, 0.30f, 1.0f } },
	{ "roller",				PBR_METAL_PAINTED },
	{ "computer",			{ 0.45f, 0.15f, 1.0f } },
	{ "wood_furniture",		PBR_WOOD_FINISHED },
	{ "wood",				PBR_WOOD },
	{ "asphalt",			PBR_ASPHALT },
	{ "concrete",			PBR_MASONRY },
	{ "brick",				PBR_MASONRY },
	{ "plaster",			PBR_MASONRY },
	{ "boulder",			PBR_MASONRY },
	{ "baserock",			PBR_MASONRY },
	{ "rock",				PBR_MASONRY },
	{ "gravel",				PBR_GROUND },
	{ "mud",				PBR_MUD },
	{ "slime",				{ 0.30f, 0.0f, 1.0f } },
	{ "snow",				{ 0.70f, 0.0f, 1.0f } },
	{ "dirt",				PBR_GROUND },
	{ "grass",				PBR_GROUND },
	{ "sand",				PBR_GROUND },
	{ "foliage",			PBR_LEAVES },
	{ "plastic",			PBR_PLASTIC },
	{ "rubber",				PBR_RUBBER },
	{ "tire",				PBR_RUBBER },
	{ "cardboard",			PBR_PAPER },
	{ "paper",				PBR_PAPER },
	{ "carpet",				PBR_CLOTH },
	{ "watermelon",			{ 0.40f, 0.0f, 1.0f } },
	{ "flesh",				PBR_FLESH },
	{ "antlion",			{ 0.45f, 0.0f, 1.0f } },
	{ "puddle",				PBR_WET },
	{ "water",				PBR_WET },
	{ "wade",				PBR_WET },
};

// the folder and the name of the material (without "maps/<map>/" of the cubemap patches)
static const pbrKeyword_t s_Names[] =
{
	{ "models/weapons",		{ 0.42f, 0.60f, 1.0f } },
	// (leaves: thin cards with rough normals - a sharp reflection would sparkle)
	{ "foliage",			PBR_LEAVES },
	{ "detail/",			PBR_LEAVES },
	{ "leaf",				PBR_LEAVES },
	{ "leaves",				PBR_LEAVES },
	{ "bush",				PBR_LEAVES },
	{ "shrub",				PBR_LEAVES },
	{ "ivy",				PBR_LEAVES },
	{ "fern",				PBR_LEAVES },
	{ "weed",				PBR_LEAVES },
	{ "glass",				PBR_GLASS },
	{ "window",				PBR_GLASS },
	{ "chrome",				{ 0.18f, 1.00f, 1.0f } },
	{ "mirror",				{ 0.10f, 1.00f, 1.0f } },
	{ "rust",				PBR_METAL_RUSTY },
	{ "corrugated",			PBR_METAL_PAINTED },
	{ "metal",				PBR_METAL_PAINTED },
	{ "steel",				PBR_METAL_BARE },
	{ "copper",				PBR_METAL_BARE },
	{ "brass",				PBR_METAL_BARE },
	{ "alumin",				PBR_METAL_BARE },
	{ "pipe",				PBR_METAL_PAINTED },
	{ "girder",				PBR_METAL_PAINTED },
	{ "combine",			{ 0.40f, 0.65f, 1.0f } },
	{ "marble",				{ 0.25f, 0.0f, 1.3f } },
	{ "ceramic",			PBR_TILE },
	{ "lino",				{ 0.40f, 0.0f, 1.2f } },
	{ "tile",				PBR_TILE },
	{ "plywood",			PBR_WOOD },
	{ "wood",				PBR_WOOD },
	{ "plank",				PBR_WOOD },
	{ "crate",				PBR_WOOD },
	{ "asphalt",			PBR_ASPHALT },
	{ "road",				PBR_ASPHALT },
	{ "pavement",			PBR_ASPHALT },
	{ "cobble",				PBR_MASONRY },
	{ "concrete",			PBR_MASONRY },
	{ "cement",				PBR_MASONRY },
	{ "plaster",			PBR_MASONRY },
	{ "stucco",				PBR_MASONRY },
	{ "brick",				PBR_MASONRY },
	{ "stone",				PBR_MASONRY },
	{ "cliff",				PBR_MASONRY },
	{ "rock",				PBR_MASONRY },
	{ "gravel",				PBR_GROUND },
	{ "mud",				PBR_MUD },
	{ "snow",				{ 0.70f, 0.0f, 1.0f } },
	{ "sand",				PBR_GROUND },
	{ "dirt",				PBR_GROUND },
	{ "grass",				PBR_GROUND },
	{ "ground",				PBR_GROUND },
	{ "carpet",				PBR_CLOTH },
	{ "cloth",				PBR_CLOTH },
	{ "fabric",				PBR_CLOTH },
	{ "curtain",			PBR_CLOTH },
	{ "mattress",			PBR_CLOTH },
	{ "plastic",			PBR_PLASTIC },
	{ "vinyl",				PBR_PLASTIC },
	{ "rubber",				PBR_RUBBER },
	{ "tire",				PBR_RUBBER },
	{ "cardboard",			PBR_PAPER },
	{ "paper",				PBR_PAPER },
	{ "humans/",			PBR_FLESH },
	{ "characters/",		PBR_FLESH },
	{ "zombie",				PBR_FLESH },
	{ "headcrab",			{ 0.45f, 0.0f, 1.0f } },
	{ "antlion",			{ 0.45f, 0.0f, 1.0f } },
};

static bool FindKeyword( const pbrKeyword_t *pTable, int nCount, const char *pszText, pbrSurface_t &out )
{
	if ( !pszText || !pszText[0] )
		return false;
	for ( int i = 0; i < nCount; i++ )
	{
		if ( V_stristr( pszText, pTable[i].pszKey ) )
		{
			out = pTable[i].surface;
			return true;
		}
	}
	return false;
}

// "maps/<map>/concrete/..." -> "concrete/..."
static const char *StripMapPrefix( const char *pszName )
{
	if ( pszName && !V_strnicmp( pszName, "maps/", 5 ) )
	{
		const char *pszSlash = strchr( pszName + 5, '/' );
		if ( pszSlash )
			return pszSlash + 1;
	}
	return pszName;
}

static const char *StringParm( IMaterialVar **params, int i )
{
	if ( i < 0 || !params[i]->IsDefined() )
		return NULL;
	const char *psz = params[i]->GetStringValue();
	return ( psz && psz[0] ) ? psz : NULL;
}

static bool BoolParm( IMaterialVar **params, int i )
{
	return i >= 0 && params[i]->IsDefined() && params[i]->GetIntValue() != 0;
}

static pbrSurface_t Classify( const char *pszSurfaceProp, const char *pszName, bool bModel )
{
	pbrSurface_t s = { bModel ? 0.60f : 0.78f, 0.0f, 1.0f };
	if ( pszSurfaceProp && !V_strnicmp( pszSurfaceProp, "default", 7 ) )
		pszSurfaceProp = NULL;
	if ( !FindKeyword( s_SurfaceProps, ARRAYSIZE( s_SurfaceProps ), pszSurfaceProp, s ) )
		FindKeyword( s_Names, ARRAYSIZE( s_Names ), StripMapPrefix( pszName ), s );
	return s;
}

//-----------------------------------------------------------------------------
// The relief of the brush materials
//-----------------------------------------------------------------------------
static ConVar r_deferred_pom( "r_deferred_pom", "1", FCVAR_ARCHIVE,
	"Parallax occlusion mapping: brush materials with a bump map get a relief (the stones of a pavement overlap the gaps, the mortar sinks between the bricks)" );
static ConVar r_deferred_pom_scale( "r_deferred_pom_scale", "1.0", FCVAR_ARCHIVE,
	"Parallax occlusion mapping: scale of the depth of all reliefs", true, 0.0f, true, 4.0f );
static ConVar r_deferred_pom_distance( "r_deferred_pom_distance", "1100", 0,
	"Parallax occlusion mapping: the distance at which the relief is gone (it fades over the far half)", true, 64.0f, true, 8192.0f );

struct pbrRelief_t
{
	const char *pszKey;
	float flDepth;
};

// the depth of the relief in repeats of the texture (a wall texture repeats every ~128 units)
static const pbrRelief_t s_Reliefs[] =
{
	{ "cobble",		0.034f },
	{ "pavement",	0.026f },
	{ "rubble",		0.034f },
	{ "gravel",		0.030f },
	{ "rock",		0.032f },
	{ "cliff",		0.034f },
	{ "stone",		0.028f },
	{ "brick",		0.022f },
	{ "roof",		0.022f },
	{ "shingle",	0.022f },
	{ "plank",		0.016f },
	{ "wood",		0.012f },
	{ "crate",		0.012f },
	{ "tile",		0.012f },
	{ "concrete",	0.012f },
	{ "cement",		0.012f },
	{ "asphalt",	0.012f },
	{ "road",		0.012f },
	{ "plaster",	0.008f },
	{ "stucco",		0.010f },
	{ "dirt",		0.022f },
	{ "mud",		0.022f },
	{ "ground",		0.020f },
	{ "grass",		0.018f },
	{ "sand",		0.012f },
	{ "snow",		0.012f },
	{ "corrugated",	0.020f },
	{ "grate",		0.016f },
	{ "metal",		0.008f },
	{ "glass",		0.0f },
	{ "window",		0.0f },
	{ "water",		0.0f },
};

float PBR_GetReliefDepth( const char *pszMaterialName )
{
	const char *pszName = StripMapPrefix( pszMaterialName );
	if ( pszName )
	{
		for ( int i = 0; i < ARRAYSIZE( s_Reliefs ); i++ )
		{
			if ( V_stristr( pszName, s_Reliefs[i].pszKey ) )
				return s_Reliefs[i].flDepth;
		}
	}
	return 0.014f;
}

void PBR_GetReliefConstants( float flMaterialDepth, bool bReflectionView, float *pOut4 )
{
	pOut4[0] = pOut4[1] = pOut4[2] = pOut4[3] = 0.0f;
	// (the 3D skybox: its geometry is a 16th of its size - a repeat of a texture spans half a
	// house; a reflection view has no G-buffer pass of its own to shift the normals alike)
	if ( !r_deferred_pom.GetBool() || bReflectionView || flMaterialDepth <= 0.0f || GetDeferredExt()->GetZScale() > 1.5f )
		return;
	const float flEnd = r_deferred_pom_distance.GetFloat();
	const float flStart = flEnd * 0.5f;
	// (a quarter of what the table says. The table is depth as a game would exaggerate it; a stock
	// texture is 128 units a repeat, and 0.02 of that is mortar sunk 7 cm deep: at any angle the
	// bricks closed over it and their sides were stretched into fur. This is 1 - 2 cm.)
	pOut4[0] = flMaterialDepth * 0.25f * r_deferred_pom_scale.GetFloat();
	pOut4[1] = 1.0f / ( flEnd - flStart );
	pOut4[2] = flEnd / ( flEnd - flStart );
}

//-----------------------------------------------------------------------------
// Trees in the wind
//-----------------------------------------------------------------------------
static ConVar r_deferred_treesway( "r_deferred_treesway", "1", FCVAR_ARCHIVE,
	"Trees, bushes and grass move in the wind of the weather" );
static ConVar r_deferred_treesway_strength( "r_deferred_treesway_strength", "1.0", FCVAR_ARCHIVE,
	"Wind sway: how far the trees lean", true, 0.0f, true, 8.0f );
static ConVar r_deferred_treesway_speed( "r_deferred_treesway_speed", "1.0", 0,
	"Wind sway: how fast the trees swing", true, 0.0f, true, 8.0f );
static ConVar r_deferred_treesway_calm( "r_deferred_treesway_calm", "0.16", 0,
	"Wind sway: the part left on a day without wind", true, 0.0f, true, 2.0f );
static ConVar r_deferred_treesway_leaves( "r_deferred_treesway_leaves", "0.55", 0,
	"Wind sway: the shiver of the leaves (units)", true, 0.0f, true, 8.0f );

bool PBR_TreeSwayEnabled()
{
	return r_deferred_treesway.GetBool();
}

void PBR_SetTreeSwayConstants( IShaderDynamicAPI *pShaderAPI, int iMode )
{
	// the wind of the weather: the slant of the rain (0 .. 0.5 of a unit sideways per unit down)
	const weatherData_t &w = GetDeferredExt()->GetWeatherData();
	float flWind[4] = { 0.12f, 0.05f, 0.0f, 0.0f };
	if ( w.bEnabled )
	{
		flWind[0] = w.vecWind.z * 2.0f;
		flWind[1] = w.vecWind.w * 2.0f;
	}
	if ( !r_deferred_treesway.GetBool() )
		flWind[0] = flWind[1] = 0.0f;
	flWind[2] = pShaderAPI->CurrentTime();
	flWind[3] = ( iMode >= 2 && r_deferred_treesway.GetBool() ) ? r_deferred_treesway_leaves.GetFloat() : 0.0f;
	const float flParams[4] = { 1.6f * r_deferred_treesway_strength.GetFloat(),
		r_deferred_treesway.GetBool() ? r_deferred_treesway_calm.GetFloat() : 0.0f,
		r_deferred_treesway_speed.GetFloat(), 0.0f };
	pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, flWind );
	pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_9, flParams );
}

//-----------------------------------------------------------------------------
// Thin things the light shines through
//-----------------------------------------------------------------------------
static ConVar r_deferred_transmission( "r_deferred_transmission", "1.0", FCVAR_ARCHIVE,
	"Leaves, grass and cloth let the light through: they glow seen against the sun (0 = off)", true, 0.0f, true, 4.0f );
static ConVar r_deferred_transmission_power( "r_deferred_transmission_power", "3.0", 0,
	"Light through leaves: how narrow the glow about the direction of the sun", true, 0.5f, true, 32.0f );
static ConVar r_deferred_transmission_tint( "r_deferred_transmission_tint", "0.65", 0,
	"Light through leaves: how much of the leaf's own color the light takes on the way through", true, 0.0f, true, 1.0f );

void PBR_GetTransmissionConstants( float flMaterial, float *pTransmit4, float *pSunDir4 )
{
	pTransmit4[0] = pTransmit4[1] = pTransmit4[2] = pTransmit4[3] = 0.0f;
	pSunDir4[0] = pSunDir4[1] = pSunDir4[3] = 0.0f;
	pSunDir4[2] = 1.0f;
	pTransmit4[1] = r_deferred_transmission_power.GetFloat();
	const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
	if ( flMaterial <= 0.0f || !light.bEnabled )
		return;
	pTransmit4[0] = flMaterial * r_deferred_transmission.GetFloat() * 1.6f;
	pTransmit4[2] = r_deferred_transmission_tint.GetFloat();
	pSunDir4[0] = light.vecLight.x;
	pSunDir4[1] = light.vecLight.y;
	pSunDir4[2] = light.vecLight.z;
}

void InitParmsPBR( const pbrParms_t &info, IMaterialVar **params, const char *pszMaterialName )
{
	const char *pszSurfaceProp = StringParm( params, info.iSurfaceProp );
	pbrSurface_t s = Classify( pszSurfaceProp, pszMaterialName, info.bModel );

	// what the author said about its reflections: a cubemap and how strong (masked or not)
	const bool bBump = info.iBumpmap >= 0 && params[ info.iBumpmap ]->IsDefined();
	const bool bNormalAlphaMask = BoolParm( params, info.iNormalAlphaEnvmapMask );
	bool bGlossMask = bBump && bNormalAlphaMask;
	if ( info.iEnvmap >= 0 && params[ info.iEnvmap ]->IsDefined() )
	{
		float flTint[3] = { 1.0f, 1.0f, 1.0f };
		if ( info.iEnvmapTint >= 0 && params[ info.iEnvmapTint ]->IsDefined() )
			params[ info.iEnvmapTint ]->GetVecValue( flTint, 3 );
		const float flStrength = clamp( ( flTint[0] * 0.3f + flTint[1] * 0.59f + flTint[2] * 0.11f ) * 1.5f, 0.0f, 1.0f );
		s.flRoughness = MIN( s.flRoughness, 0.65f - 0.45f * flStrength );
		s.flSpecular = MAX( s.flSpecular, 1.0f + 1.5f * flStrength );
	}

	// $phong of the models: the exponent is a roughness, the boost a specular level, the mask
	// the alpha of the bump map (unless it is the base texture's)
	if ( BoolParm( params, info.iPhong ) )
	{
		float flExp = ( info.iPhongExponent >= 0 && params[ info.iPhongExponent ]->IsDefined() ) ? params[ info.iPhongExponent ]->GetFloatValue() : 0.0f;
		if ( flExp <= 0.0f )
			flExp = 20.0f;
		// (Blinn-Phong n -> GGX: alpha^2 = 2 / ( n + 2 ), roughness = sqrt( alpha ))
		s.flRoughness = MIN( s.flRoughness, clamp( powf( 2.0f / ( flExp + 2.0f ), 0.25f ), 0.25f, 0.8f ) );
		const float flBoost = ( info.iPhongBoost >= 0 && params[ info.iPhongBoost ]->IsDefined() ) ? params[ info.iPhongBoost ]->GetFloatValue() : 1.0f;
		s.flSpecular = MAX( s.flSpecular, clamp( 0.8f + 0.2f * flBoost, 0.8f, 3.0f ) );
		if ( bBump && !BoolParm( params, info.iBaseAlphaPhongMask ) )
			bGlossMask = true;
	}

	// what the material says itself
	if ( info.iRoughness >= 0 && params[ info.iRoughness ]->IsDefined() && params[ info.iRoughness ]->GetFloatValue() >= 0.0f )
		s.flRoughness = params[ info.iRoughness ]->GetFloatValue();
	if ( info.iMetalness >= 0 && params[ info.iMetalness ]->IsDefined() && params[ info.iMetalness ]->GetFloatValue() >= 0.0f )
		s.flMetalness = params[ info.iMetalness ]->GetFloatValue();
	if ( info.iSpecular >= 0 && params[ info.iSpecular ]->IsDefined() && params[ info.iSpecular ]->GetFloatValue() >= 0.0f )
		s.flSpecular = params[ info.iSpecular ]->GetFloatValue();
	if ( info.iGlossMask >= 0 && params[ info.iGlossMask ]->IsDefined() )
		bGlossMask = bBump && params[ info.iGlossMask ]->GetIntValue() != 0;

	s.flRoughness = clamp( s.flRoughness, 0.0f, 1.0f );
	s.flMetalness = clamp( s.flMetalness, 0.0f, 1.0f );
	s.flSpecular = clamp( s.flSpecular, 0.0f, 4.0f );

	if ( info.iRoughness >= 0 )
		params[ info.iRoughness ]->SetFloatValue( s.flRoughness );
	if ( info.iMetalness >= 0 )
		params[ info.iMetalness ]->SetFloatValue( s.flMetalness );
	if ( info.iSpecular >= 0 )
		params[ info.iSpecular ]->SetFloatValue( s.flSpecular );
	if ( info.iGlossMask >= 0 )
		params[ info.iGlossMask ]->SetIntValue( bGlossMask ? 1 : 0 );

	// the gloss of the G-buffer (a material that sets it itself - $phong_exp - keeps it)
	if ( info.iGloss >= 0 && !params[ info.iGloss ]->IsDefined() )
		params[ info.iGloss ]->SetFloatValue( 1.0f - s.flRoughness );

	// the second texture of a blend: by its own name
	float flRoughness2 = s.flRoughness;
	if ( info.iGloss2 >= 0 )
	{
		if ( info.iRoughness2 >= 0 && params[ info.iRoughness2 ]->IsDefined() && params[ info.iRoughness2 ]->GetFloatValue() >= 0.0f )
			flRoughness2 = clamp( params[ info.iRoughness2 ]->GetFloatValue(), 0.0f, 1.0f );
		else
		{
			pbrSurface_t s2 = s;
			if ( FindKeyword( s_Names, ARRAYSIZE( s_Names ), StringParm( params, info.iBaseTexture2 ), s2 ) )
				flRoughness2 = s2.flRoughness;
		}
		if ( !params[ info.iGloss2 ]->IsDefined() )
			params[ info.iGloss2 ]->SetFloatValue( 1.0f - flRoughness2 );
	}

	if ( PBR_ShouldReport( pszMaterialName ) )
	{
		Msg( "[pbr] %s (%s%s): roughness %.2f%s, metalness %.2f, specular %.2f%s\n", pszMaterialName,
			pszSurfaceProp ? pszSurfaceProp : "no surfaceprop", info.bModel ? ", model" : "",
			s.flRoughness, ( info.iGloss2 >= 0 && flRoughness2 != s.flRoughness ) ? CFmtStr( " / %.2f", flRoughness2 ).Access() : "",
			s.flMetalness, s.flSpecular, bGlossMask ? ", gloss mask in the bump map's alpha" : "" );
	}
}
