# Radiance Cascades - Screen-Space Global Illumination

This implementation provides real-time diffuse global illumination using the Radiance Cascades technique. It captures indirect lighting—light that bounces off surfaces before reaching the camera—enabling color bleeding, ambient occlusion, and realistic light transport without expensive ray tracing hardware.

## The Core Idea

### Radiance Intervals

Instead of tracing rays to infinity, Radiance Cascades divides the ray into intervals:

```
┌─────────────────────────────────────────────────────────────────┐
│                     RAY INTERVAL STRUCTURE                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Surface ──┬──────┬──────────────┬────────────────────────────► │
│            │      │              │                              │
│            │ C0   │     C1       │           C2                 │
│            │      │              │                              │
│         [0,0.32] [0.32,1.28]  [1.28,5.12]    ...                │
│                                                                 │
│  Cascade 0: Near-field, high angular resolution                 │
│  Cascade 1: Mid-field, medium resolution                        │
│  Cascade 2: Far-field, lower resolution                         │
│                                                                 │
│  Each cascade handles a specific distance range                 │
│  Higher cascades = longer intervals, fewer directions           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### The Penumbra Condition

The key insight is that spatial resolution can decrease with distance while angular resolution increases. Far-away surfaces subtend smaller solid angles, so we need finer angular sampling to capture them—but we don't need as many probe locations since indirect lighting varies smoothly over distance.

This means:
- **Cascade 0**: Probe every 2 pixels, 2×2 directions, covers 0 to 0.32 world units
- **Cascade 1**: Probe every 4 pixels, 4×4 directions, covers 0.32 to 1.28 world units
- **Cascade 2**: Probe every 8 pixels, 8×8 directions, covers 1.28 to 5.12 world units
- And so on...

The 4× branching factor (2× per dimension) maintains balance: each cascade has 4× more directions but 4× fewer probes, keeping total work roughly constant.

### Interval Merging

Higher cascades provide "far-field" radiance that gets merged into lower cascades:

```
┌─────────────────────────────────────────────────────────────────┐
│                      CASCADE MERGING                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Cascade 2 (8×8 tiles, far)                                     │
│       │                                                          │
│       │ Each C2 texel covers 4 C1 texels                        │
│       ▼                                                          │
│  Cascade 1 (4×4 tiles, mid)                                     │
│       │                                                          │
│       │ Each C1 texel covers 4 C0 texels                        │
│       ▼                                                          │
│  Cascade 0 (2×2 tiles, near)                                    │
│       │                                                          │
│       │ Final radiance = C0.radiance + C0.beta × C1.merged      │
│       ▼                                                          │
│  Screen pixels                                                   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

Beta (β) represents transmittance—how much of the far-field radiance passes through. A beta of 0 means the interval hit something and blocks far-field light. A beta of 1 means the interval was empty, allowing far-field radiance to pass through.

## Pipeline Stages

The Radiance Cascades pass executes four compute stages in sequence:

1. **Compute Cascade Bands**: Calculate the distance intervals for each cascade based on the 4× branching rule
2. **Build Depth Pyramid**: Convert hardware depth to linear depth and build a min/max mip chain for efficient ray marching
3. **Build Cascades**: For each cascade, trace rays through its interval and record radiance and transmittance
4. **Merge Cascades**: Propagate radiance from far cascades to near, combining intervals
5. **Resolve Indirect**: Sample cascade 0 to compute final indirect lighting for each pixel

## Depth Pyramid

### Purpose

The depth pyramid enables efficient ray-depth intersection testing. Instead of checking every pixel along a ray, we can skip large empty regions by checking coarse mip levels first.

### Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                      DEPTH PYRAMID                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Mip 0: 1920×1080  ─  Per-pixel linear depth                    │
│  Mip 1: 960×540    ─  Min/Max of 2×2 region                     │
│  Mip 2: 480×270    ─  Min/Max of 4×4 region                     │
│  Mip 3: 240×135    ─  Min/Max of 8×8 region                     │
│  ...                                                             │
│                                                                  │
│  Format: RG32F (R = min depth, G = max depth)                   │
│                                                                  │
│  Usage: If ray depth is outside [min, max] of a mip cell,       │
│         we know there's no geometry in that region              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Seed Pass

The first pass converts hardware depth (which is non-linear due to perspective projection) to linear view-space depth. This linearization is essential for accurate distance calculations during ray marching.

### Downsample Pass

Each subsequent mip level computes the minimum and maximum depth from its parent's 2×2 region. This hierarchical structure allows rays to quickly skip empty space by checking coarse levels before drilling down to fine detail.

## Cascade Building

### Probe Layout

Each cascade has a grid of probes—sample points distributed across the screen. The probe stride doubles with each cascade level: cascade 0 has a probe every 2 pixels, cascade 1 every 4 pixels, and so on.

### Atlas Structure

Each cascade stores its probes in a 2D texture atlas:

```
┌─────────────────────────────────────────────────────────────────┐
│                      CASCADE ATLAS                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────┬────┬────┬────┐                                          │
│  │P0,0│P1,0│P2,0│... │  Each cell is a tileSize × tileSize     │
│  ├────┼────┼────┼────┤  block of direction samples              │
│  │P0,1│P1,1│P2,1│... │                                          │
│  ├────┼────┼────┼────┤  Cascade 0: 2×2 tiles                    │
│  │P0,2│P1,2│P2,2│... │  Cascade 1: 4×4 tiles                    │
│  ├────┼────┼────┼────┤  Cascade 2: 8×8 tiles                    │
│  │... │... │... │... │                                          │
│  └────┴────┴────┴────┘                                          │
│                                                                  │
│  Atlas size = (screenWidth/probeStride) × tileSize              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

Each tile texel maps to a direction on the upper hemisphere using octahedral mapping. This encoding efficiently covers the hemisphere with minimal distortion at the edges.

### Ray Marching

For each direction in each probe, the shader marches through the cascade's interval. At each step, it projects the sample position to screen space and checks the depth pyramid. If the ray depth falls within the min/max range at a coarse mip level, it drills down to finer levels for precise intersection testing.

When a valid hit is found, the shader samples the G-Buffer to get the surface's albedo and the incident light buffer to determine how much light is arriving at that point. This radiance is then recorded along with a beta value indicating how much of the interval was traversed before the hit.

### Jittering for Temporal Stability

To reduce banding artifacts from discrete sampling, the implementation jitters ray origins, directions, and march distances. The golden ratio provides good temporal distribution, spreading samples across frames to accumulate a cleaner result.

## Cascade Merging

After building all cascades, the merge pass propagates radiance from far to near. Starting from the highest cascade, each level fetches the corresponding texels from the next cascade and blends based on local beta values.

The merge formula combines the current cascade's radiance with the transmittance-weighted radiance from the parent cascade. If the current interval hit something (low beta), it blocks most of the far-field contribution. If it was empty (high beta), the far-field radiance passes through.

## Indirect Resolve

The final stage samples cascade 0 and integrates over the hemisphere to compute diffuse indirect lighting for each pixel. The shader builds a tangent-space basis from the surface normal and samples multiple directions across the hemisphere.

For each direction, it performs bilinear interpolation between the four nearest probes to get smooth results. The radiance from each direction is weighted by the cosine of the angle with the surface normal (NdotL) and accumulated.

Any remaining transmittance (rays that didn't hit geometry) is filled with skybox samples, ensuring the indirect lighting integrates naturally with the environment.

## Temporal Accumulation

To reduce noise from jittering, the result is blended with previous frames. The shader reprojects the current pixel to its previous frame location using the inverse view-projection matrix. It then validates the history by comparing depth and normal similarity.

When the history is valid (the surface hasn't moved or changed orientation), more weight is given to accumulated history for a stable result. When invalid (disocclusion or motion), the current frame dominates to avoid ghosting.

## Implementation Details

### Constants

The implementation uses 6 cascades with a base tile size of 2×2 directions. Probes start at every 2 pixels for cascade 0. The base interval length is 0.08 world units (8 centimeters), with a 15% overlap between adjacent cascade intervals to reduce seams.

### Interval Distances

Following the 4× branching rule, the cascades cover exponentially increasing distances:

| Cascade | Start | End | Length |
|---------|-------|-----|--------|
| 0 | 0.00 | 0.32 | 0.32 |
| 1 | 0.32 | 1.28 | 0.96 |
| 2 | 1.28 | 5.12 | 3.84 |
| 3 | 5.12 | 20.48 | 15.36 |
| 4 | 20.48 | 81.92 | 61.44 |
| 5 | 81.92 | 327.68 | 245.76 |

### Compute Dispatch

The build pass dispatches one thread per atlas texel, with workgroups of 8×8 threads. The resolve pass dispatches one thread per screen pixel. Memory barriers between stages ensure writes are visible before subsequent reads.

## Performance Analysis

### Memory Usage (1080p)

| Resource | Size | Notes |
|----------|------|-------|
| Depth Pyramid | ~8 MB | RG32F, full mip chain |
| Cascade 0 Atlas | ~4 MB | RGBA16F, 960×540 × 2×2 |
| Cascade 1 Atlas | ~4 MB | RGBA16F, 480×270 × 4×4 |
| Cascade 2 Atlas | ~4 MB | RGBA16F, 240×135 × 8×8 |
| ... | ... | Roughly constant per cascade |
| GI Output | ~16 MB | RGBA16F, full resolution |
| History Buffers | ~32 MB | Position + GI history |
| **Total** | **~80 MB** | |

### GPU Time (RTX 3070, 1080p)

| Stage | Time |
|-------|------|
| Depth Pyramid | 0.2 ms |
| Cascade Build (all 6) | 3.2 ms |
| Cascade Merge | 0.5 ms |
| Resolve | 1.5 ms |
| **Total** | **~5.5 ms** |

### Scaling

- **Resolution**: Roughly linear scaling—doubling resolution approximately doubles the time
- **Cascade count**: Each additional cascade adds approximately 0.6 ms
- **Tile size**: Larger tiles mean more directions per probe, improving quality at the cost of performance

## Limitations

1. **Screen-space only**: Cannot capture GI from geometry that's off-screen or occluded
2. **Single bounce**: Only computes first-order indirect lighting; multi-bounce effects are not captured
3. **Temporal lag**: Fast-moving objects may exhibit GI artifacts as the temporal accumulation catches up
4. **Thickness assumptions**: Thin geometry may cause light leaking since the depth buffer only captures front faces

## References
- Alexander Sannikov, "Scalable Real-Time Global Illumination for Large Scenes", 2024
