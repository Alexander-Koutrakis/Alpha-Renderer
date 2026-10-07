# Descriptors phase: implementation plan (for review)

Branch: `descriptors` (from `main` @ 455a61a). Status: plan only, no code changed yet.
Parent plan: `PLAN.md`, Phase 3 ("Vulkan struct descriptors"). The barrier slice is done (`src/Rendering/Core/barriers.*`);
this is the next slice. Read `AGENTS.md` for the repo rules and the gate (`just check`).

## 1. Context

C++17 Vulkan renderer (deferred PBR, cascaded shadows, Radiance Cascades GI, WBOIT, SMAA), MinGW GCC 15 on Windows.
The owner likes the Vulkan style of passing small structs around. The barrier slice introduced value structs
(`ImageBarrierDesc`, `BufferBarrierDesc`, ...) plus one recording function and migrated every call site. This phase
does the same for descriptor sets.

## 2. The problem

Facts, checked against the source at `main`:

| Item | Where | Count |
| --- | --- | --- |
| Hand-built `VkDescriptorSetLayoutCreateInfo` + `vkCreateDescriptorSetLayout` | `rendering_resources.cpp` (lines ~870-1420) | 20 |
| Same, elsewhere | `resource_manager.cpp` | 1 |
| Tutorial `DescriptorSetLayout` class + `Builder` (RAII) | `descriptors.{hpp,cpp}` | 0 callers: dead code |
| Hand-built `VkDescriptorSetAllocateInfo` + `vkAllocateDescriptorSets` | `rendering_resources.cpp` (`createDescriptorSets`, lines ~1430-2210) | ~20 |
| `VkWriteDescriptorSet` filled field by field | `rendering_resources.cpp` 31 sites, `light_pass.cpp` 1, `shadow_pass.cpp` 1 | 33 |
| `vkUpdateDescriptorSets` calls | `rendering_resources.cpp` 15, `shadow_pass.cpp` 1 | 16 |

`rendering_resources.cpp` is 2833 lines; roughly 1,300 of them are this boilerplate.

Concrete defects, not just verbosity:

1. **Repeated 15-25 line blocks that differ only in bindings.** Each layout is: bindings array, create-info, create,
   error check, debug name, `std::cout`. A wrong stage flag or type is easy to introduce and hard to see in review.
2. **`w0..w7` style code** (RC build/resolve sets): one local `VkWriteDescriptorSet` per binding, each assigned field
   by field and pushed into a vector. Binding numbers are literals (`1 + b`).
3. **Existing `DescriptorWriter` is half-adopted and unsafe.** `descriptors.hpp` already has a tutorial-style writer
   (`writeBuffer`, `writeImage`, `build`, `overwrite`), used by `material.cpp` and ~5 sites in `rendering_resources.cpp`.
   - It stores raw pointers to the caller's `VkDescriptorBufferInfo` / `VkDescriptorImageInfo`. A caller whose info
     goes out of scope leaves a dangling pointer inside the writer.
   - `writeImage` hardcodes `COMBINED_IMAGE_SAMPLER`; there is no storage image, no sampled image, no array binding
     (`descriptorCount > 1`), so ~30 sites cannot use it.
4. **Pool sizes are hand-counted.** `rendering_resources.cpp:~818-855` derives `totalDescriptorSets`,
   `uniformBufferCount`, `storageBufferCount`, `combinedImageSamplerCount`, `storageImageCount` from comments like
   "18 core sets" and "gbuffer4 + depth + incident". Adding a binding to a layout silently makes the pool too small
   (runtime `VK_ERROR_OUT_OF_POOL_MEMORY`, or worse, validation noise).
5. **Two existing validation errors point at this area** (see section 6): shaders use descriptors that the pipeline
   layout does not declare (`VUID-VkComputePipelineCreateInfo-layout-07988`, `VUID-VkGraphicsPipelineCreateInfo-layout-07988`).
   Hand-built layouts make such drift easy; they are not fixed in this phase (see non-goals).

## 3. Goals and non-goals

Goals
- One struct-based way to describe a layout, allocate a set from it, and write it.
- Migrate every site; delete the legacy path (no second way left in the tree).
- Make the writer safe (no dangling info pointers) and expressive enough for every current binding type.
- Behaviour identical: same layouts, same writes, same order.

Non-goals (separate branches)
- Fixing the two validation errors (that changes layouts; belongs in `hygiene`).
- Images / views / samplers / pipeline layouts / render passes (other `structs` slices).
- Moving to descriptor indexing, push descriptors or `VK_KHR_dynamic_rendering`.
- Splitting `rendering_resources.cpp` into per-pass owners (later phase, but this phase makes it much easier).

## 4. Proposed design

Header: extend `src/Rendering/Core/descriptors.hpp` (and `.cpp`); no new module.

### 4.1 Layout description

```cpp
struct DescriptorBindingDesc {          // one binding of a set layout
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;
    uint32_t count = 1;
    // factories read as the Vulkan type they create
    static DescriptorBindingDesc uniformBuffer(uint32_t binding, VkShaderStageFlags stages);
    static DescriptorBindingDesc storageBuffer(uint32_t binding, VkShaderStageFlags stages);
    static DescriptorBindingDesc combinedImageSampler(uint32_t binding, VkShaderStageFlags stages, uint32_t count = 1);
    static DescriptorBindingDesc storageImage(uint32_t binding, VkShaderStageFlags stages, uint32_t count = 1);
};

struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBindingDesc> bindings;
    const char* debugName = nullptr;
};

VkDescriptorSetLayout createDescriptorSetLayout(Device&, const DescriptorSetLayoutDesc&);  // checks + names it
```

Consecutive bindings (G-buffer: 4 samplers on bindings 0..3) get a small helper
`DescriptorSetLayoutDesc::addRange(first, count, type, stages)`, replacing the `for` loops that fill binding arrays today.

Return type is a raw `VkDescriptorSetLayout`, because `RenderingResources` stores raw handles and destroys them in
`cleanup()` and every other file takes the raw handle (e.g. `material.cpp`, the pass `CreateInfo` structs). The unused
RAII `DescriptorSetLayout` class and its `Builder` are deleted (nothing calls them; open question Q2).

### 4.2 Writes

Rework `DescriptorWriter` rather than adding a second class:
- Own copies of the infos (small `std::vector`/`std::deque` inside the writer), so no caller lifetime rules.
- `buffer(binding, type, VkDescriptorBufferInfo)`, `image(binding, type, VkDescriptorImageInfo)`,
  `images(binding, type, ArrayView<VkDescriptorImageInfo>)` (array bindings), keeping `uniformBuffer` / `combinedImage`
  conveniences that read like the layout factories.
- Two terminal operations: `allocateAndWrite() -> VkDescriptorSet` (allocates from the pool the writer was built
  with; throws on failure) and `update(set)` for re-writing an existing set. The existing `build(set)` /
  `overwrite(set)` names stay as thin aliases only while migrating, then are removed.
- Stable write order = call order (important for verification, section 5).

Per-site result, e.g. today's 18-line "light array" block becomes:

```cpp
lightArrayDescriptorSets[i] = DescriptorWriter(lightArrayDescriptorSetLayout, *descriptorPool)
    .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, lightArrayUniformBuffers[i]->descriptorInfo())
    .allocateAndWrite();
setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, ..., "LightArrayDescriptorSet_Frame" + std::to_string(i));
```

### 4.3 Pool sizing (optional slice C)

Derive pool sizes from the layout descriptions instead of by hand: keep a registry of
`(DescriptorSetLayoutDesc, setCount)` and sum `count * setCount` per descriptor type. This removes defect 4 at the
root. It needs the layouts to be data (slice A), so it only becomes possible after A.
Not required for the migration; included because it is the real fix for a latent bug (Q3).

## 5. Migration order and verification

Slices, each its own commit; build and verification after each:

- **A. Layouts.** Add `DescriptorBindingDesc` / `DescriptorSetLayoutDesc` / `createDescriptorSetLayout`.
  Migrate the 20 sites in `rendering_resources.cpp` and the one in `resource_manager.cpp`; delete the dead RAII class and Builder.
- **B. Writer + sets.** Rework `DescriptorWriter` (copy semantics, types, arrays). Migrate the ~20 allocate-and-write
  blocks, the `w0..w7` blocks, the writes in `light_pass.cpp` / `shadow_pass.cpp`, then the 5 existing users.
  Remove the legacy raw-struct paths.
- **C. (optional) Derived pool sizes.**

Verification, in increasing strength:

1. `just check` (Debug `-Werror`, shaders, clang-format, clang-tidy init check).
2. **Validation baseline.** Run with validation forced on (temporary local edit of `enableValidationLayers`,
   reverted) and compare the `VUID-*` list to the baseline recorded before the phase: exactly the two
   `...-layout-07988` errors, nothing new.
3. **Structural call log (the important one).** Validation cannot catch "binding 3 is now a sampler instead of a
   storage image if the shader tolerates it". So: build a scratch binary with a force-included header that
   `#define`s `vkCreateDescriptorSetLayout` and `vkUpdateDescriptorSets` to wrappers that print, in call order, each
   layout's `(binding, type, count, stages)` and each write's `(dstBinding, type, descriptorCount, arrayElement)` plus
   which object kind the handle is. Run `main` and the branch, diff the logs. Handles differ between runs so only
   structure is compared; the diff must be empty. The header is a scratch tool, not committed.
4. Run the app for ~25 s and confirm the startup log is byte-identical to `main` (it was 247 lines).

Not available: screenshot / image diff. The environment can start the app and read its log but does not capture the
window. Item 3 is the substitute for "it still renders the same".

## 6. Risks

- **Layout order sensitivity.** The hand-built layouts list bindings in source order, which is not always ascending
  binding order. Vulkan does not care, but the call log (3) would show spurious diffs if `createDescriptorSetLayout`
  re-sorted. Plan: keep the declared order in `pBindings` and compare logs as-is.
- **Lifetime of infos** is the exact bug being removed; the writer must copy them, and that must be tested by the
  `w0..w7` migration (those sites keep infos in locals that die at the end of the loop body).
- **Behaviour must not change**, so some oddities are preserved on purpose: partial stage flags that cause the two
  validation errors, set counts, write order. They are listed for `hygiene` instead of fixed here.
- **Size of the diff.** Slice B touches ~1,000 lines of one file. Commit per pass (per `createDescriptorSets` block) so
  each step is reviewable and bisectable.

## 7. Questions for the reviewer

- **Q1.** Is rework-in-place of `DescriptorWriter` right, or is a new struct-only API
  (`WriteSet { std::vector<WriteEntry> }`) plus deleting the class cleaner?
- **Q2.** Raw handles + `createDescriptorSetLayout` (delete the unused RAII class), or give `RenderingResources` RAII
  owners now? (RAII would delete ~400 lines of `cleanup()` code but changes lifetimes and destruction order, which
  this phase promises not to touch.)
- **Q3.** Is derived pool sizing (slice C) worth doing in this phase, or a follow-up?
- **Q4.** Should the call-log harness (section 5.3) become a permanent `just` recipe, or stay a scratch tool?
  A permanent one would also protect the later slices (pipeline layouts, render passes).
- **Q5.** Anything in section 4 that conflicts with a later split of `rendering_resources.cpp` into per-pass
  resource owners (each pass owning its layout desc, sets and writer)?
