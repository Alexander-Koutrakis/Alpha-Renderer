# Alpha Renderer

**Alpha Renderer** is a real-time Vulkan-based renderer built to explore modern GPU-driven rendering techniques from the ground up. It focuses on correctness, performance, and architectural clarity rather than engine-level features, serving as a technical playground for experimenting with contemporary real-time graphics algorithms.

The project implements a full deferred rendering pipeline with physically-based materials, cascaded shadow maps, screen-space global illumination via Radiance Cascades, and order-independent transparency — all written directly on top of Vulkan’s explicit API. Every system is designed to expose how data flows from scene representation to GPU execution, emphasizing low-level control, synchronization, and efficient batching.

Alpha Renderer is **not a game engine**. It is a renderer-first project intended for learning, research, and experimentation with modern real-time rendering techniques, making it suitable for graphics programmers who want to understand *how* these systems are built rather than simply use them.

---

## Showcase

### Radiance Cascades Global Illumination

<table>
  <tr>
    <th>No Global Illumination</th>
    <th>Radiance Cascades GI</th>
  </tr>
  <tr>
    <td>
      <img src="https://github.com/user-attachments/assets/6f9870c1-1ff2-4c06-a280-ad09ea1a4ff5" />
    </td>
    <td>
      <img src="https://github.com/user-attachments/assets/87bc2baf-c166-4e81-9b48-b0aaea9af7bd" />
    </td>
  </tr>
</table>

### Real-Time Rendering Demo

https://github.com/user-attachments/assets/a0214d83-2f8e-4207-be3d-8a5bbaca1ed1

*Deferred PBR lighting, cascaded shadows, screen-space global illumination, transparency, and post-processing running in real time.*

---

### Scene Examples

<img src="https://github.com/user-attachments/assets/10ae5c26-5f8f-44fa-be84-cfae5648ef84" />
<img src="https://github.com/user-attachments/assets/de14e02d-ec43-49ee-931f-947729481580" />

---

## Features

### Rendering Pipeline
- Deferred shading with a G-Buffer (position, normal, albedo, material properties)
- Physically-Based Rendering using the Cook-Torrance BRDF with GGX distribution
- Cascaded Shadow Maps for directional lights with PCF filtering
- Screen-space global illumination using Radiance Cascades
- Weighted Blended Order-Independent Transparency (WBOIT)

### Post-Processing
- Subpixel Morphological Anti-Aliasing (SMAA)

### Performance & Architecture
- Instanced rendering with material batching to minimize draw calls
- Octree-based spatial acceleration for efficient culling of renderables and lights

---

### Deferred Rendering Pipeline

The renderer uses a deferred shading architecture that separates geometry rendering from lighting calculations. All opaque surfaces are first rendered into a G-Buffer containing position, normal, albedo, and material properties. Lighting is then computed in screen-space, making the cost independent of scene complexity. This approach enables complex multi-light scenarios without the performance penalty of forward rendering.

### Physically-Based Rendering

Materials are rendered using the Cook-Torrance BRDF with GGX microfacet distribution. This physically-grounded model accurately simulates how light interacts with surfaces of varying roughness and metallicity. The energy-conserving formulation ensures materials look correct under any lighting condition, from harsh direct sunlight to soft ambient illumination.

### Cascaded Shadow Maps

Directional lights use cascaded shadow mapping to provide high-resolution shadows near the camera while still covering distant geometry. Four cascades divide the view frustum, each with its own shadow map, allowing shadow detail to scale appropriately with distance. Soft shadow edges are achieved through PCF filtering with a rotated Poisson disk pattern.

### Radiance Cascades Global Illumination

Screen-space global illumination is computed using the Radiance Cascades technique. This hierarchical approach divides indirect lighting into distance-based intervals, with each cascade handling a specific range. Near-field lighting uses high spatial resolution with few directions, while far-field lighting uses fewer probes but more directions. The cascades merge from far to near, propagating indirect radiance throughout the scene. This enables real-time color bleeding and ambient lighting without requiring ray tracing hardware.

### Order-Independent Transparency

Transparent surfaces are rendered using Weighted Blended Order-Independent Transparency (WBOIT). This technique eliminates the need for depth sorting by using a weighted average where closer fragments naturally dominate the blend. Transparent objects receive full PBR lighting including shadows, ensuring visual consistency with opaque geometry.

### Subpixel Morphological Anti-Aliasing

The final image is anti-aliased using SMAA, a post-process technique that detects and smooths jagged edges. The three-pass approach (edge detection, blend weight calculation, neighborhood blending) provides high-quality results with minimal performance impact.

### Instanced Rendering with Material Batching

Objects are grouped by mesh and material to minimize GPU state changes. All instances sharing the same material are rendered in a single draw call, with per-instance transforms stored in a buffer. This batching strategy scales efficiently with scene complexity.

### Spatial Acceleration with Octree

The scene uses an octree for spatial partitioning, enabling O(log N) frustum queries instead of linear searches. Both renderables and lights are stored in the octree, allowing efficient culling for both camera visibility and shadow caster determination.


## Project Structure

```
src/
├── Engine/              Application lifecycle and main loop
├── ECS/                 Entity Component System
├── Scene/               Spatial data structures and scene management
├── Systems/             Game logic systems (camera, culling, lights)
├── Rendering/           Vulkan rendering pipeline
│   ├── Core/            Device, swapchain, buffers, pipelines
│   ├── RenderPasses/    Individual render passes (see detailed docs)
│   └── Resources/       GPU resources (textures, meshes, materials)
├── Resources/           Asset loading (glTF, textures)
├── Math/                AABB, frustum, octree
└── main.cpp             Entry point
```

## Scene Workflow

Scenes are authored in Unity Editor, which provides a familiar interface for placing objects, configuring materials, and setting up lights. A custom serialization tool exports the scene into a JSON format that the renderer can load at runtime.

The exported scene file (`assets/Scene/Scene.json`) contains:
- **Texture paths**: References to color and normal map textures
- **Mesh paths**: Serialized mesh data including vertices, indices, and submeshes
- **Material paths**: PBR material definitions with albedo, metallic, smoothness, and texture references
- **Game objects**: Entity hierarchy with transform, mesh renderer, and light components

This workflow allows rapid iteration on scene composition without recompiling the renderer, while keeping the runtime asset loading straightforward.

## Frame Execution Flow

Each frame follows a structured sequence that separates concerns and maximizes GPU utilization:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           FRAME EXECUTION                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. INPUT & CAMERA                                                          │
│     Process keyboard/mouse input and update camera matrices                 │
│                                                                             │
│  2. CULLING & BATCHING                                                      │
│     Query the octree with the view frustum to find visible objects          │
│     Group objects by material for instanced rendering                       │
│     Cull lights and compute shadow matrices for each cascade                │
│                                                                             │
│  3. RENDER PASSES                                                           │
│     Shadow Pass        → Render depth from each light's perspective         │
│     Geometry Pass      → Fill G-Buffer with surface properties              │
│     Skybox Pass        → Render environment cubemap                         │
│     Direct Light Pass  → Evaluate PBR lighting with shadows                 │
│     Radiance Cascades  → Compute screen-space global illumination           │
│     Transparency Pass  → Order-independent transparency (WBOIT)             │
│     Composition Pass   → Combine opaque, transparent, and GI layers         │
│     SMAA Passes        → Subpixel morphological anti-aliasing               │
│     Color Correction   → Tone mapping and final output                      │
│                                                                             │
│  4. PRESENT                                                                 │
│     Submit command buffer and present to swapchain                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Object Rendering Pipeline

The path from scene data to pixels follows a clear flow:

**Scene Registration**
When objects are loaded, they are inserted into an octree—a spatial data structure that partitions 3D space hierarchically. This enables O(log N) frustum queries instead of checking every object linearly.

**Frustum Culling**
Each frame, the camera's view frustum is used to query the octree. Only objects whose bounding boxes intersect the frustum are considered for rendering, dramatically reducing the number of draw calls.

**Material Batching**
Visible objects are grouped by their mesh, material, and submesh index. All instances sharing the same key are rendered together in a single instanced draw call, minimizing GPU state changes and maximizing throughput.

**GPU Rendering**
For each batch, the renderer binds the material's descriptor set, pushes the model matrix buffer offset, and issues an instanced draw command. This approach scales efficiently with scene complexity.

## Culling System

The renderer employs a two-level culling strategy:

**Camera Frustum Culling**
The octree is queried with the camera's view frustum to collect visible renderables. Objects are then separated into opaque and transparent lists for proper rendering order.

**Shadow Caster Culling**
For each shadow-casting light, a separate frustum query determines which objects need to be rendered into the shadow map. Directional lights use cascaded shadow maps, with each cascade having its own frustum.

**Light Culling**
Lights themselves are stored in an octree and culled against the camera frustum. Only lights that could affect visible pixels are processed, reducing both CPU and GPU overhead.

## FrameContext

The FrameContext is the central data structure that flows through the entire rendering pipeline. It encapsulates:

- **Command recording state**: The current command buffer and frame index
- **Camera data**: View and projection matrices, frustum planes, camera position
- **Culling results**: Arrays of material batches for both opaque and transparent geometry
- **GPU buffers**: Model matrices, normal matrices, camera uniforms, light arrays
- **Descriptor sets**: Pre-bound resource sets for shaders to access
- **G-Buffer images**: References for synchronization barriers between passes

This design keeps the rendering code clean by passing a single context object rather than numerous individual parameters.

## Detailed Documentation

Each major system has its own documentation with implementation details:

| Module | Description |
|--------|-------------|
| [ECS](ECS/README.md) | Entity Component System architecture |
| [Rendering](Rendering/README.md) | Vulkan rendering pipeline overview |
| [Geometry Pass](Rendering/RenderPasses/Geometry/README.md) | G-Buffer generation |
| [Direct Lighting](Rendering/RenderPasses/Direct%20Lighting/README.md) | PBR shading with shadows |
| [Shadow Mapping](Rendering/RenderPasses/Shadowmapping/README.md) | Cascaded and cubemap shadows |
| [Radiance Cascades](Rendering/RenderPasses/Radiance%20Cascades/README.md) | Screen-space global illumination |
| [Transparency](Rendering/RenderPasses/Transparency/README.md) | Order-independent transparency |

## Building

Build with CMake :
```
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .

```

## Dependencies

- **Vulkan 1.3** — Graphics API with explicit GPU control
- **GLFW** — Cross-platform window and input handling
- **GLM** — Mathematics library for graphics
- **stb_image** — Texture loading
- **Dear ImGui** — Immediate-mode debug UI
