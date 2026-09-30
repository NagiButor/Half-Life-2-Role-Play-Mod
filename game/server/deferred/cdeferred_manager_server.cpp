
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_verbose.h"
#include "mapentities.h"
#include "filesystem.h"
#include "bspfile.h"
#include "utlbuffer.h"
#include "lzmaDecoder.h"
#include "icommandline.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred( "r_deferred", "1", FCVAR_ARCHIVE, "Enable deferred renderer (requires SM3)." );

static CDeferredManagerServer __g_defmanager;

IGameSystem* DeferredManagerSystem()
{
	return &__g_defmanager;
}

CDeferredManagerServer *GetDeferredManager()
{
	return &__g_defmanager;
}

CDeferredManagerServer::CDeferredManagerServer()
{
}

CDeferredManagerServer::~CDeferredManagerServer()
{
}

bool CDeferredManagerServer::Init()
{
	return true;
}

void CDeferredManagerServer::Shutdown()
{
}

#define LIGHT_MIN_LIGHT_VALUE 0.03f
float ComputeLightRadius( float radius, float intensity, float constant_attn, float linear_attn, float quadratic_attn )
{
	float flLightRadius = radius;
	if (flLightRadius == 0.0f)
	{
		// Compute the light range based on attenuation factors
		if (quadratic_attn == 0.0f)
		{
			if (linear_attn == 0.0f)
			{
				// Infinite, but we're not going to draw it as such
				flLightRadius = 2000;
			}
			else
			{
				flLightRadius = (intensity / LIGHT_MIN_LIGHT_VALUE - constant_attn) / linear_attn;
			}
		}
		else
		{
			const float a = quadratic_attn;
			const float b = linear_attn;
			const float c = constant_attn - intensity / LIGHT_MIN_LIGHT_VALUE;
			const float discrim = b * b - 4 * a * c;
			if (discrim < 0.0f)
			{
				// Infinite, but we're not going to draw it as such
				flLightRadius = 2000;
			}
			else
			{
				flLightRadius = (-b + sqrtf(discrim)) / (2.0f * a);
				if (flLightRadius < 0)
					flLightRadius = 0;
			}
		}
	}
	return flLightRadius;
}

float ComputeLightRadius( const dworldlight_t &light )
{
	float flLightRadius = light.radius;
	if (flLightRadius == 0.0f)
	{
		// Compute the light range based on attenuation factors
		const float flIntensity = sqrtf( DotProduct( light.intensity, light.intensity ) );
		if ( light.quadratic_attn == 0.0f )
		{
			if ( light.linear_attn == 0.0f )
			{
				// Infinite, but we're not going to draw it as such
				flLightRadius = 2000;
			}
			else
			{
				flLightRadius = (flIntensity / LIGHT_MIN_LIGHT_VALUE - light.constant_attn) / light.linear_attn;
			}
		}
		else
		{
			const float a = light.quadratic_attn;
			const float b = light.linear_attn;
			const float c = light.constant_attn - flIntensity / LIGHT_MIN_LIGHT_VALUE;
			const float discrim = b * b - 4 * a * c;
			if (discrim < 0.0f)
			{
				// Infinite, but we're not going to draw it as such
				flLightRadius = 2000;
			}
			else
			{
				flLightRadius = (-b + sqrtf(discrim)) / (2.0f * a);
				if (flLightRadius < 0)
					flLightRadius = 0;
			}
		}
	}

	return flLightRadius;
}

ConVar r_deferred_autoenvlight_ambient_intensity_low("r_deferred_autoenvlight_ambient_intensity_low", "0.1");
ConVar r_deferred_autoenvlight_ambient_intensity_high("r_deferred_autoenvlight_ambient_intensity_high", "0.2");
ConVar r_deferred_autoenvlight_diffuse_intensity("r_deferred_autoenvlight_diffuse_intensity", "1");
ConVar r_deferred_verbose( "r_deferred_verbose", "0", FCVAR_CHEAT, "0=off, 1=entity lifecycle, 2=net updates, 3=spam" );

// ---------------------------------------------------------------------------
// HL2RPM: reach of a classic light entity. vrad normalizes the attenuation so that the
// brightness is the light at 100 units: I(d) = I0 * S / ( c + l d + q d^2 ),
// S = c + 100 l + 10000 q. The old code solved the raw formula and got 0 for most
// Hammer lights (constant/linear attenuations of 10000 and more).
// ---------------------------------------------------------------------------
static float ComputeEntityLightRadius( KeyValues *entity, const int color[4] )
{
	const float flZero = entity->GetFloat( "_zero_percent_distance" );
	if ( flZero > 0.0f )
		return clamp( flZero, 64.0f, 4096.0f );
	const float flFifty = entity->GetFloat( "_fifty_percent_distance" );
	if ( flFifty > 0.0f )
		return clamp( flFifty * 2.5f, 64.0f, 4096.0f );
	const float flDistance = entity->GetFloat( "_distance" );
	if ( flDistance > 0.0f )
		return clamp( flDistance, 64.0f, 4096.0f );

	float c = entity->GetFloat( "_constant_attn" );
	float l = entity->GetFloat( "_linear_attn" );
	float q = entity->GetFloat( "_quadratic_attn" );
	if ( c <= 0.0f && l <= 0.0f && q <= 0.0f )
		q = 1.0f;
	const float S = c + 100.0f * l + 10000.0f * q;
	const float I0 = ( Max( color[0], Max( color[1], color[2] ) ) / 255.0f ) * ( color[3] / 255.0f );
	const float K = I0 * S / 0.02f;	// attenuation where the light is down to 2 %

	float d;
	if ( q > 0.0f )
	{
		const float disc = l * l - 4.0f * q * ( c - K );
		d = ( disc < 0.0f ) ? 2000.0f : ( -l + sqrtf( disc ) ) / ( 2.0f * q );
	}
	else if ( l > 0.0f )
	{
		d = ( K - c ) / l;
	}
	else
	{
		d = 2000.0f;
	}
	return clamp( d, 64.0f, 4096.0f );
}

// ---------------------------------------------------------------------------
// HL2RPM: compiled lights (dworldlight_t) -> light_deferred
// ---------------------------------------------------------------------------
ConVar r_deferred_convert_worldlights( "r_deferred_convert_worldlights", "1", 0, "Turn the map's compiled lights (incl. texture lights) into deferred lights" );
ConVar r_deferred_convert_surfacelights( "r_deferred_convert_surfacelights", "1", 0, "Also convert texture lights (lights.rad)" );
ConVar r_deferred_convert_max( "r_deferred_convert_max", "384", 0, "Most compiled lights converted per map" );

struct WorldLightCandidate_t
{
	const dworldlight_t *pLight;
	float flStrength;	// linear intensity at 100 units
};

static int WorldLightSort( const WorldLightCandidate_t *a, const WorldLightCandidate_t *b )
{
	return ( a->flStrength > b->flStrength ) ? -1 : ( ( a->flStrength < b->flStrength ) ? 1 : 0 );
}

static void ConvertWorldLights( const dworldlight_t *lights, int lightCount, const CUtlVector<Vector> &skipPositions )
{
	if ( !r_deferred_convert_worldlights.GetBool() || !lights || lightCount <= 0 )
		return;

	CUtlVector<WorldLightCandidate_t> candidates;
	for ( int i = 0; i < lightCount; i++ )
	{
		const dworldlight_t &light = lights[i];
		const bool bSurface = light.type == emit_surface;
		if ( light.type != emit_point && light.type != emit_spotlight && !( bSurface && r_deferred_convert_surfacelights.GetBool() ) )
			continue;

		// named lights still exist as entities and were converted with their I/O
		bool bSkip = false;
		for ( int j = 0; j < skipPositions.Count(); j++ )
		{
			if ( skipPositions[j].DistToSqr( light.origin ) < 8.0f * 8.0f )
			{
				bSkip = true;
				break;
			}
		}
		if ( bSkip )
			continue;

		// strength at 100 units (texture lights fall off with 1 / d^2)
		const float flMax = Max( light.intensity.x, Max( light.intensity.y, light.intensity.z ) );
		float flRatio = bSurface ? 100.0f * 100.0f : ( light.constant_attn + 100.0f * light.linear_attn + 100.0f * 100.0f * light.quadratic_attn );
		if ( flRatio <= 0.0f )
			flRatio = 1.0f;
		const float flStrength = flMax / flRatio;
		if ( flStrength < ( bSurface ? 0.04f : 0.02f ) )
			continue;

		WorldLightCandidate_t &c = candidates[ candidates.AddToTail() ];
		c.pLight = &light;
		c.flStrength = flStrength;
	}

	candidates.Sort( WorldLightSort );
	const int nMax = Min( candidates.Count(), Max( 0, r_deferred_convert_max.GetInt() ) );

	int nPoint = 0, nSpot = 0, nSurface = 0;
	for ( int i = 0; i < nMax; i++ )
	{
		const dworldlight_t &light = *candidates[i].pLight;
		const bool bSurface = light.type == emit_surface;
		const bool bSpot = light.type == emit_spotlight || bSurface;

		// radius from the compiled attenuation, texture lights from their 1 / d^2 falloff
		float flRadius;
		if ( bSurface )
		{
			const float flMax = Max( light.intensity.x, Max( light.intensity.y, light.intensity.z ) );
			flRadius = sqrtf( flMax / 0.06f );
		}
		else
		{
			flRadius = ComputeLightRadius( light );
		}
		flRadius = clamp( flRadius, 96.0f, 1600.0f );

		// deferred falloff is ( 1 - d / r )^2: match the compiled light at 100 units
		const float flRef = Min( 100.0f, flRadius * 0.5f );
		const float flFalloffAtRef = ( 1.0f - flRef / flRadius ) * ( 1.0f - flRef / flRadius );
		float flRatio = bSurface ? flRef * flRef : ( light.constant_attn + flRef * light.linear_attn + flRef * flRef * light.quadratic_attn );
		if ( flRatio <= 0.0f )
			flRatio = 1.0f;
		Vector col = light.intensity * ( 1.0f / ( flRatio * Max( flFalloffAtRef, 0.05f ) ) );
		const float flColMax = Max( col.x, Max( col.y, col.z ) );
		if ( flColMax > 4.0f )
			col *= 4.0f / flColMax;

		// texture lights emit from their surface: a wide spot a little in front of it
		Vector pos = light.origin;
		if ( bSurface )
			pos += light.normal * 6.0f;
		QAngle ang( 0, 0, 0 );
		if ( bSpot )
			VectorAngles( light.normal, ang );

		CDeferredLight *lightEntity = static_cast<CDeferredLight*>( CBaseEntity::CreateNoSpawn( "light_deferred", pos, ang ) );
		if ( !lightEntity )
			break;

		lightEntity->KeyValue( GetLightParamName( LPARAM_DIFFUSE ), UTIL_VarArgs( "%f %f %f 255", col.x * 255.0f, col.y * 255.0f, col.z * 255.0f ) );
		lightEntity->KeyValue( GetLightParamName( LPARAM_POWER ), 2.0f );
		lightEntity->KeyValue( GetLightParamName( LPARAM_RADIUS ), flRadius );
		lightEntity->KeyValue( GetLightParamName( LPARAM_VIS_DIST ), flRadius * 3.0f + 512.0f );
		lightEntity->KeyValue( GetLightParamName( LPARAM_VIS_RANGE ), flRadius * 1.5f + 256.0f );
		lightEntity->KeyValue( GetLightParamName( LPARAM_SHADOW_DIST ), flRadius * 1.5f );
		lightEntity->KeyValue( GetLightParamName( LPARAM_SHADOW_RANGE ), flRadius * 0.75f );

		if ( bSpot )
		{
			lightEntity->KeyValue( GetLightParamName( LPARAM_LIGHTTYPE ), "1" );
			float flOuter, flInner;
			if ( bSurface )
			{
				flOuter = 165.0f;
				flInner = 90.0f;
			}
			else
			{
				flOuter = clamp( 2.0f * RAD2DEG( acosf( clamp( light.stopdot2, -1.0f, 1.0f ) ) ), 2.0f, 175.0f );
				flInner = clamp( 2.0f * RAD2DEG( acosf( clamp( light.stopdot, -1.0f, 1.0f ) ) ), 1.0f, flOuter );
			}
			lightEntity->KeyValue( GetLightParamName( LPARAM_SPOTCONE_OUTER ), flOuter );
			lightEntity->KeyValue( GetLightParamName( LPARAM_SPOTCONE_INNER ), flInner );
		}
		else
		{
			lightEntity->KeyValue( GetLightParamName( LPARAM_LIGHTTYPE ), "0" );
		}

		// shadows for the lights that matter: bright, with some reach; texture lights are many
		const float flColMaxFinal = Max( col.x, Max( col.y, col.z ) );
		int iFlags = DEFLIGHT_ENABLED;
		if ( !bSurface && ( bSpot ? ( flColMaxFinal > 0.35f && flRadius > 200.0f ) : ( flColMaxFinal > 1.0f && flRadius > 400.0f ) ) )
			iFlags |= DEFLIGHT_SHADOW_ENABLED;

		// switchable lights (style >= 32) belong to named entities; low styles are patterns
		const int iStyle = light.style;
		if ( iStyle > 0 && iStyle < 32 )
		{
			float flAmt = 0.4f, flSpeed = 5.0f, flSmooth = 0.3f, flRandom = 1.0f;
			if ( iStyle == 10 || iStyle == 13 )
			{
				flAmt = 0.8f; flSpeed = 12.0f; flSmooth = 0.0f;
			}
			else if ( iStyle == 2 || iStyle == 5 || iStyle == 7 || iStyle == 8 )
			{
				flAmt = 0.4f; flSpeed = 1.0f; flSmooth = 1.0f; flRandom = 0.0f;
			}
			iFlags |= DEFLIGHT_LIGHTSTYLE_ENABLED;
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_AMT ), flAmt );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SPEED ), flSpeed );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SMOOTH ), flSmooth );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_RANDOM ), flRandom );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SEED ), RandomInt( 0, DEFLIGHT_SEED_MAX ) );
		}
		lightEntity->KeyValue( "spawnflags", UTIL_VarArgs( "%d", iFlags ) );

		DispatchSpawn( lightEntity );

		if ( bSurface )
			nSurface++;
		else if ( bSpot )
			nSpot++;
		else
			nPoint++;
	}

	DevMsg( "CDeferredManagerServer: converted %d compiled lights (%d point, %d spot, %d texture) of %d candidates\n",
		nPoint + nSpot + nSurface, nPoint, nSpot, nSurface, candidates.Count() );
}

void CDeferredManagerServer::LevelInitPreEntity()
{
	if ( gpGlobals->eLoadType == MapLoad_LoadGame )
		return;

	if ( !r_deferred.GetBool() )
		return;

	if (CommandLine() && CommandLine()->FindParm( "-nodeferred" ) != 0)
		return;

	const char* entStr = engine->GetMapEntitiesString();

	// HL2RPM: a map lit for the deferred renderer by hand (light_deferred) keeps its lights.
	// Otherwise the classic lamps are converted even if the map has its own global light
	// (such maps used to stay dark wherever the lamps were: only the sun was deferred).
	if ( V_stristr( entStr, "\"light_deferred\"" ) )
	{
		if ( DeferredVerboseLevel() >= 1 )
			DevMsg( "CDeferredManagerServer: map has light_deferred entities, skipping auto-conversion.\n" );
		return;
	}
	const bool bMapHasGlobalLight = V_stristr( entStr, "\"light_deferred_global\"" ) != NULL;

	// HL2RPM: MapName() is the bare map name ("d1_trainstation_01"), the open always failed
	// and nothing was ever converted
	char szBSPPath[MAX_PATH];
	const char *pszMapName = MapName();
	if ( V_stristr( pszMapName, ".bsp" ) )
		V_strncpy( szBSPPath, pszMapName, sizeof( szBSPPath ) );
	else
		V_snprintf( szBSPPath, sizeof( szBSPPath ), "maps/%s.bsp", pszMapName );

	const FileHandle_t hFile = g_pFullFileSystem->Open( szBSPPath, "rb", "GAME" );
	if ( !hFile )
	{
		Warning( "CDeferredManagerServer: can't open %s, classic lights are not converted\n", szBSPPath );
		return;
	}

	dheader_t header;
	g_pFullFileSystem->Read( &header, sizeof(dheader_t), hFile );

	// HL2RPM: maps compiled with HDR only have their lights in the HDR lump
	const bool bUseHDRLights = header.lumps[LUMP_WORLDLIGHTS].filelen == 0 && header.lumps[LUMP_WORLDLIGHTS_HDR].filelen > 0;
	const lump_t &lightLump = header.lumps[ bUseHDRLights ? LUMP_WORLDLIGHTS_HDR : LUMP_WORLDLIGHTS ];
	dworldlight_t* lights;
	size_t lightCount;

	/*
	if ( lightLump.uncompressedSize )
	{
		byte* compressed = new byte[lightLump.filelen];
		g_pFullFileSystem->Seek( hFile, lightLump.fileofs, FILESYSTEM_SEEK_HEAD );
		g_pFullFileSystem->Read( compressed, lightLump.filelen, hFile );

		if ( CLZMA::IsCompressed( compressed ) )
		{
			if ( lightLump.uncompressedSize % sizeof( dworldlight_t ) )
				return;

			lightCount = lightLump.uncompressedSize / sizeof( dworldlight_t );
			lights = new dworldlight_t[lightCount];
			CLZMA::Uncompress( compressed, reinterpret_cast<byte*>( lights ) );
		}
		else
			Error( "Compressed lump that isn't compressed?" );

		delete[] compressed;
	}
	else
	{
	*/
		if ( lightLump.filelen % sizeof( dworldlight_t ) )
			return;

		g_pFullFileSystem->Seek( hFile, lightLump.fileofs, FILESYSTEM_SEEK_HEAD );

		lightCount = lightLump.filelen / sizeof( dworldlight_t );

		lights = new dworldlight_t[lightCount];
		g_pFullFileSystem->Read( lights, lightLump.filelen, hFile );
	//}

	g_pFullFileSystem->Close( hFile );

	KeyValuesAD vmfFile( "vmf" );

	char szTokenBuffer[MAPKEY_MAXLENGTH];
	for ( ; true; entStr = MapEntity_SkipToNextEntity( entStr, szTokenBuffer ) )
	{
		char token[MAPKEY_MAXLENGTH];
		entStr = MapEntity_ParseToken( entStr, token );

		if ( !entStr )
			break;

		if ( token[0] != '{' )
		{
			Error( "MapEntity_ParseAllEntities: found %s when expecting {", token );
			continue;
		}

		CEntityMapData entData( (char*)entStr );

		char className[MAPKEY_MAXLENGTH];
		entData.ExtractValue( "classname", className );
		int iType;
		if ( FStrEq( className, "light" ) )
			iType = 0;
		else if ( FStrEq( className, "light_spot" ) )
			iType = 1;
		else if ( FStrEq( className, "point_spotlight" ) )
			iType = 2;
		else if ( FStrEq( className, "light_environment" ) )
			iType = 3;
		else
			continue;

		char keyName[MAPKEY_MAXLENGTH];
		char value[MAPKEY_MAXLENGTH];
		KeyValues* pSubKey = new KeyValues( className );
		if ( entData.GetFirstKey( keyName, value ) )
		{
			do
			{
				pSubKey->SetString( keyName, value );
			}
			while ( entData.GetNextKey( keyName, value ) );
		}
		pSubKey->SetInt( "light_type", iType );
		vmfFile->AddSubKey( pSubKey );
	}

	KeyValuesDumpAsDevMsg( vmfFile, 0, 4 );

	struct SpotLightPair_t
	{
		KeyValues* spotlight;
		KeyValues* light;
	};

	CUtlVector<SpotLightPair_t> spotLightPairs;

	// Find light_spot and point_spotlight entities at same place
	FOR_EACH_TRUE_SUBKEY( vmfFile, entity1 )
	{
		const int type1 = entity1->GetInt( "light_type" );
		if ( type1 != 1 )
			continue;
		bool bSkip = false;
		const int numPairs = spotLightPairs.Count();
		for ( int i = 0; i < numPairs; ++i )
		{
			const SpotLightPair_t& pair = spotLightPairs[i];
			if (pair.light == entity1)
			{
				bSkip = true;
				break;
			}
		}
		if ( bSkip )
			continue;

		Vector pos1;
		QAngle rot1;
		UTIL_StringToVector( pos1.Base(), entity1->GetString( "origin" ) );
		UTIL_StringToVector( rot1.Base(), entity1->GetString( "angles" ) );

		FOR_EACH_TRUE_SUBKEY( vmfFile, entity2 )
		{
			const int type2 = entity2->GetInt( "light_type" );
			if ( type2 != 2 )
				continue;

			if (entity1 == entity2)
				continue;

			bool bSkip2 = false;
			const int numPairs2 = spotLightPairs.Count();
			for ( int i = 0; i < numPairs2; ++i )
			{
				const SpotLightPair_t& pair = spotLightPairs[i];
				if (pair.spotlight == entity2)
				{
					bSkip2 = true;
					break;
				}
			}
			if (bSkip2)
				continue;


			Vector pos2;
			QAngle rot2;
			UTIL_StringToVector( pos2.Base(), entity2->GetString( "origin" ) );
			UTIL_StringToVector( rot2.Base(), entity2->GetString( "angles" ) );

			if ( CloseEnough( pos1.x, pos2.x, 2.f ) && CloseEnough( pos1.y, pos2.y, 2.f ) && CloseEnough( pos1.z, pos1.z, 2.f ) && CloseEnough( rot1.y, rot2.y, 2.f ) && CloseEnough( rot1.z, rot2.z, 2.f ) )
			{
				DevMsg(1, "Found matching lights at positions %f %f %f and %f %f %f of types %d and %d\n", pos1.x, pos1.y, pos1.z, pos2.x, pos2.y, pos2.z, type1, type2 );
				SpotLightPair_t& pair = spotLightPairs[spotLightPairs.AddToTail()];
				pair.light = type1 == 1 ? entity1 : entity2;
				pair.spotlight = type1 != 1 ? entity1 : entity2;
				break;
			}
		}
	}

#define COPY_LIGHT_DATA( name ) pair.spotlight->SetString( name, pair.light->GetString( name ) )

	const int numPairs = spotLightPairs.Count();
	for ( int i = 0; i < numPairs; ++i )
	{
		SpotLightPair_t& pair = spotLightPairs[i];
		vmfFile->RemoveSubKey( pair.light );
		COPY_LIGHT_DATA( "_light" );
		COPY_LIGHT_DATA( "_exponent" );
		COPY_LIGHT_DATA( "_cone" );
		COPY_LIGHT_DATA( "_inner_cone" );
		COPY_LIGHT_DATA( "_constant_attn" );
		COPY_LIGHT_DATA( "_linear_attn" );
		COPY_LIGHT_DATA( "_quadratic_attn" );
		pair.spotlight->SetInt( "light_type", 5 );
		pair.light->deleteThis();
	}

	const char* szParamDiffuse = GetLightParamName( LPARAM_DIFFUSE );
	const char* szParamLightType = GetLightParamName( LPARAM_LIGHTTYPE );
	const char* szParamSpotConeInner = GetLightParamName( LPARAM_SPOTCONE_INNER );
	const char* szParamSpotConeOuter = GetLightParamName( LPARAM_SPOTCONE_OUTER );
	const char* szParamPower = GetLightParamName( LPARAM_POWER );
	const char* szParamRadius = GetLightParamName( LPARAM_RADIUS );
	const char* szParamVisDist = GetLightParamName( LPARAM_VIS_DIST );
	const char* szParamVisRange = GetLightParamName( LPARAM_VIS_RANGE );
	const char* szParamShadowDist = GetLightParamName( LPARAM_SHADOW_DIST );
	const char* szParamShadowRange = GetLightParamName( LPARAM_SHADOW_RANGE );
	const char* szParamVolumeSamples = GetLightParamName( LPARAM_VOLUME_SAMPLES );

	bool bCreatedGlobalLight = bMapHasGlobalLight;
	CUtlVector<Vector> convertedPositions;
	//CUtlVector<const dworldlight_t*> unspawnedLights;
	FOR_EACH_TRUE_SUBKEY( vmfFile, entity )
	{
		const int type = entity->GetInt( "light_type" );
		if ( type == -1 || ( type == 3 && bCreatedGlobalLight ))
			continue;

		Vector pos;
		QAngle rot;
		UTIL_StringToVector( pos.Base(), entity->GetString( "origin" ) );
		if ( KeyValues* angle = entity->FindKey( "angles" ) ) {
			UTIL_StringToVector( rot.Base(), angle->GetString() );
			if (type == 1 && entity->GetInt("pitch") != 0)
				rot.x = -entity->GetInt("pitch");

		}
		else
			rot = vec3_angle;

		if ( type == 3 )
		{
			rot.x = -entity->GetInt( "pitch" );
			CDeferredLightGlobal* lightEntity = static_cast<CDeferredLightGlobal*>( CBaseEntity::CreateNoSpawn( "light_deferred_global", pos, rot ) );
			if ( !lightEntity )
				break;

			int color[4], ambient[4];
			UTIL_StringToIntArray( color, 4, entity->GetString( "_light" ) );
			UTIL_StringToIntArray( ambient, 4, entity->GetString( "_ambient" ) );

			const float ds = r_deferred_autoenvlight_diffuse_intensity.GetFloat();
			const float asl = r_deferred_autoenvlight_ambient_intensity_low.GetFloat();
			const float ash = r_deferred_autoenvlight_ambient_intensity_high.GetFloat();

			lightEntity->KeyValue( "diffuse", UTIL_VarArgs("%d %d %d %f", color[0], color[1], color[2], color[3] * ds ) );
			lightEntity->KeyValue( "ambient_high", UTIL_VarArgs("%d %d %d %f", ambient[0], ambient[1], ambient[2], ambient[3] * ash ) );
			lightEntity->KeyValue( "ambient_low", UTIL_VarArgs("%d %d %d %f", ambient[0], ambient[1], ambient[2], ambient[3] * asl ) );
			lightEntity->KeyValue( "spawnflags", "3" );

			if ( DeferredVerboseLevel() >= 1 )
			{
				DevMsg( "CDeferredManagerServer: auto-create light_deferred_global at (%.1f %.1f %.1f) ang=(%.1f %.1f %.1f) diffuse=%s ambient=%s\n",
					XYZ( pos ), XYZ( rot ),
					entity->GetString( "_light" ),
					entity->GetString( "_ambient" ) );
			}

			DispatchSpawn( lightEntity );

			bCreatedGlobalLight = true;
			continue;
		}

		// HL2RPM: the compiled light at the same place (it used to take the first point
		// light of the map for every entity) gives the exact radius and cone
		for ( uint i = 0; i < lightCount; ++i )
		{
			const dworldlight_t& light = lights[i];
			if ( light.type != emit_spotlight && light.type != emit_point )
				continue;
			if ( light.origin.DistToSqr( pos ) > 4.0f * 4.0f )
				continue;

			if ( light.type == emit_point )
			{
				entity->SetFloat( "_distance", ComputeLightRadius( light ) );
				break;
			}

			QAngle ang;
			VectorAngles( light.normal, ang );
			rot = ang;
			entity->SetFloat( "_inner_cone", acos( light.stopdot ) * 180.f / M_PI_F );
			entity->SetFloat( "_cone", acos( light.stopdot2 ) * 180.f / M_PI_F );
			entity->SetFloat( "_distance", ComputeLightRadius( light ) );
			break;
		}

		//out:
		CDeferredLight* lightEntity = static_cast<CDeferredLight*>( CBaseEntity::CreateNoSpawn( "light_deferred", pos, rot ) );
		if ( !lightEntity )
			break;

		int color[4];
		UTIL_StringToIntArray( color, 4, entity->GetString( type == 2 ? "rendercolor" : "_light" ) );
		if ( type == 5 )
			color[3] = 255;

		char string[256];
			V_sprintf_safe( string, "%d %d %d %d", color[0], color[1], color[2], color[3] );
		lightEntity->KeyValue( szParamDiffuse, string );

		// classic "Initially dark" (light, light_spot) / "Start on" (point_spotlight)
		const int iClassicFlags = entity->GetInt( "spawnflags" );
		const bool bStartOn = ( type == 2 || type == 5 ) ? ( iClassicFlags & 1 ) != 0 : ( iClassicFlags & 1 ) == 0;
		int iDefFlags = bStartOn ? DEFLIGHT_ENABLED : 0;
		if ( type == 2 || type == 5 )
			iDefFlags |= DEFLIGHT_VOLUMETRICS_ENABLED;
		// Shadows: spots render one view, a point light six (cube map). Dim fill lights, which
		// are most of the point lights of a map, don't get any.
		{
			const float flI0 = ( Max( color[0], Max( color[1], color[2] ) ) / 255.0f ) * ( color[3] / 255.0f );
			const bool bSpotType = ( type == 1 || type == 2 || type == 5 );
			if ( bSpotType ? flI0 > 0.25f : flI0 > 0.8f )
				iDefFlags |= DEFLIGHT_SHADOW_ENABLED;
		}

		// classic light styles: flickering fluorescent tubes, pulsing lamps, candles...
		const int iStyle = entity->GetInt( "style" );
		float flStyleAmt = 0.0f, flStyleSpeed = 0.0f, flStyleSmooth = 0.0f, flStyleRandom = 0.0f;
		switch ( iStyle )
		{
		case 1: case 6: case 11: case 12:	// flicker
			flStyleAmt = 0.45f; flStyleSpeed = 6.0f; flStyleSmooth = 0.2f; flStyleRandom = 1.0f; break;
		case 10: case 13:					// fluorescent flicker, fast strobe
			flStyleAmt = 0.8f; flStyleSpeed = 12.0f; flStyleSmooth = 0.0f; flStyleRandom = 1.0f; break;
		case 2: case 5: case 7: case 8:		// slow / gentle pulse
			flStyleAmt = 0.4f; flStyleSpeed = ( iStyle == 2 ) ? 0.6f : 1.2f; flStyleSmooth = 1.0f; flStyleRandom = 0.0f; break;
		case 3: case 4: case 9:				// candles, slow strobe
			flStyleAmt = 0.3f; flStyleSpeed = 3.0f; flStyleSmooth = 0.6f; flStyleRandom = 0.8f; break;
		}
		if ( flStyleAmt > 0.0f )
		{
			iDefFlags |= DEFLIGHT_LIGHTSTYLE_ENABLED;
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_AMT ), flStyleAmt );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SPEED ), flStyleSpeed );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SMOOTH ), flStyleSmooth );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_RANDOM ), flStyleRandom );
			lightEntity->KeyValue( GetLightParamName( LPARAM_STYLE_SEED ), RandomInt( 0, DEFLIGHT_SEED_MAX ) );
		}

		lightEntity->KeyValue( "spawnflags", UTIL_VarArgs( "%d", iDefFlags ) );
		if ( type == 1 || type == 5 )
		{
			lightEntity->KeyValue( szParamLightType, "1" );
			// classic cones are half angles, deferred lights take the full cone angle
			lightEntity->KeyValue( szParamSpotConeInner, entity->GetFloat( "_inner_cone" ) * 2.0f );
			lightEntity->KeyValue( szParamSpotConeOuter, entity->GetFloat( "_cone" ) * 2.0f );
			lightEntity->KeyValue( szParamPower, entity->GetFloat( "_exponent", 1.f ) );
			if ( type == 5 )
				lightEntity->KeyValue( szParamVolumeSamples, 50 );
			}
		else if ( type == 2 )
		{
			lightEntity->KeyValue( szParamLightType, "1" );
			const float width = entity->GetFloat( "spotlightwidth" );
			lightEntity->KeyValue( szParamSpotConeInner, width );
			lightEntity->KeyValue( szParamSpotConeOuter, width * 1.5f );
			lightEntity->KeyValue( szParamPower, 1 );
			lightEntity->KeyValue( szParamVolumeSamples, 50 );
		}
		else
		{
			lightEntity->KeyValue( szParamLightType, "0" );
			lightEntity->KeyValue( szParamPower, "1" );
		}

		const float radius = type == 2 ? entity->GetFloat( "spotlightlength" ) : ComputeEntityLightRadius( entity, color );
		lightEntity->KeyValue( szParamRadius, radius );
		lightEntity->KeyValue( szParamVisDist, radius * 2 );
		lightEntity->KeyValue( szParamVisRange, radius * 1.25f );
		lightEntity->KeyValue( szParamShadowDist, radius * ( 5.f / 6.f ) );
		lightEntity->KeyValue( szParamShadowRange, radius * ( 2.f / 3.f ) );

		if ( KeyValues* targetname = entity->FindKey( "targetname" ) )
			lightEntity->KeyValue( "targetname", targetname->GetString() );

		if ( KeyValues* parent = entity->FindKey( "parentname" ) )
			lightEntity->KeyValue( "parentname", parent->GetString() );

		if ( DeferredVerboseLevel() >= 1 )
		{
			DevMsg( "CDeferredManagerServer: auto-create light_deferred at (%.1f %.1f %.1f) ang=(%.1f %.1f %.1f) srcType=%d defType=%s radius=%.1f flags=%s\n",
				XYZ( pos ), XYZ( rot ),
				type,
				(type == 1 || type == 2 || type == 5) ? "spot" : "point",
				radius,
				(type == 2 || type == 5) ? "11" : "3" );
		}

		DispatchSpawn( lightEntity );
		convertedPositions.AddToTail( pos );
	}

	DevMsg( "CDeferredManagerServer: converted %d light entities (%d compiled lights in the map)\n", convertedPositions.Count(), (int)lightCount );

	// HL2RPM: the compiler removes unnamed light / light_spot entities and texture lights
	// (lights.rad: fluorescent panels, lamps' bulbs...) never had one: they only exist in
	// the world lights lump. Convert those too, or the deferred interiors stay dark.
	ConvertWorldLights( lights, (int)lightCount, convertedPositions );

	/*
	const int numUnspawnedLights = unspawnedLights.Count();
	for ( int i = 0; i < numUnspawnedLights; ++i )
	{
		const dworldlight_t* light = unspawnedLights[i];
		const float radius = ComputeLightRadius( *light );

		if ( radius == 0 )
			continue;

		CDeferredLight* lightEntity = static_cast<CDeferredLight*>( CBaseEntity::CreateNoSpawn( "light_deferred", light->origin, vec3_angle ) );
		if ( !lightEntity )
			break;

		Vector intensity = light->intensity;
		const float ratio = light->constant_attn + 100 * light->linear_attn + 100 * 100 * light->quadratic_attn;
		if ( ratio > 0 )
			VectorScale( light->intensity, 1.f / ratio, intensity );
		intensity *= 255.f;

		char string[256];
		V_sprintf_safe( string, "%f %f %f 255", intensity.x, intensity.y, intensity.z );
		lightEntity->KeyValue( szParamDiffuse, string );

		lightEntity->KeyValue( "spawnflags", "3" );
		if ( light->type == emit_spotlight )
		{
			QAngle angle;
			VectorAngles( light->normal, angle );
			lightEntity->SetAbsAngles( angle );
			lightEntity->KeyValue( szParamLightType, "1" );
			lightEntity->KeyValue( szParamSpotConeInner, acos( light->stopdot ) * 180.f / M_PI_F );
			lightEntity->KeyValue( szParamSpotConeOuter, acos( light->stopdot2 ) * 180.f / M_PI_F );
			lightEntity->KeyValue( szParamPower, light->exponent );
		}
		else
		{
			lightEntity->KeyValue( szParamLightType, "0" );
			lightEntity->KeyValue( szParamPower, "1" );
		}

		lightEntity->KeyValue( szParamRadius, radius );
		lightEntity->KeyValue( szParamVisDist, radius * 2 );
		lightEntity->KeyValue( szParamVisRange, radius * 1.25f );
		lightEntity->KeyValue( szParamShadowDist, radius * ( 5.f / 6.f ) );
		lightEntity->KeyValue( szParamShadowRange, radius * ( 2.f / 3.f ) );

		DispatchSpawn( lightEntity );
	}
	*/

	delete[] lights;
}

int CDeferredManagerServer::AddCookieTexture( const char *pszCookie )
{
	Assert( g_pStringTable_LightCookies != NULL );

	return  g_pStringTable_LightCookies->AddString( true, pszCookie );
}

void CDeferredManagerServer::AddWorldLight( CDeferredLight *l )
{
	CDeferredLightContainer *pC = FindAvailableContainer();

	if ( !pC )
		pC = assert_cast< CDeferredLightContainer* >( CreateEntityByName( "deferred_light_container" ) );

	pC->AddWorldLight( l );
}
