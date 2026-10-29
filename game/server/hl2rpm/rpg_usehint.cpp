//========= HL2RPM ============//
//
// RPG-система (сервер): подсказка у прицела «[E] Говорить: Тамара Ильинична» - что сделает
// клавиша «использовать» с тем, на что смотрит игрок. 10 раз в секунду FindUseEntity; клиенту
// уходит сообщение RPG_UseHint (byte вид, string подпись - токен или текст) только при смене.
// Клиент рисует её в hl2rpm/rpg_hud.cpp.
//
//=============================//

#include "cbase.h"
#include "player.h"
#include "ai_basenpc.h"
#include "items.h"
#include "basecombatweapon.h"
#include "hl2rpm/rpg_dialog.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_shared.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar rpg_usehint( "rpg_usehint", "1", FCVAR_GAMEDLL, "RPG: send the use hint (what the use key would do) to the HUD" );

enum RPGUseHint_t
{
	RPG_HINT_NONE = 0,
	RPG_HINT_TALK,
	RPG_HINT_TAKE,
	RPG_HINT_SLEEP,
	RPG_HINT_READ,
	RPG_HINT_USE,
};

// prop_rpg_usable (rpg_entities.cpp): действие и подпись
extern bool RPG_GetUsableHint( CBaseEntity *pEnt, const char *&pszAction, const char *&pszName );

static int ClassifyUse( CBaseEntity *pEnt, CUtlString &label )
{
	label.Clear();
	if ( !pEnt )
		return RPG_HINT_NONE;
	// люди и всё, у кого есть разговор
	const char *pszName = STRING( pEnt->GetEntityName() );
	if ( pszName && pszName[0] && RPGDialog::Has( pszName ) )
	{
		const char *pszSpeaker = RPGDialog::GetSpeaker( pEnt );
		if ( pszSpeaker )
		{
			label = pszSpeaker;
			return RPG_HINT_TALK;
		}
	}
	// кровати, записки, терминалы
	const char *pszAction = NULL, *pszHint = NULL;
	if ( RPG_GetUsableHint( pEnt, pszAction, pszHint ) )
	{
		label = pszHint ? pszHint : "";
		if ( pszAction && !Q_stricmp( pszAction, "sleep" ) )
			return RPG_HINT_SLEEP;
		if ( pszAction && ( !Q_stricmp( pszAction, "note" ) || !Q_stricmp( pszAction, "terminal" ) ) )
			return RPG_HINT_READ;
		return RPG_HINT_USE;
	}
	// предметы и оружие на земле
	const char *pszClass = pEnt->GetClassname();
	const bool bItem = dynamic_cast<CItem *>( pEnt ) != NULL;
	CBaseCombatWeapon *pWpn = pEnt->MyCombatWeaponPointer();
	if ( ( bItem || ( pWpn && !pWpn->GetOwner() ) ) && !pEnt->GetMoveParent() )
	{
		const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
		label = pDef ? pDef->name.Get() : CFmtStr( "#RPG_Item_%s", pszClass ).Access();
		return RPG_HINT_TAKE;
	}
	return RPG_HINT_NONE;
}

class CRPGUseHintSystem : public CAutoGameSystemPerFrame
{
public:
	CRPGUseHintSystem() : CAutoGameSystemPerFrame( "CRPGUseHintSystem" ) {}
	virtual void LevelInitPostEntity() { m_iKind = -1; m_flNext = 0.0f; }
	virtual void FrameUpdatePostEntityThink()
	{
		if ( !rpg_usehint.GetBool() || gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.1f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer )
			return;
		CUtlString label;
		int kind = RPG_HINT_NONE;
		if ( pPlayer->IsAlive() && !RPGDialog::IsActive() && !pPlayer->IsInAVehicle() )
			kind = ClassifyUse( pPlayer->FindUseEntity(), label );
		if ( kind == m_iKind && !Q_strcmp( label.Get(), m_Label.Get() ) )
			return;
		m_iKind = kind;
		m_Label = label;
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_UseHint" );
			WRITE_BYTE( kind );
			WRITE_STRING( label.Get() );
		MessageEnd();
	}
private:
	int m_iKind = -1;
	CUtlString m_Label;
	float m_flNext = 0.0f;
};
static CRPGUseHintSystem g_RPGUseHintSystem;
