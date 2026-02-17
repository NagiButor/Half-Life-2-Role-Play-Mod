#ifndef DEFERRED_VERBOSE_H
#define DEFERRED_VERBOSE_H

#include "tier1/convar.h"

FORCEINLINE int DeferredVerboseLevel()
{
	static ConVarRef s_r_deferred_verbose( "r_deferred_verbose" );
	return ( s_r_deferred_verbose.IsValid() ) ? s_r_deferred_verbose.GetInt() : 0;
}

#endif
