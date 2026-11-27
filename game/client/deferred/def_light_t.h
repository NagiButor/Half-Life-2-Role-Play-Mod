#ifndef DEF_LIGHT_H
#define DEF_LIGHT_H

class CMeshBuilder;
class IDefCookie;

#if DEFCFG_USE_SSE
struct def_light_presortdatax4_t
{
	fltx4		bounds_min_naive[3];
	fltx4		bounds_max_naive[3];
	i32x4		hasShadow;
	i32x4		hasVolumetrics;
	def_light_t* lights[4];
	uint16		count;				//this throws the alignment way out
};
#endif

struct def_light_t
{
	friend class CLightingManager;

	def_light_t( bool bWorld = false );
	virtual ~def_light_t();

	DECLARE_DATADESC();

	static def_light_t *AllocateFromKeyValues( KeyValues *pKV );
	KeyValues *AllocateAsKeyValues();
	void ApplyKeyValueProperties( KeyValues *pKV );

	bool bWorldLight;

	Vector pos;
	QAngle ang;

	Vector col_diffuse;
	Vector col_ambient;

	float flRadius;
	float flFalloffPower;

	uint8 iLighttype;
	uint8 iFlags;
	uint8 iCookieIndex;

	float flSpotCone_Inner;
	float flSpotCone_Outer;

	uint16 iVisible_Dist;
	uint16 iVisible_Range;
	uint16 iShadow_Dist;
	uint16 iShadow_Range;

	uint16 iStyleSeed;
	float flStyle_Amount;
	float flStyle_Smooth;
	float flStyle_Random;
	float flStyle_Speed;

#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	float flVolumeLOD0Dist;
	float flVolumeLOD1Dist;
	float flVolumeLOD2Dist;
	float flVolumeLOD3Dist;
#endif

#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	int	iVolumeSamples;
#endif

	int iShadowExcludeEntIndex; // Entity index to exclude from shadow rendering (e.g. flashlight owner), -1 = none

	void BuildBox( IMesh *pMesh );
	void BuildSphere( IMesh **pMesh );
	void BuildCone( IMesh *pMesh );
	void BuildConeFlipped( IMesh *pMesh );

	static void ShutdownSharedMeshes();

	FORCEINLINE bool IsWorldLight()
	{
		return bWorldLight;
	};

	FORCEINLINE bool IsDirty()
	{
		return ( iFlags & ( DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH ) ) != 0;
	};
	FORCEINLINE bool IsDirtyXForms()
	{
		return ( iFlags & DEFLIGHT_DIRTY_XFORMS ) != 0;
	};
	FORCEINLINE bool IsDirtyRenderMesh()
	{
		return ( iFlags & DEFLIGHT_DIRTY_RENDERMESH ) != 0;
	};

	FORCEINLINE void UnDirtyAll()
	{
		iFlags &= ~( DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH );
	};

	FORCEINLINE void MakeDirtyXForms()
	{
		iFlags |= DEFLIGHT_DIRTY_XFORMS;
	};
	FORCEINLINE void MakeDirtyRenderMesh()
	{
		iFlags |= DEFLIGHT_DIRTY_RENDERMESH;
	};
	FORCEINLINE void MakeDirtyAll()
	{
		iFlags |= DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH;
	};

	// HL2RPM: fast point-light transform/bounds update without 14 world traces or leaf enumeration
	// (used for small per-frame firelight wander / dynamic light updates).
	void UpdatePointTransformFast();

	FORCEINLINE bool IsPoint()
	{
		return iLighttype == DEFLIGHTTYPE_POINT;
	};

	// HL2RPM: the light's distance falloff, as the light passes compute it (common_lighting_fxc.h
	// DeferredLightFalloff): its own ( 1 - d / r )^power, or, with a negative power (a light converted
	// from the map's light entities), the engine's attenuation, 1 at 100 units
	float GetDistanceFalloff( float flDist ) const
	{
		const float x = clamp( flDist / Max( flRadius, 1.0f ), 0.0f, 1.0f );
		const float x4 = x * x * x * x;
		if ( flFalloffPower < 0.0f )
		{
			const float k = Max( -flFalloffPower, 576.0f );
			const float w = 1.0f - x4;
			return ( k + 10000.0f ) / ( k + flDist * flDist ) * w * w;
		}
		return powf( 1.0f - x, Max( flFalloffPower, 0.01f ) ) * ( 1.0f - x4 * x4 );
	}
	FORCEINLINE bool IsSpot()
	{
		return iLighttype == DEFLIGHTTYPE_SPOT;
	};

	// HL2RPM: the light's own flag or a shadow lent by the shadow budget for this frame
	FORCEINLINE bool HasShadow()
	{
		return ( iFlags & DEFLIGHT_SHADOW_ENABLED ) != 0 || bBudgetShadow;
	};
	FORCEINLINE bool HasCookie()
	{
		return ( iFlags & DEFLIGHT_COOKIE_ENABLED ) != 0;
	};
	FORCEINLINE bool HasVolumetrics()
	{
		return ( iFlags & DEFLIGHT_VOLUMETRICS_ENABLED ) != 0;
	};
	FORCEINLINE bool HasLightstyle()
	{
		return ( iFlags & DEFLIGHT_LIGHTSTYLE_ENABLED ) != 0;
	};

	FORCEINLINE bool ShouldRenderShadow()
	{
		return HasShadow() && flShadowFade < 1;
	};

	bool IsCookieReady();
	ITexture *GetCookieForDraw( const int iTargetIndex = 0 );
	void ClearCookie();
	IDefCookie *CreateCookieInstance( const char *pszCookieName );
	void SetCookie( IDefCookie *pCookie );

	FORCEINLINE float GetFOV()
	{
		Assert( iLighttype == DEFLIGHTTYPE_SPOT );
		return flFOV;
	};

	FORCEINLINE int *GetLeaves()
	{
		return iLeaveIDs;
	};
	FORCEINLINE int GetNumLeaves()
	{
		return iNumLeaves;
	};

	// HL2RPM: light reach (deferred_lightreach.cpp) - what of the world the light gets to, traced from
	// it. The box of all its rays reached replaces the box of the 14 / 5 traces of UpdateXForms for
	// the leaves the light is culled by (those traces missed the light falling between them).
	void SetReachBounds( const Vector &vecMins, const Vector &vecMaxs );
	void ClearReachBounds();
	void GetCullBounds( Vector &vecMins, Vector &vecMaxs ) const;
	int iReachSlot;				// slot in the reach atlas (-1 = none)
	float flReachParam;			// for the shader this frame: slot + 0.5 x fade, -1 = no map
	float flReachFade;			// 0..1 the map eases in
	Vector reachTrackPos;		// where it was last seen, and since when it stays
	float flReachTrackRadius;
	float flReachStillSince;

private:

	def_light_t( const def_light_t &o );

	void DestroyMeshes();

	// *********************************************
	// * volatile data, not guaranteed to be valid *
	// *********************************************

	int iNumLeaves;
	int iLeaveIDs[DEFLIGHT_MAX_LEAVES];

	float flMaxDistSqr;

	void UpdateMatrix();
	float flFOV;
	VMatrix worldTransform;
	VMatrix spotMVPInv;
	VMatrix spotVPInv;
	VMatrix spotWorldToTex;

	void UpdateFrustum();
	Frustum_t spotFrustum;
	
	void UpdateXForms();
	Vector bounds_min, bounds_max;
	Vector bounds_min_naive, bounds_max_naive;
	// HL2RPM: the box of all the light reaches (set by the reach map) and what it was traced for
	bool bReachBounds;
	Vector reach_min, reach_max;
	Vector reachBoundsPos;
	float flReachBoundsRadius;
	void UpdateLeaves();

	void UpdateRenderMesh();
	IMesh *pMesh_World;

	void UpdateVolumetrics();
	IMesh *pMesh_Volumetrics;
	IMesh *pMesh_VolumPrepass;

#if DEBUG
	IMesh *pMesh_Debug;
	IMesh *pMesh_Debug_Volumetrics;
#endif

	static IMesh *pMeshUnitSphere;

	void UpdateCookieTexture();
	uint8 iOldCookieIndex;
	IDefCookie *pCookie;

	Vector backDir;
	Vector boundsCenter;

	float flDistance_ViewOrigin;
	float flShadowFade;

	// HL2RPM: shadow budget (CLightingManager::UpdateShadowBudget) - the nearest point lights
	// without shadows get one; iFlags stays as authored (editor, saved lights)
	bool bBudgetShadow;
	float flBudgetFade;		// 0 = full budget shadow .. 1 = none (fades in / out)

	float flLastRandomTime;
	float flLastRandomValue;
};

struct def_light_temp_t : public def_light_t
{
	def_light_temp_t( float fLifeTime = 1.0f )
	{
		fEndLifeTime = gpGlobals->curtime + fLifeTime;
		fDecay = 1.f / fLifeTime;
	}

	float fEndLifeTime;
	float fDecay;
};

#endif