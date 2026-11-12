# Deferred Global Light / Sun Shadows / Cascaded Shadow Maps - Shader Research

## Overview
This document summarizes the shader implementation for deferred global lighting with cascaded shadow maps (CSM) in the HL2 RPMSOURCE Source SDK 2013 Mapbase v8.0.

---

## Key Configuration Settings (deferred_global_common.h)

### Cascade Shadow Maps (CSM)
- **CSM_USE_COMPOSITED_TARGET**: 1 (One physical shadowmap for multiple cascades)
- **CSM_COMP_RES_X**: 8192 (Composited resolution width)
- **CSM_COMP_RES_Y**: 4096 (Composited resolution height)
- **SHADOW_NUM_CASCADES**: 2 (Number of cascades, from MAX_SHADOW_ORTHO)

### Shadow Rendering Targets
```
DEFRTNAME_SHADOWDEPTH_ORTHO "_rt_ShadowDepth_ortho_"     // Orthographic shadow depth
DEFRTNAME_SHADOWCOLOR_ORTHO "_rt_ShadowColor_ortho_"     // Orthographic shadow color
DEFRTNAME_SHADOWDEPTH_PROJ  "_rt_ShadowDepth_proj_"      // Projected shadow depth
DEFRTNAME_SHADOWCOLOR_PROJ  "_rt_ShadowColor_proj_"      // Projected shadow color
DEFRTNAME_SHADOWDEPTH_DP    "_rt_ShadowDepth_dp_"        // Dual paraboloid depth
DEFRTNAME_SHADOWCOLOR_DP    "_rt_ShadowColor_dp_"        // Dual paraboloid color
```

### Shadow Modes
```
DEFERRED_SHADOW_MODE_ORTHO      0   // Orthographic projection (used for sun/global light)
DEFERRED_SHADOW_MODE_PROJECTED  1   // Perspective projection
DEFERRED_SHADOW_MODE_DPSM       2   // Dual Paraboloid Shadow Map
```

### Shadowmapping Methods (currently set to 5x5 Gaussian stencil)
```
SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN = 7 (currently selected)
SHADOWMAPPING_METHOD = SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN
```

Supported methods include:
- RAW (unfiltered)
- 4x4 SOFTWARE_BILINEAR_BOX
- 4x4 SOFTWARE_BILINEAR_GAUSSIAN
- 5x5 SOFTWARE_BILINEAR_GAUSSIAN
- PCF (Percentage Closer Filtering)
- PCSS (Percentage Closer Soft Shadows)

---

## Cascade Configuration (cascade_t.cpp)

Two cascades defined with the following parameters:

### Cascade 0 (Close/High-Detail)
```
Resolution:        4096
Projection Size:   1536.0f
Light Offset:      10000.0f
Far Z:             12000.0f
Slope Scale Min:   0.8f
Slope Scale Max:   3.0f
Normal Scale Max:  4.0f
Update Delay:      0.0f
Radiosity Output:  true
Radiosity Target:  0
```

### Cascade 1 (Far/Low-Detail)
```
Resolution:        4096
Projection Size:   4096.0f
Light Offset:      10000.0f
Far Z:             15000.0f
Slope Scale Min:   3.0f
Slope Scale Max:   6.0f
Normal Scale Max:  12.0f
Update Delay:      0.25f
Radiosity Output:  true
Radiosity Target:  1
Viewport X:        4096
Viewport Y:        0
```

Note: Slope and normal offsets mitigate shadow acne on shallow angles (floors).

---

## Shader Pipeline

### 1. Shadow Depth Pass (shadowpass_vs30.fxc / shadowpass_ps30.fxc)

**Vertex Shader (shadowpass_vs30.fxc)**

Static Combos:
- MODEL (0/1): Whether rendering model or world geometry
- MORPHING_VTEX (0/1): Vertex texture morphing
- MULTITEXTURE (0/2): Blending mode support

Dynamic Combos:
- COMPRESSED_VERTS (0/1)
- SKINNING (0/1)
- MORPHING (0/1)
- SHADOW_MODE (0/2): ORTHO, PROJECTED, or DPSM
- RADIOSITY (0/1): Whether to output radiosity data

Key Constants:
- g_vecShadowForward (SHADER_SPECIFIC_CONST_0): Light direction forward vector
- g_vecSlopeData (SHADER_SPECIFIC_CONST_1): Slope scale min/max and projection depth
- g_vecOrigin (SHADER_SPECIFIC_CONST_2): Light origin for projected shadows
- g_matView (SHADER_SPECIFIC_CONST_7): View matrix for depth color output

Key Processing:
1. Decompress/morph/skin vertices
2. Apply slope-based normal offset bias based on surface normal facing (reduces self-shadowing)
3. For ORTHO mode: Projects directly using orthographic view-projection with slope bias
4. For PROJECTED mode: Applies additional origin-based offset for perspective shadows
5. For DPSM mode: Performs dual paraboloid projection
6. Outputs depth for shadow mapping (color or projected Z/W)

**Pixel Shader (shadowpass_ps30.fxc)**

Static Combos:
- ALPHATEST (0/1): Alpha testing for transparency
- MULTITEXTURE (0/2): Blending support

Dynamic Combos:
- SHADOW_MODE (0/2)
- RADIOSITY (0/1)

Key Processing:
1. Performs alpha testing if enabled
2. For radiosity: Samples albedo and outputs normal data for radiosity propagation
3. Outputs depth to color buffer based on SHADOWMAPPING_USE_COLOR setting
4. Supports bilateral depth testing for better quality

---

### 2. Global Lighting Pass (lighting_global.cpp / lightingpass_global_ps30.fxc)

**Purpose**: Full-screen deferred lighting pass that samples G-buffer and applies global directional light with cascaded shadows.

**Vertex Shader**: Uses defconstruct_vs30 (basic fullscreen quad)

**Pixel Shader (lightingpass_global_ps30.fxc)**

Input Textures:
- sNormals (s0): G-buffer normals + lighting control data
- sDepth (s1): G-buffer depth
- sLightCtrl (s3, optional): Lighting control data (if not packed)
- sShadowDepth (s2, if HAS_SHADOW): Cascaded shadow depth texture

Key Constants:
```
c0-c1:   View origin, light forward vector
c2-c7:   2x 4x3 orthographic projection matrices (2 cascades)
c8-c9:   2x UV transform for shadow mapping
c10-c11: 2x Slope data for each cascade
c12-c13: 2x Filter config A
c14-c15: 2x Filter config B
c16-c18: Diffuse, ambient high, ambient low-half lighting
```

Key Processing:
1. **Depth Test**: Clips if depth < 0.1f
2. **Normal Unpacking**: Decodes normal from G-buffer (0-1 range to -1 to 1)
3. **Lighting Control**: Unpacks phong exponent, half-lambert, lit-face flags
4. **Lit Normal**: Applies lit-face modulation to normal
5. **Specular Calculation**: Blinn-phong style specular based on view direction and light
6. **Diffuse Calculation**:
   - Uses half-lambert for softer lighting
   - Applies sun visibility fade based on sun angle (viewFwdDot.z + 0.02) / 0.08
7. **Cascaded Shadow**: If HAS_SHADOW enabled:
   - Reconstructs world position from depth and view ray
   - Calls PerformCascadedShadow() which:
     - Projects to shadow space using cascade 0 projection matrix
     - Tests if in cascade 0 bounds (0-1 range with 0.0015f bias and 1.003f scale)
     - Falls back to cascade 1 if out of bounds
     - Tests if out of cascade 1 bounds with more generous tolerance (0.003f, 1.006f)
     - Performs shadow mapping with 5x5 Gaussian filtering
8. **Final Lighting**: Combines diffuse and specular with shadow attenuation

---

## Shadow Mapping Implementation (common_shadowmapping_fxc.h)

### Core Shadow Functions

**PerformCascadedShadow()**
- Takes 2 cascades of projection matrices and filter configs
- Implements cascade selection based on projected position bounds
- Handles AMD-specific workarounds for floating-point precision
- Returns shadow multiplier (0-1)

**Cascade Transition**:
```hlsl
// Cascade 0: within 0.0015f normalized bounds scaled by 1.003f
// Cascade 1: within 0.003f normalized bounds scaled by 1.006f
// Beyond cascade 1: returns 1.0f (fully lit)
```

**ToShadowSpace_Ortho()**
- Applies normal-offset bias for self-shadowing reduction
- Applies slope-scale bias based on normal facing
- Projects world position to orthographic shadow space

### Shadow Filtering

**Current Method: 5x5 Gaussian Stencil (SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN)**

Uses NVIDIA's hardware depth comparison with 5x5 Gaussian kernel weights:
```
ShadowDepth_5x5Gauss_Nvidia():
- Center: 0.162103f
- Cross (4 samples): 0.021938f each
- Diagonals (4 samples): 0.059634f each
- Plus/minus (4 samples): 0.098320f each
- Corners (4 samples): 0.002969f each
- Far corners (4 samples): 0.013306f each
```

Alternative methods available:
- **PerformProjectedShadow()**: For spotlight/point light projected shadows
- **PerformDualParaboloidShadow()**: For omnidirectional light shadows
- **PCSS (Percentage Closer Soft Shadows)**: Advanced variable penumbra filtering

---

## Deferred Global Light Structure

The global light is committed via `CommitGlobalLightForward()` in lighting_global.cpp:

**Light Data Components**:
- diff: Diffuse color
- ambh: Ambient high color (used for upward-facing surfaces)
- ambl: Ambient low color (used for downward-facing surfaces)
- bEnabled: Whether light is active
- bShadow: Whether to apply shadow mapping

**Ambient Calculation**:
```hlsl
// Interpolates between ambient low and high based on normal-to-light dot product
ambient = lerp(g_light_ambient_low_half, g_light_ambient_high, viewFwdDot)
```

---

## Common Deferred Functions (common_deferred_fxc.h)

### Lighting Control Packing
- Packs phong exponent, half-lambert, lit-face into single float
- 6 bits phong exp, 1 bit half-lambert, 1 bit lit-face

### Direction Vector Encoding
- EncodeDirectionVectorXYZToSphericalST(): Encodes normal to 2D
- DecodeDirectionVectorXYZFromSphericalST(): Decodes back to 3D

### Light Accumulation
- WriteLighting() / ReadLighting(): Handles compressed vs. full precision lighting
- GetLightAccumUVs(): Converts projected position to light accumulation buffer UVs

---

## Render Passes Overview

### G-Buffer Rendering (defpass_gbuffer)
- Outputs normals, depth, lighting control data

### Shadow Pass (defpass_shadow)
- Renders scene from light's perspective to generate shadow depth maps
- Supports alphatest for transparent materials
- Can output radiosity data simultaneously

### Lighting Composite (defpass_composite)
- Combines all light passes (global, point, spot) with G-buffer data
- Applies final shading

### Global Lighting Pass (lighting_global)
- Special case for directional/sun light with cascaded shadows
- Processes full-screen with G-buffer sampling

---

## Key Files Referenced

1. **deferred_global_common.h** - Configuration and constants
2. **shadowpass_vs30.fxc** - Shadow depth pass vertex shader
3. **shadowpass_ps30.fxc** - Shadow depth pass pixel shader
4. **lightingpass_global_ps30.fxc** - Global lighting/shadow application
5. **common_shadowmapping_fxc.h** - Shadow filtering and cascade implementation
6. **common_deferred_fxc.h** - Deferred rendering utilities
7. **defpass_shadow.cpp/h** - C++ shadow pass interface
8. **defpass_shadow.h** - Shadow pass structures
9. **cascade_t.cpp/h** - Cascade configuration
10. **lighting_global.cpp** - Global light shader initialization

---

## Performance Considerations

1. **Cascaded Shadow Maps**: Reduces artifacts from single-cascade limitations
2. **Composited Target**: Uses single 8192x4096 texture for both cascades (CSM_USE_COMPOSITED_TARGET)
3. **Slope/Normal Bias**: Mitigates shadow acne without excessive bias
4. **Gaussian Filtering**: 5x5 soft shadows for quality (can be changed to faster methods)
5. **Radiosity Output**: Optional simultaneous indirect lighting computation
6. **Adaptive LOD**: Framework for level-of-detail adjustments (disabled by default)

---

## Notes

- The implementation uses orthographic projection for global/sun light
- Cascades use different projection sizes and far-z values for optimal quality
- Shadow mapping uses DEPTH_STENCIL format with Gaussian filtering
- Supports AMD-specific workarounds for floating-point precision issues
- Depth reconstruction limit: 7000.0f for regular objects, 8192.0f for fullscreen
- Specular exponent base: 6.0f
