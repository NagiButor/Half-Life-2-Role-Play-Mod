//========= HL2RPM ============================================================//
//
// Zombies that do not all look the same (the user's addon "Headcrab Zombie Mod Hexed Models":
// models/zombie_new/*). A zombie with the stock model takes the addon's model of its kind and a
// random skin - a citizen, a rebel in green or blue, a medic, a cop, an elite soldier... The
// skins of these models switch whole clothing layers on and off, so a skin is the whole look.
// The torso it becomes and the legs that fall off are the addon's torso / legs with the same
// clothes (the legs have fewer skins: a table maps them).
//
// A kind can have more than one body (2026-11-02): the hazmat worker - a classic zombie of our own
// (hl2rpm/characters/zombie_hazmat, make_zombie_hazmat.py) and the addon's Zombine in the suit
// (models/zombie/zombie_hl2_combine_hazmat) - and the Combine worker of the other addon; "share"
// is how often a body comes among those of its kind. Their torso and legs are the stock ones.
//
// A zombie given a model by the map or the Creation Kit keeps it. sv_rpm_zombie_variants 0 - stock.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_zombies.h"
#include "baseanimating.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sv_rpm_zombie_variants( "sv_rpm_zombie_variants", "1", FCVAR_ARCHIVE, "HL2RPM: zombies take random clothes (models/zombie_new); 0 = the stock models" );

struct ZombieVariant_t
{
	int kind;
	int share;				// how often this body comes among the bodies of its kind
	const char *stock;
	const char *body;
	const char *torso;		// NULL: the stock torso / legs of the class
	const char *legs;
	int skins;
	int weights[12];		// how often each skin comes
	int legsSkin[12];		// the skin of the legs for each body skin
};

static const ZombieVariant_t s_Variants[] =
{
	// skins: 0 the stock zombie, 1 citizen, 2 rebel (green), 3 rebel (blue), 4 medic (grey), 5 medic (blue), 6 civil protection
	{ RPMZ_CLASSIC, 80, "models/zombie/classic.mdl", "models/zombie_new/classic_new.mdl", "models/zombie_new/classic_new_torso.mdl", "models/zombie_new/classic_new_legs.mdl",
		7, { 3, 4, 2, 2, 1, 1, 2 }, { 0, 0, 1, 1, 2, 2, 3 } },
	// the hazmat worker (only the bloody suit); the Combine worker: three uniforms
	{ RPMZ_CLASSIC, 12, "models/zombie/classic.mdl", "models/hl2rpm/characters/zombie_hazmat.mdl", NULL, NULL,
		1, { 1 }, { 0 } },
	{ RPMZ_CLASSIC, 8, "models/zombie/classic.mdl", "models/puntleycmb/zombie_worker.mdl", NULL, NULL,
		3, { 2, 1, 1 }, { 0 } },
	// 0 soldier, 1 shotgunner, 2 Nova Prospekt guard, 3 elite
	{ RPMZ_ZOMBINE, 85, "models/zombie/zombie_soldier.mdl", "models/zombie_new/zombie_soldier_new.mdl", "models/zombie_new/zombie_soldier_new_torso.mdl", "models/zombie_new/zombie_soldier_new_legs.mdl",
		4, { 4, 3, 2, 1 }, { 0, 1, 2, 3 } },
	// the Zombine in a hazmat suit (the addon's)
	{ RPMZ_ZOMBINE, 15, "models/zombie/zombie_soldier.mdl", "models/zombie/zombie_hl2_combine_hazmat.mdl", NULL, NULL,
		1, { 1 }, { 0 } },
	// 0 the stock one, 1 rebel, 2 medic, 3 cop, 4 soldier, 5 shotgunner, 6 guard, 7 elite
	{ RPMZ_POISON, 100, "models/zombie/poison.mdl", "models/zombie_new/poison_new.mdl", NULL, NULL,
		8, { 4, 3, 2, 2, 2, 1, 1, 1 }, { 0 } },
};

static bool HasBody( const ZombieVariant_t &v )
{
	return filesystem->FileExists( v.body, "GAME" );
}

// a body of that kind, by the shares of those that are in the game
static const ZombieVariant_t *PickVariant( int kind )
{
	int total = 0;
	for ( int i = 0; i < ARRAYSIZE( s_Variants ); i++ )
		if ( s_Variants[i].kind == kind && HasBody( s_Variants[i] ) )
			total += s_Variants[i].share;
	if ( total <= 0 )
		return NULL;
	int r = RandomInt( 0, total - 1 );
	for ( int i = 0; i < ARRAYSIZE( s_Variants ); i++ )
	{
		const ZombieVariant_t &v = s_Variants[i];
		if ( v.kind != kind || !HasBody( v ) )
			continue;
		if ( r < v.share )
			return &v;
		r -= v.share;
	}
	return NULL;
}

// the variant a zombie wears now (its body, or the torso it has become)
static const ZombieVariant_t *VariantOf( CBaseAnimating *pZombie )
{
	if ( !pZombie || pZombie->GetModelName() == NULL_STRING )
		return NULL;
	const char *model = STRING( pZombie->GetModelName() );
	for ( int i = 0; i < ARRAYSIZE( s_Variants ); i++ )
	{
		const ZombieVariant_t &v = s_Variants[i];
		if ( !Q_stricmp( model, v.body ) || ( v.torso && !Q_stricmp( model, v.torso ) ) )
			return &v;
	}
	return NULL;
}

void RPMZombie_Precache( int kind )
{
	if ( !sv_rpm_zombie_variants.GetBool() )
		return;
	for ( int i = 0; i < ARRAYSIZE( s_Variants ); i++ )
	{
		const ZombieVariant_t &v = s_Variants[i];
		if ( v.kind != kind || !HasBody( v ) )
			continue;
		CBaseEntity::PrecacheModel( v.body );
		if ( v.torso )
			CBaseEntity::PrecacheModel( v.torso );
		if ( v.legs )
			CBaseEntity::PrecacheModel( v.legs );
	}
}

void RPMZombie_Choose( CBaseAnimating *pZombie, int kind )
{
	if ( !pZombie || !sv_rpm_zombie_variants.GetBool() )
		return;
	const ZombieVariant_t *v = PickVariant( kind );
	if ( !v )
		return;
	// a model of its own (Hammer, the Creation Kit): left alone
	if ( pZombie->GetModelName() != NULL_STRING && Q_stricmp( STRING( pZombie->GetModelName() ), v->stock ) )
		return;
	int total = 0;
	for ( int i = 0; i < v->skins; i++ )
		total += v->weights[i];
	int r = RandomInt( 0, MAX( total - 1, 0 ) ), skin = 0;
	for ( int i = 0; i < v->skins; i++ )
	{
		if ( r < v->weights[i] )
		{
			skin = i;
			break;
		}
		r -= v->weights[i];
	}
	pZombie->SetModelName( AllocPooledString( v->body ) );
	pZombie->m_nSkin = skin;
}

const char *RPMZombie_Torso( CBaseAnimating *pZombie, const char *pszStock )
{
	const ZombieVariant_t *v = VariantOf( pZombie );
	return ( v && v->torso ) ? v->torso : pszStock;
}

const char *RPMZombie_Legs( CBaseAnimating *pZombie, const char *pszStock )
{
	const ZombieVariant_t *v = VariantOf( pZombie );
	return ( v && v->legs ) ? v->legs : pszStock;
}

void RPMZombie_LegsGib( CBaseAnimating *pZombie, CBaseEntity *pGib )
{
	const ZombieVariant_t *v = VariantOf( pZombie );
	CBaseAnimating *pAnim = pGib ? pGib->GetBaseAnimating() : NULL;
	if ( !v || !v->legs || !pAnim )
		return;
	const int skin = pZombie->m_nSkin;
	pAnim->m_nSkin = ( skin >= 0 && skin < v->skins ) ? v->legsSkin[skin] : 0;
}

CON_COMMAND( rpm_zombie_dump, "HL2RPM: the zombies of the map, their models and skins" )
{
	for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
	{
		if ( !Q_stristr( pEnt->GetClassname(), "zomb" ) || !pEnt->GetBaseAnimating() )
			continue;
		Msg( "%s #%d: %s skin %d\n", pEnt->GetClassname(), pEnt->entindex(), STRING( pEnt->GetModelName() ), pEnt->GetBaseAnimating()->m_nSkin.Get() );
	}
}
