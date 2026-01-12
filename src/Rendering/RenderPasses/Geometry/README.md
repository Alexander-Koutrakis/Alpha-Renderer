# Geometry Pass

The Geometry Pass is the foundation of the deferred rendering pipeline. It renders all opaque geometry into the G-Buffer, storing surface properties that will be used by subsequent lighting passes.

## Purpose

In deferred shading, we separate "what surfaces exist" from "how they're lit." The Geometry Pass handles the first part:

- Rasterize all opaque meshes
- Sample material textures (albedo, normal, metallic/smoothness, AO)
- Transform normals to world space
- Output packed surface data to multiple render targets

This decoupling means lighting complexity is independent of scene geometry complexity. Whether the scene has one light or a hundred, the geometry pass runs exactly once.

## G-Buffer Layout

The pass outputs to five attachments simultaneously:

| Attachment | Format | Contents | Why This Format? |
|------------|--------|----------|------------------|
| Position | RGBA32F | World XYZ, validity W | Full precision for world-space calculations |
| Normal | RGBA16F | World normal (0-1 encoded) | Half-float sufficient for directions |
| Albedo | RGBA8 | Base color RGB, alpha | sRGB-friendly, alpha for masking |
| Material | RGBA8 | Metallic, Smoothness, AO | Packed PBR parameters |
| Depth | D32F | Hardware depth | Required for depth testing |

## Pipeline Configuration

### Vertex Input

The vertex shader receives standard mesh attributes:
- Position (vec3) for spatial location
- Normal (vec3) for surface orientation
- UV (vec2) for texture mapping
- Tangent (vec4) for normal mapping, with the W component indicating bitangent handedness

### Descriptor Sets

The geometry pass binds three descriptor sets:
- **Set 0**: Camera uniform buffer containing view, projection, and combined matrices
- **Set 1**: Model matrices stored in a shader storage buffer, indexed by instance ID
- **Set 2**: Material data including a uniform buffer for scalar properties and samplers for textures

### Bandwidth

G-Buffer writes are the main bandwidth cost of this pass:
- Position: 16 bytes per pixel
- Normal: 8 bytes per pixel
- Albedo: 4 bytes per pixel
- Material: 4 bytes per pixel
- Depth: 4 bytes per pixel
- **Total: 36 bytes per pixel**

At 1080p resolution, this amounts to approximately 75 MB per frame just for G-Buffer writes.

### Optimizations Applied

1. **Material batching** reduces descriptor set binds to once per unique material
2. **Instanced rendering** issues one draw call per mesh/material combination regardless of instance count
3. **Push constants** for matrix buffer offsets avoid descriptor updates between batches
