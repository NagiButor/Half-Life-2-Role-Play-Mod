//========= HL2RPM ============//
//
// RPG-система: все формулы правил в одном месте (RPG_SPEC §1, Part I/02-08).
// Чистые функции без состояния - одинаковы на клиенте (превью в интерфейсе) и сервере.
//
//=============================//

#ifndef RPG_FORMULAS_H
#define RPG_FORMULAS_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_shared.h"

namespace RPGFormulas
{
	// Навык без тегов и вложений: 2 + 2·P + ceil(S/2) (15 при 5/5)
	int SkillBase( int primary, int secondary );

	// Накопленный опыт уровня L: 80·(L−1)·(L+3)
	int XPForLevel( int level );
	int LevelForXP( int xp );

	// Очки навыков за уровень: 10 + floor(ANA/2)
	int SkillPointsPerLevel( int analytics );
	// Перк на чётных уровнях
	bool LevelGivesPerk( int level );

	// Макс. HP: 50 + 10·DUH + 3·(L−1) (+ бонусы)
	int MaxHealth( int spirit, int level, int bonus );

	// Предел веса: 25 + 3·MIG кг (+ бонусы)
	float CarryWeight( int might, float bonus );

	// XP за проверку: навык - порог/2, характеристика - 5×значение; в квесте - половина
	int CheckXPSkill( int threshold, bool bInQuest );
	int CheckXPAttr( int value, bool bInQuest );

	// Подкуп: база × (1.3 − 0.06·AVT) × (1.2 − 0.005·barter) × множитель, вверх до 5
	int BribeCost( int base, int authority, int barter, float flMult );

	// Цены: покупка ×max(1.0, 1.6 − 0.006·barter − R), продажа ×min(0.9, 0.4 + 0.004·barter + R)
	float BuyPriceMult( int barter, float flFactionBonus );
	float SellPriceMult( int barter, float flFactionBonus );
	// R по уровню фракции: Признание +0.1, Союзник +0.2, Неприязнь −0.25
	float FactionPriceBonus( int factionTier );

	// Уровни шкал
	int CivilTier( int civil );
	int FactionTier( int value );
	int HumanityTier( int humanity );
	int SuspicionTier( float suspicion );
	int CivilTierMin( int tier );		// нижняя граница уровня (для условий «не ниже»)
	int CivilTierMax( int tier );
	int FactionTierMin( int tier );
	int FactionTierMax( int tier );

	// Спад подозрения: множитель по уровню ГС (Благонадёжный ×1.5, Лоялист ×2, Под наблюдением ×0.5)
	float SuspicionDecayCivilMult( int civilTier );

	// Ключи и разбор
	const char *CivilTierKey( int tier );
	const char *FactionTierKey( int tier );
	const char *HumanityTierKey( int tier );
	const char *SuspicionTierKey( int tier );
	int CivilTierFromKey( const char *pszKey );		// -1 если неизвестно
	int FactionTierFromKey( const char *pszKey );
	int HumanityTierFromKey( const char *pszKey );
	int SuspicionTierFromKey( const char *pszKey );

	// Атрибут по id/сокращению (agility, LOV, ЛОВ...); -1 если не атрибут
	int AttrFromName( const char *pszName );
}

#endif // RPG_FORMULAS_H
