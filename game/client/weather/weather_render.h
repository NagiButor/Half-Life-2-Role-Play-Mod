//========= HL2RPM ============================================================//
//
// Purpose: Weather rendering passes for the deferred renderer.
//
// Frame order (see CDeferredViewRender):
//   G-buffer -> [rain occlusion map] -> shadows + lighting -> WeatherRender_CommitFrame
//   -> WeatherRender_Clouds (screen-space volumetric clouds, temporal)
//   -> composite (sky shader draws atmosphere + clouds + moon/stars;
//                 WeatherRender_PostOpaque: wet surfaces, puddles, height fog)
//   -> translucents -> WeatherRender_Rain (GPU rain streaks)
//   -> volumetrics (WeatherRender_SunShafts: shadow-map raymarched sun rays)
//
//=============================================================================//

#ifndef WEATHER_RENDER_H
#define WEATHER_RENDER_H
#pragma once

class CViewSetup;
class ITexture;
class VMatrix;
struct lightData_Global_t;

// procedural noise (weather_noise.cpp)
ITexture *GetWeatherTexture_CloudNoise();
ITexture *GetWeatherTexture_WeatherMap();
void ShutdownWeatherTextures();

// render targets, created from InitDeferredRTs()
void InitWeatherRTs();
ITexture *GetWeatherRT_Clouds( int index );
ITexture *GetWeatherRT_RainMapDepth();
ITexture *GetWeatherRT_RainMapDummy();
int GetWeatherRainMapResolution();

// quality (driven by the graphics settings)
int WeatherRender_GetCloudQuality();	// 0 off .. 3 high
int WeatherRender_GetSunShaftQuality();	// 0 off .. 3 high
bool WeatherRender_WantsRain();
bool WeatherRender_WantsRainMap();
// illuminance of the light the sky LUT is built for (untinted sun, or the moon at night)
float WeatherRender_GetSkyLightIlluminance();

// per frame
void WeatherRender_CommitFrame( const CViewSetup &view, const lightData_Global_t &light );
void WeatherRender_Clouds( const CViewSetup &view );
void WeatherRender_SetCloudTextureValid( bool bValid );
void WeatherRender_PostOpaque( const CViewSetup &view );
void WeatherRender_Rain( const CViewSetup &view );
void WeatherRender_SunShafts( const CViewSetup &view, ITexture *pTarget );

// rain occlusion map (top-down depth), rendered by CDeferredViewRender
bool WeatherRender_ShouldUpdateRainMap( const CViewSetup &view, Vector &vecCenter, float &flSize );
void WeatherRender_OnRainMapRendered( const VMatrix &matWorldToTexture, const Vector &vecCenter, float flSize, float flZFar );

void WeatherRender_LevelInit();

#endif // WEATHER_RENDER_H
