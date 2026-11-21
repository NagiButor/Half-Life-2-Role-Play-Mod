
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "tier0/icommandline.h"

#include "tier0/memdbgon.h"


INetworkStringTable *g_pStringTable_LightCookies = NULL;

static const char *g_pszLightParamNames[ LPARAM_COUNT ] =
{
	"diffuse",
	"ambient",
	"radius",
	"power",
	"spot_cone_inner",
	"spot_cone_outer",
	"vis_dist",
	"vis_range",
	"shadow_dist",
	"shadow_range",
	"light_type",
	"cookietex",
	"style_amt",
	"style_speed",
	"style_smooth",
	"style_random",
	"style_seed",

	"spawnFlags",
	"angles",
	"origin",

	"ambient_low",
	"ambient_high",

#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	"volume_lod0_dist",
	"volume_lod1_dist",
	"volume_lod2_dist",
	"volume_lod3_dist",
#endif
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	"volume_samples",
#endif
};

const char *GetLightParamName( LIGHT_PARAM_ID id )
{
	Assert( id >= 0 && id < LPARAM_COUNT );

	return g_pszLightParamNames[ id ];
}

// HL2RPM: CDeferredManagerClient::Init and CDeferredManagerServer::Init run before
// config.cfg is executed by the engine, so read the archived r_deferred choice directly.
bool Deferred_ReadInitialEnabled()
{
	if ( CommandLine() )
	{
		if ( CommandLine()->FindParm( "-nodeferred" ) != 0 )
			return false;
		const char *pszVal = NULL;
		if ( CommandLine()->CheckParm( "+r_deferred", &pszVal ) && pszVal && pszVal[0] )
			return atoi( pszVal ) != 0;
	}

	if ( g_pFullFileSystem )
	{
		CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
		if ( g_pFullFileSystem->ReadFile( "cfg/config.cfg", "MOD", buf ) )
		{
			buf.PutChar( 0 );
			const char *pszLine = (const char *)buf.Base();
			bool bFound = false;
			bool bEnabled = true;
			while ( pszLine && *pszLine )
			{
				while ( *pszLine == ' ' || *pszLine == '\t' || *pszLine == '\r' || *pszLine == '\n' )
					pszLine++;
				if ( !*pszLine )
					break;
				if ( !Q_strnicmp( pszLine, "r_deferred", 10 ) )
				{
					const char cNext = pszLine[10];
					if ( cNext == ' ' || cNext == '\t' || cNext == '"' )
					{
						const char *pVal = pszLine + 10;
						while ( *pVal == ' ' || *pVal == '\t' || *pVal == '"' )
							pVal++;
						bEnabled = ( atoi( pVal ) != 0 );
						bFound = true;
					}
				}
				const char *pszEnd = strchr( pszLine, '\n' );
				pszLine = pszEnd ? ( pszEnd + 1 ) : NULL;
			}
			if ( bFound )
				return bEnabled;
		}
	}

	return true;
}
