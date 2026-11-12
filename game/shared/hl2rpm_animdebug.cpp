//========= HL2RPM ============================================================//
//
// Animation debugging (client and server): pose parameters of models by index
// and value, so the two sides can be compared (rpm_pose_dump / cl_rpm_pose_dump).
//
//=============================================================================//
#include "cbase.h"
#include "studio.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define RPM_POSE_DUMP_CMD "cl_rpm_pose_dump"
#define RPM_ANIMATING C_BaseAnimating
#else
#define RPM_POSE_DUMP_CMD "rpm_pose_dump"
#define RPM_ANIMATING CBaseAnimating
#endif

static void PoseDump( const CCommand &args )
{
	const char *pszFilter = args.ArgC() > 1 ? args[1] : "";
	int n = 0;
#ifdef CLIENT_DLL
	for ( C_BaseEntity *pEnt = ClientEntityList().FirstBaseEntity(); pEnt; pEnt = ClientEntityList().NextBaseEntity( pEnt ) )
#else
	for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
#endif
	{
		RPM_ANIMATING *pAnim = pEnt->GetBaseAnimating();
		if ( !pAnim || !pAnim->GetModel() )
			continue;
		const char *pszModel = modelinfo->GetModelName( pAnim->GetModel() );
		if ( pszFilter[0] && !Q_stristr( pszModel, pszFilter ) && !Q_stristr( pEnt->GetClassname(), pszFilter ) )
			continue;
		CStudioHdr *hdr = pAnim->GetModelPtr();
		if ( !hdr )
			continue;
		Msg( "%s #%d %s seq %d (%s), %d pose parameters:\n", pEnt->GetClassname(), pEnt->entindex(), pszModel,
			pAnim->GetSequence(), pAnim->GetSequenceName( pAnim->GetSequence() ), hdr->GetNumPoseParameters() );
		for ( int i = 0; i < hdr->GetNumPoseParameters(); i++ )
		{
			const mstudioposeparamdesc_t &p = hdr->pPoseParameter( i );
			Msg( "  %2d %-16s %7.2f..%-7.2f = %.3f\n", i, p.pszName(), p.start, p.end, pAnim->GetPoseParameter( i ) );
		}
		if ( ++n >= 8 )
			break;
	}
}

#ifdef CLIENT_DLL
static ConCommand cl_rpm_pose_dump( RPM_POSE_DUMP_CMD, PoseDump, "HL2RPM: pose parameters of client models (filter: model or class substring)" );
#else
static ConCommand rpm_pose_dump( RPM_POSE_DUMP_CMD, PoseDump, "HL2RPM: pose parameters of server models (filter: model or class substring)" );
#endif
