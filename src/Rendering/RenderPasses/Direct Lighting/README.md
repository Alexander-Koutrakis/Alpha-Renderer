# Direct Lighting Pass

The Direct Lighting Pass evaluates physically-based shading for all scene lights using the G-Buffer data. This is where the Cook-Torrance BRDF, shadow mapping, and image-based lighting come together to produce the final lit image.

## Purpose

This full-screen pass reads the G-Buffer and computes:

- Direct illumination from all lights (directional, spot, point)
- Shadow attenuation using cascaded and cubemap shadow maps
- Image-based lighting (diffuse irradiance and specular reflections)
- Incident light buffer for Radiance Cascades GI

## Outputs

| Target | Format | Contents |
|--------|--------|----------|
| Light Result | RGBA16F | Final lit color (direct + IBL) |
| Incident Buffer | RGBA16F | Pre-albedo irradiance for GI |

The incident buffer stores light arriving at each surface before albedo multiplication. This allows the Radiance Cascades pass to compute indirect lighting with correct color bleeding—if a red wall receives white light, the incident buffer records white, and the GI system can bounce red light to nearby surfaces.

## Cook-Torrance BRDF

### The Microfacet Model

Surfaces are modeled as collections of tiny perfect mirrors called microfacets. The BRDF describes how these microfacets statistically reflect light based on surface roughness. The specular term combines three functions: the normal distribution, Fresnel reflectance, and geometry/masking.

### Normal Distribution Function (GGX)

The normal distribution function models how microfacet normals are distributed around the surface normal. With low roughness, the distribution has a sharp peak, producing mirror-like reflections. With high roughness, the distribution spreads wide, creating blurry highlights.

GGX (also known as Trowbridge-Reitz) is preferred over older models like Blinn-Phong because it has a longer tail, creating more realistic highlight falloff at grazing angles.

### Fresnel (Schlick Approximation)

The Fresnel effect describes how reflectivity increases at grazing angles. Looking straight at a surface, you see mostly the surface color. Looking at a steep angle, you see mostly reflection. The Schlick approximation efficiently computes this effect using the reflectance at normal incidence (F0) and the view angle.

For dielectrics (non-metals), F0 is approximately 0.04 regardless of color. For metals, F0 equals the albedo color, which is why metals have colored reflections.

### Geometry/Masking-Shadowing (Smith-Schlick)

At the microscopic level, microfacets can shadow each other. The geometry function accounts for this self-shadowing, reducing the specular contribution when the view or light direction is at a grazing angle. The Smith formulation separates the masking (view direction) and shadowing (light direction) terms, which are then combined.

### Energy Conservation

The shader ensures energy is conserved by splitting incoming light between diffuse and specular based on the Fresnel term. Whatever is reflected specularly cannot also be scattered diffusely. Metals have no diffuse component at all—their color comes entirely from specular reflection.

## Light Types

### Unified Light Structure

All light types share a common data structure containing position, color, intensity, direction, range, and attenuation parameters. A type field distinguishes directional, spot, and point lights. Additional fields track shadow map indices and matrices.

### Distance Attenuation

Point and spot lights use quartic falloff for distance attenuation. The light intensity decreases as the fourth power of the distance ratio, with a smooth window function at the range boundary to avoid harsh cutoffs. This produces more physically plausible falloff than simple inverse-square while remaining artist-friendly.

### Spot Angle Attenuation

Spot lights add angular attenuation based on the angle between the light direction and the vector to the fragment. The falloff is controlled by inner and outer cone angles, with smooth interpolation between them.

## Shadow Mapping

### Cascaded Shadow Maps (Directional)

Directional lights use four cascades, each covering a portion of the view frustum. The shader determines which cascade to use based on the fragment's view-space depth. Near the boundary between cascades, it blends between them to avoid visible seams.

### PCF Soft Shadows

Hard shadow edges look unrealistic. The shader softens them using percentage-closer filtering with a 16-sample Poisson disk pattern. The disk is rotated per-pixel using a random angle derived from screen position, breaking up the regular sampling pattern that would otherwise create visible banding.

### Point Light Cubemap Shadows

Point lights emit in all directions, requiring cubemap shadow maps. The shader samples the cubemap using the direction from the light to the fragment, comparing the stored distance against the actual distance to determine shadow.

## Image-Based Lighting

### Diffuse IBL

For diffuse lighting from the environment, the shader samples the lowest mip level of the environment cubemap in the normal direction. This heavily blurred version approximates the irradiance that would result from integrating the environment over the hemisphere.

### Specular IBL

Specular reflections sample the environment cubemap in the reflection direction. The mip level is chosen based on roughness—smooth surfaces sample sharp mips, rough surfaces sample blurry mips. This approximates the prefiltered environment that would result from convolving with the BRDF.

## Descriptor Layout

The pass binds multiple descriptor sets:

- **Set 0**: Scene lighting uniform buffer with camera data and ambient/reflection intensities
- **Set 1**: Light array uniform buffer containing all lights in the scene
- **Set 2**: G-Buffer samplers for position, normal, albedo, and material
- **Set 3**: Shadow map samplers for directional arrays, spot 2D textures, and point cubemaps
- **Set 4**: Shadow matrices stored in a shader storage buffer
- **Set 5**: Environment cubemap for IBL
- **Set 6**: Cascade split distances for CSM

## Fullscreen Rendering

The pass renders a single triangle that covers the entire screen. No vertex buffer is needed—the vertex shader generates positions and UVs procedurally from the vertex index. This is more efficient than a quad, which would have two triangles sharing an edge.

## Performance Notes

### Light Loop

All lights are evaluated in a single pass through the light array. The shader iterates over the light count, evaluating each light's contribution and accumulating the result. This unified approach simplifies the code compared to separate passes per light type.

### Shadow Map Sampling

Shadow maps are stored in arrays by light type:
- Directional lights use 2D array textures with one layer per cascade
- Spot lights use a 2D texture array
- Point lights use a cubemap array

### Bandwidth Considerations

The pass reads the entire G-Buffer (approximately 36 bytes per pixel) and writes two RGBA16F outputs (16 bytes per pixel). Shadow map sampling adds variable bandwidth depending on the PCF kernel size and number of shadow-casting lights.
