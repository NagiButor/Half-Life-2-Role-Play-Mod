// item_combine_soldier_suit.h
#ifndef ITEM_COMBINE_SOLDIER_SUIT_H
#define ITEM_COMBINE_SOLDIER_SUIT_H
#ifdef _WIN32
#pragma once
#endif

#include "items.h"

class CItemCombineSoldierSuit : public CItem
{
public:
    DECLARE_CLASS( CItemCombineSoldierSuit, CItem );

    CItemCombineSoldierSuit() {}

    virtual void Spawn( void );
    virtual void Precache( void );

    // Give to player on touch
    virtual bool MyTouch( CBasePlayer *pPlayer );

    DECLARE_DATADESC();
};

#endif // ITEM_COMBINE_SOLDIER_SUIT_H
