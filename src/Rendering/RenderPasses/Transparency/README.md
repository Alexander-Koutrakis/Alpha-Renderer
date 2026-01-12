# Order-Independent Transparency (OIT)

This pass implements Weighted Blended Order-Independent Transparency (WBOIT), enabling correct rendering of overlapping transparent surfaces without requiring depth sorting. Transparent objects receive full PBR lighting including shadows.

## Weighted Blended OIT

### The Key Insight

Instead of requiring a specific draw order, WBOIT uses a weighted average where the weight function approximates depth ordering. Closer fragments receive higher weights, naturally dominating the blend without explicit sorting.

The weight function combines depth and alpha:
- Depth component: Closer fragments get exponentially higher weights
- Alpha component: More opaque fragments have more influence

### Two-Buffer Approach

The pass renders to two buffers simultaneously:

**Accumulation Buffer (RGBA16F)**
Stores the weighted sum of color contributions. Each fragment adds its color multiplied by alpha and weight. The alpha channel accumulates the sum of (alpha × weight) for normalization.

**Revealage Buffer (R8)**
Tracks how much of the background shows through. Uses multiplicative blending—each fragment multiplies by (1 - alpha), so the final value represents the product of all transmittances.

### Blend States

The accumulation buffer uses additive blending (ONE, ONE). Each fragment's contribution adds to the running sum.

The revealage buffer uses multiplicative blending (ZERO, ONE_MINUS_SRC). Each fragment multiplies the existing value by its transmittance.

### Composition

In the composition pass, the accumulated color is divided by the accumulated weight to get the average transparent color. This is then blended with the opaque background using the revealage value:

Final = AverageTransparent × (1 - Revealage) + Opaque × Revealage

## Pipeline Configuration

### Render Pass Setup

The transparency render pass has two color attachments (accumulation and revealage) plus a depth attachment. The accumulation buffer clears to zero. The revealage buffer clears to 1.0 (fully transparent). The depth attachment is loaded from the geometry pass—it's not cleared.

### Depth Configuration

Depth testing is enabled so transparent surfaces are correctly occluded by opaque geometry. However, depth writing is disabled. This is critical—if transparent surfaces wrote depth, they would occlude each other incorrectly.

## Full PBR Lighting

Transparent surfaces receive the same lighting as opaque geometry. The fragment shader evaluates the full Cook-Torrance BRDF for each light, including shadow map lookups. This ensures visual consistency between transparent and opaque materials.

### Descriptor Sets

The transparency pass binds all the same resources as the lighting pass:
- Camera and scene lighting uniforms
- Light array with all scene lights
- Shadow map samplers for all light types
- Model and normal matrix buffers
- Material textures

### Fragment Shader Flow

The shader samples material properties, evaluates PBR lighting with shadows, then applies the WBOIT weight function. The weighted color goes to the accumulation buffer; the alpha goes to the revealage buffer.

## Weight Function

The weight function is crucial for quality. It must:
- Give higher weights to closer fragments
- Scale with alpha (more opaque = more influence)
- Stay within reasonable bounds to avoid precision issues

The implementation uses a depth-based weight with quartic falloff. Close fragments (low depth) receive weights approaching the maximum of 3000. Far fragments (high depth) receive weights approaching the minimum of 0.01. Alpha modulates this base weight.

### Post-Pass

The accumulation and revealage buffers transition to SHADER_READ_ONLY_OPTIMAL for the composition pass to sample.

## Limitations and Artifacts

### Known Issues

1. **Color bleeding**: When surfaces at similar depths have very different colors, the weighted average can produce incorrect intermediate colors
2. **High alpha overlap**: Many overlapping surfaces with high alpha values can saturate the accumulation buffer
3. **Silhouette edges**: Sharp transitions between transparent and opaque regions may show artifacts

### Mitigation

- Use reasonable alpha values; avoid stacking many nearly-opaque layers
- For critical transparency (like UI or cutscenes), consider depth peeling
- Tune the weight function parameters for your specific content

## Performance

### Cost Breakdown

| Component | Cost |
|-----------|------|
| Vertex processing | Same as opaque geometry |
| Fragment shading | Full PBR evaluation per fragment |
| Blend operations | Two render targets with blending |
| Composition | Single fullscreen pass |

### Memory

| Buffer | Format | Size (1080p) |
|--------|--------|--------------|
| Accumulation | RGBA16F | 16 MB |
| Revealage | R8 | 2 MB |

### Optimization Notes

WBOIT requires no depth sorting, eliminating the O(N log N) CPU cost of sort-based approaches. The single geometry pass handles any number of overlapping layers with constant cost. The composition pass is a simple fullscreen operation with minimal overhead.

## References

- McGuire & Bavoil, "Weighted Blended Order-Independent Transparency", JCGT 2013
