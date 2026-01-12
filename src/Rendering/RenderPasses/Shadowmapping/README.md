# Shadow Mapping Pass

The Shadow Pass generates depth maps from each light's perspective, enabling the lighting pass to determine which surfaces are in shadow. This implementation supports directional lights with cascaded shadow maps, spot lights with perspective shadow maps, and point lights with cubemap shadows.

## Shadow Map Types

### Directional Lights - Cascaded Shadow Maps (CSM)

Directional lights like the sun affect the entire scene, requiring special handling. A single shadow map covering the whole view frustum would have terrible resolution near the camera. Cascaded shadow maps solve this by dividing the view frustum into segments, each with its own shadow map.

```
┌─────────────────────────────────────────────────────────────────┐
│                    CASCADED SHADOW MAPS                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Camera ──────────────────────────────────────────────────────► │
│     │                                                           │
│     │  Cascade 0    Cascade 1      Cascade 2       Cascade 3    │
│     │  [0-25m]      [25-75m]       [75-175m]       [175-300m]   │
│     │  ┌────┐       ┌────────┐     ┌────────────┐  ┌──────────┐ │
│     │  │2048│       │  2048  │     │    2048    │  │   2048   │ │
│     │  │ px │       │   px   │     │     px     │  │    px    │ │
│     │  └────┘       └────────┘     └────────────┘  └──────────┘ │
│     │                                                           │
│     │  High detail   Medium        Lower detail    Far shadows  │
│     │  near camera   detail        for mid-range   (optional)   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

Four cascades cover distances from the camera out to 300 meters. Each cascade uses a 2048×2048 shadow map, but the near cascades cover a smaller world-space area, providing higher effective resolution where it matters most.

### Spot Lights - Perspective Shadow Maps

Spot lights use standard perspective projection from the light's position. The projection frustum matches the spot light's cone angle, ensuring the shadow map covers exactly the illuminated area. Each spot light has a 1024×1024 shadow map.

### Point Lights - Cubemap Shadows

Point lights emit in all directions, requiring six shadow map faces arranged as a cubemap. Each face uses a 90° field-of-view perspective projection looking along one of the six axis directions (±X, ±Y, ±Z). Each face is 512×512 pixels.

## Pipeline Architecture

### Render Pass Configuration

The shadow render pass uses only a depth attachment—no color output is needed. The depth buffer is cleared at the start and stored for later sampling. The final layout is SHADER_READ_ONLY_OPTIMAL, ready for the lighting pass to sample.

## Instanced Rendering

### Push Constants

Each draw call receives push constants containing:
- Light position and range (for point light distance encoding)
- Light matrix index (which view-projection matrix to use)
- Model matrix buffer offset (where this batch's transforms start)
- Light type (to select the appropriate transformation logic)


### Batching Strategy

Objects are grouped by light to minimize render pass switches. For each shadow-casting light, the pass iterates through all cascades (for directional) or faces (for point), rendering the appropriate shadow casters into each.

## Framebuffer Management

### Per-Light, Per-Frame Resources

The renderer maintains separate framebuffers for each light, frame-in-flight, and cascade/face:

- Directional lights: 4 lights × 3 frames × 4 cascades = 48 framebuffers
- Spot lights: 8 lights × 3 frames = 24 framebuffers  
- Point lights: 8 lights × 3 frames × 6 faces = 144 framebuffers

## Performance Considerations

### Culling

Before rendering into a shadow map, objects are culled against the light's frustum. Only objects that could cast visible shadows are rendered. For cascaded shadow maps, each cascade has its own culling pass since different objects may be visible in different cascades.
