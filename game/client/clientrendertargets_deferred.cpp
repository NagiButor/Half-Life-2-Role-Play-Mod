#include "cbase.h"
#include "baseclientrendertargets.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "rendertexture.h"
 
static ITexture *CreateDeferredGBufferTexture( IMaterialSystem *pMaterialSystem, int index )
{
	char name[64];
	V_snprintf( name, sizeof( name ), "_rt_GBuffer%d", index );
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		name,
		1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F,
		MATERIAL_RT_DEPTH_SHARED,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
		CREATERENDERTARGETFLAGS_HDR );
}
 
class CDeferredClientRenderTargets : public CBaseClientRenderTargets
{
	DECLARE_CLASS_GAMEROOT( CDeferredClientRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
	{
		CBaseClientRenderTargets::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );

		if ( !pHardwareConfig || !pHardwareConfig->SupportsPixelShaders_2_b() )
			return;

		for ( int i = 0; i < DEFERRED_GBUFFER_RT_COUNT; ++i )
		{
			m_DeferredGBufferTextures[i].Init( CreateDeferredGBufferTexture( pMaterialSystem, i ) );
		}
	}

	virtual void ShutdownClientRenderTargets( void )
	{
		for ( int i = 0; i < DEFERRED_GBUFFER_RT_COUNT; ++i )
		{
			m_DeferredGBufferTextures[i].Shutdown();
		}

		CBaseClientRenderTargets::ShutdownClientRenderTargets();
	}

private:
	CTextureReference m_DeferredGBufferTextures[DEFERRED_GBUFFER_RT_COUNT];
};

static CDeferredClientRenderTargets g_DeferredClientRenderTargets;

IClientRenderTargets *g_pClientRenderTargets = &g_DeferredClientRenderTargets;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CDeferredClientRenderTargets, IClientRenderTargets, CLIENTRENDERTARGETS_INTERFACE_VERSION, g_DeferredClientRenderTargets );
