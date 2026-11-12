//========= HL2RPM ============================================================//
//
// Keys of the mod's own commands: a config.cfg written before a command existed has
// no key for it. Once a map is up (config.cfg has run by then) a command without a key
// gets its default, if that key is free or holds something useless in single player;
// a key the player gave it himself stays.
//
//=============================================================================//
#include "cbase.h"
#include "igamesystem.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

struct RPMDefaultKey_t
{
	const char *pszCommand;
	const char *pszKey;
};
static const RPMDefaultKey_t s_Keys[] =
{
	{ "rpm_holster", "h" },
};

class CRPMKeys : public CAutoGameSystem
{
public:
	CRPMKeys() : CAutoGameSystem( "CRPMKeys" ) {}

	virtual void LevelInitPostEntity()
	{
		for ( int i = 0; i < ARRAYSIZE( s_Keys ); i++ )
		{
			if ( engine->Key_LookupBinding( s_Keys[i].pszCommand ) )
				continue;
			engine->ClientCmd_Unrestricted( CFmtStr( "bind \"%s\" \"%s\"\n", s_Keys[i].pszKey, s_Keys[i].pszCommand ) );
		}
	}
};
static CRPMKeys s_RPMKeys;
