# Descriptors phase: implementation plan (for review)

Branch: `descriptors` (from `main` @ 455a61a). Status: plan only, no code changed yet.
Parent plan: `PLAN.md`, Phase 3 ("Vulkan struct descriptors"). The barrier slice is done (`src/Rendering/Core/barriers.*`);
this is the next slice. Read `AGENTS.md` for the repo rules and the gate (`just check`).

## 1. Context

C++17 Vulkan renderer (deferred PBR, cascaded shadows, Radiance Cascades GI, WBOIT, SMAA), MinGW GCC 15 on Windows.
The owner likes the Vulkan style of passing small structs around. Unlike the barrier slice, this phase does not add
parallel descriptor structs: Vulkan's descriptor structs already express the required data. It adds thin operations
around those structs to remove repeated creation, allocation and update ceremony.

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

Problems:

1. **Repeated 15-25 line blocks that differ only in bindings.** Each layout is: bindings array, create-info, create,
   error check, debug name, `std::cout`. A wrong stage flag or type is easy to introduce and hard to see in review.
2. **`w0..w7` style code** (RC build/resolve sets): one local `VkWriteDescriptorSet` per binding, each assigned field
   by field and pushed into a vector. Binding numbers are literals (`1 + b`).
3. **Existing `DescriptorWriter` is half-adopted and has a hazardous API.** `descriptors.hpp` already has a tutorial-style writer
   (`writeBuffer`, `writeImage`, `build`, `overwrite`), used by `material.cpp` and ~5 sites in `rendering_resources.cpp`.
   - It stores raw pointers to the caller's `VkDescriptorBufferInfo` / `VkDescriptorImageInfo`. Current temporary-writer
     call sites update immediately and appear safe, but the API permits dangling pointers if a writer outlives an info.
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
- One thin path over Vulkan's native structs to create a layout, allocate a set and write it.
- Migrate every site; delete the legacy path (no second way left in the tree).
- Make the writer own copied infos and support every descriptor type and array shape currently used.
- Behaviour identical: same layouts, same writes, same order.

Non-goals (separate branches)
- A universal descriptor struct or parallel `DescriptorBindingDesc` / `WriteSet` public data model.
- Fixing the two validation errors (that changes layouts; belongs in `hygiene`).
- Deriving descriptor-pool sizes from layouts.
- Changing raw descriptor-layout ownership to RAII.
- Images / views / samplers / pipeline layouts / render passes (other `structs` slices).
- Moving to descriptor indexing, push descriptors or `VK_KHR_dynamic_rendering`.
- Splitting `rendering_resources.cpp` into per-pass owners (later phase, but this phase makes it much easier).

## 4. Proposed design

Header: extend `src/Rendering/Core/descriptors.hpp` (and `.cpp`); no new module.

### 4.1 Layout description

No new binding struct. `VkDescriptorSetLayoutBinding` already carries everything a binding needs (`binding`,
`descriptorType`, `descriptorCount`, `stageFlags`, `pImmutableSamplers`), so a parallel `DescriptorBindingDesc` would be a
field-for-field copy. The binding fields are the real shader contract and remain visible. The boilerplate is the
create-info, create call and error check repeated 21 times. Add one core function plus an initializer-list convenience,
both taking Vulkan's own struct:

```cpp
VkDescriptorSetLayoutBinding layoutBinding(
    uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages, uint32_t count = 1);
VkDescriptorSetLayout createDescriptorSetLayout(
    Device&, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindingCount);
VkDescriptorSetLayout createDescriptorSetLayout(
    Device&, std::initializer_list<VkDescriptorSetLayoutBinding> bindings);
```

`layoutBinding` returns a native `VkDescriptorSetLayoutBinding`; it is not a parallel data model. The named function
prevents silent swaps between `descriptorCount` and `stageFlags`, which are both `uint32_t`-compatible in Vulkan.

The real G-buffer layout then reads:

```cpp
gbufferLayout = createDescriptorSetLayout(device, {
    layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
});
```

Existing short loops remain for layouts with consecutive bindings. Do not add `bindingRange` or per-descriptor-type
factories unless the migration demonstrates a repeated pattern that is clearer with one.

Declared order is passed through unchanged to `pBindings` (see section 6, layout order sensitivity). Any layout that
needs `pNext` binding flags, immutable samplers or non-zero create flags is found by the pre-slice grep (section 5) and
gets an overload or a field only if one actually exists.

Return type is a raw `VkDescriptorSetLayout`, because `RenderingResources` stores raw handles and destroys them in
`cleanup()` and every other file takes the raw handle (e.g. `material.cpp`, the pass `CreateInfo` structs). The unused
RAII `DescriptorSetLayout` class and its `Builder` are deleted because nothing calls them. Debug naming and exact
startup logging stay at the call sites; they are not responsibilities of this helper.

### 4.2 Writes

Rework `DescriptorWriter` rather than adding a second class:
- Own copies of the infos inside the writer, so no caller lifetime rules. Materialize `VkWriteDescriptorSet` pointers
  only at the terminal operation so internal container growth cannot invalidate them.
- `buffer(binding, type, VkDescriptorBufferInfo)`, `image(binding, type, VkDescriptorImageInfo)`,
  `images(binding, type, const VkDescriptorImageInfo*, count)` for array bindings. These take Vulkan structs directly;
  do not add public `WriteSet` or `WriteEntry` structs or per-type convenience vocabulary.
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

## 5. Migration order and verification

Slices, each its own commit; build and verification after each:

- **A. Layouts.** First grep the 21 layouts for `pNext` binding flags, immutable samplers, non-zero create flags and
  sampled-image/sampler types, and record the result here. Then add `layoutBinding` and
  `createDescriptorSetLayout`.
  Migrate the 20 sites in `rendering_resources.cpp` and the one in `resource_manager.cpp`; delete the dead RAII class and Builder.
  *Pre-slice grep result (2026-10-07):* none of the layout create-infos set `pNext`, `flags`, binding flags or
  `pImmutableSamplers`. Descriptor types used in layouts: `COMBINED_IMAGE_SAMPLER`, `STORAGE_BUFFER`, `STORAGE_IMAGE`,
  `UNIFORM_BUFFER` only. So `layoutBinding` + `createDescriptorSetLayout` need no extra overload.
  *Baseline call log (unmigrated code):* 341 descriptor calls = 22 layouts, 91 set allocations, 228 writes. The 22nd
  layout is ImGui's own (the harness wraps every translation unit, including the vendored ImGui backend). Two captures
  are byte-identical. An earlier 334-call baseline was truncated by stderr buffering; the harness now unbuffers stderr
  and rejects a truncated last line.
- **B. Writer + sets.** Rework `DescriptorWriter` (copy semantics, types, arrays). Migrate the ~20 allocate-and-write
  blocks, the `w0..w7` blocks, the writes in `light_pass.cpp` / `shadow_pass.cpp`, then the 5 existing users.
  Remove the legacy raw-struct paths.
Verification, in increasing strength:

1. `just check` (Debug `-Werror`, shaders, clang-format, clang-tidy init check).
2. **Validation baseline.** Run with validation forced on (temporary local edit of `enableValidationLayers`,
   reverted) and compare the `VUID-*` list to the baseline recorded before the phase: exactly the two
   `...-layout-07988` errors, nothing new.
3. **Structural call log (the important one).** Validation cannot catch "binding 3 is now a sampler instead of a
   storage image if the shader tolerates it". Build a binary with a force-included header that
   `#define`s `vkCreateDescriptorSetLayout` and `vkUpdateDescriptorSets` to wrappers that print, in call order, each
   layout's `(binding, type, count, stages)` and each write's `(dstBinding, type, descriptorCount, arrayElement)` plus
   which object kind the handle is. Run `main` and the branch, diff the logs. Handles differ between runs so only
   structure is compared; the diff must be empty. Commit the reusable harness under `scripts/` so later
   pipeline-layout and render-pass slices can use it, but do not add it to the default `just check` gate.
4. Run the app for ~25 s and confirm the startup log is byte-identical to `main` (it was 247 lines). Because this is a
   rendering-path change, also capture the same camera on `main` and the branch for visual comparison.

If the agent environment cannot capture the window, the owner must provide the two screenshots before the slice is
called verified. The structural log supplements but does not replace visual evidence.

## 6. Risks

- **Layout order sensitivity.** The hand-built layouts list bindings in source order, which is not always ascending
  binding order. Vulkan does not care, but the call log (3) would show spurious diffs if `createDescriptorSetLayout`
  re-sorted. Plan: keep the declared order in `pBindings` and compare logs as-is.
- **Lifetime of infos.** The current call sites appear safe because updates happen before local infos die, but the
  pointer-based API is easy to misuse. The writer must copy infos, including descriptor arrays, and only construct
  pointer-bearing Vulkan writes after its owned storage is complete.
- **Behaviour must not change**, so some oddities are preserved on purpose: partial stage flags that cause the two
  validation errors, set counts, write order. They are listed for `hygiene` instead of fixed here.
- **Size of the diff.** Slice B touches ~1,000 lines of one file. Commit per pass (per `createDescriptorSets` block) so
  each step is reviewable and bisectable.

## 7. Decisions and boundaries

- Use Vulkan's native descriptor structs at call sites; do not create a universal descriptor struct.
- Rework the existing `DescriptorWriter`; do not add a second descriptor-writing API.
- Keep raw layout handles and current ownership in this phase.
- Keep debug naming, logging and `vkCmdBindDescriptorSets` at their current owners.
- Keep the structural call-log harness as a reusable manual tool under `scripts/`, not a permanent `just` recipe.
- Defer derived pool sizing. If that follow-up needs persistent binding data, an owning layout description may then
  be justified because it adds lifetime and aggregation semantics rather than copying Vulkan fields for style.
- This phase centralizes how layouts and sets are created and written. It does not centralize which descriptors a
  render pass owns or receives, prevent wrong descriptor types/stage flags, or fix shader/layout mismatches.
