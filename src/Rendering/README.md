# Rendering Architecture

A modern deferred rendering pipeline built with Vulkan 1.3, featuring physically-based shading, cascaded shadow mapping, order-independent transparency, and screen-space global illumination via Radiance Cascades.

## Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           FRAME EXECUTION ORDER                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. Shadow Pass          Generate depth maps for all shadow-casting lights  │
│         │                                                                   │
│         ▼                                                                   │
│  2. Geometry Pass        Fill G-Buffer (position, normal, albedo, material) │
│         │                                                                   │
│         ▼                                                                   │
│  3. Skybox Pass          Render environment cubemap to albedo buffer        │
│         │                                                                   │
│         ▼                                                                   │
│  4. Direct Light Pass    Evaluate PBR lighting using G-Buffer + shadows     │
│         │                                                                   │
│         ▼                                                                   │
│  5. Radiance Cascades    Screen-space GI: depth pyramid → cascades → resolve│
│         │                                                                   │
│         ▼                                                                   │
│  6. Transparency Pass    Order-independent transparency (WBOIT)             │
│         │                                                                   │
│         ▼                                                                   │
│  7. Composition Pass     Combine opaque + transparent + GI                  │
│         │                                                                   │
│         ▼                                                                   │
│  8. SMAA Passes          Subpixel morphological anti-aliasing (3 stages)    │
│         │                                                                   │
│         ▼                                                                   │
│  9. Color Correction     Tone mapping and final output to swapchain         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Key Features

### Deferred Rendering

The engine uses a deferred shading architecture that decouples geometry rendering from lighting calculations. This enables:

- **Single geometry pass** regardless of light count
- **Complex lighting** with many lights at minimal cost
- **Screen-space effects** that operate on G-Buffer data

### G-Buffer Layout

| Attachment | Format | Contents |
|------------|--------|----------|
| Position | RGBA32F | World-space position (XYZ), validity flag (W) |
| Normal | RGBA16F | World-space normal (encoded 0-1 range) |
| Albedo | RGBA8 | Base color (RGB), alpha for masking |
| Material | RGBA8 | Metallic (R), Smoothness (G), AO (B) |
| Depth | D32F | Hardware depth buffer |

### Physically-Based Rendering

The lighting model uses the Cook-Torrance BRDF with:

- **GGX/Trowbridge-Reitz** normal distribution for realistic specular highlights
- **Schlick-GGX** geometry function for microfacet self-shadowing
- **Fresnel-Schlick** with roughness for view-dependent reflectance
- Energy-conserving diffuse/specular split ensuring materials look correct under any lighting

### Shadow Mapping

- **Directional lights**: 4-cascade CSM with smooth blending between cascades
- **Spot lights**: Standard perspective shadow maps
- **Point lights**: Cubemap shadows with 6 faces per light
- **PCF filtering**: 16-sample rotated Poisson disk for soft shadow edges

### Global Illumination

Screen-space Radiance Cascades implementation providing:

- Real-time diffuse indirect lighting with color bleeding
- 6 cascades covering near to far field with proper interval merging
- Temporal accumulation for noise reduction
- Skybox integration for rays that miss geometry

### Order-Independent Transparency

Weighted Blended OIT based on McGuire & Bavoil 2013:

- Accumulation buffer for weighted color contribution
- Revealage buffer for coverage tracking
- Depth-tested against opaque geometry
- Full PBR lighting on transparent surfaces

## Render Pass Documentation

Each major render pass has detailed documentation:

| Pass | Description |
|------|-------------|
| [Geometry Pass](RenderPasses/Geometry/README.md) | G-Buffer fill with material support |
| [Direct Lighting](RenderPasses/Direct%20Lighting/README.md) | PBR lighting with shadows and IBL |
| [Shadow Mapping](RenderPasses/Shadowmapping/README.md) | Multi-light shadow generation |
| [Radiance Cascades](RenderPasses/Radiance%20Cascades/README.md) | Screen-space global illumination |
| [Transparency](RenderPasses/Transparency/README.md) | Order-independent transparency |

## Resource Management

### Frame-in-Flight Model

The renderer maintains three sets of resources to enable GPU/CPU parallelism. While the GPU processes one frame, the CPU can prepare the next. This triple-buffering approach includes command buffers, uniform buffers, descriptor sets, and framebuffer attachments.

### Synchronization Strategy

Vulkan requires explicit synchronization between operations:

- **Buffer barriers** ensure uniform buffer writes from the CPU are visible to shaders
- **Image barriers** handle layout transitions between passes (e.g., from render target to shader input)
- **Subpass dependencies** order attachment access within render passes


## Technical Decisions

### Why Deferred?

For a scene with N lights and M objects, forward rendering requires O(N × M) fragment shader invocations—each object must be shaded for each light. Deferred rendering reduces this to O(M) for geometry plus O(N × screen pixels) for lighting.

The break-even point is typically 4-8 lights. Beyond that, deferred scales significantly better. Since this renderer targets scenes with multiple shadow-casting lights, deferred is the clear choice.

### Why Radiance Cascades over SSAO/SSGI?

Traditional screen-space ambient occlusion only approximates shadowing, without any color bleeding from nearby surfaces. Standard SSGI has limited range and struggles with multi-bounce effects.

Radiance Cascades uses a hierarchical structure where each cascade handles a specific distance range. This captures both near-field detail and far-field ambient lighting with proper interval merging. The result is more physically plausible indirect illumination at real-time rates.

### Why WBOIT over Depth Peeling?

Depth peeling renders the scene multiple times—once per transparency layer—making it O(N) passes for N layers. This becomes expensive with many overlapping transparent surfaces.

WBOIT approximates the correct blend in a single pass using weighted averages. While it can produce artifacts when surfaces at similar depths have very different colors, the performance is predictable and the visual quality is sufficient for most content.

## Building and Debugging

### Shader Compilation

All GLSL shaders are compiled to SPIR-V before the application runs. The compile script processes vertex, fragment, and compute shaders, outputting to the build/shaders directory.

## References

- Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013
- Walter et al., "Microfacet Models for Refraction", EGSR 2007
- McGuire & Bavoil, "Weighted Blended OIT", JCGT 2013
- Alexander Sannikov, "Scalable Real-Time Global Illumination for Large Scenes", 2024
