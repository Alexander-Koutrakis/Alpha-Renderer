# Alpha Renderer: engineering quality plan

Goal: treat this as a professional, maintainable codebase: anyone can clone it, build it, read it and trust the checks.
Baseline (2026-10-06, `main` @ a5baa24): builds with GCC 15 + Ninja, 0 warnings, 21/21 shaders pass
`glslc` + `spirv-val` (Vulkan 1.3). ~20k lines C++/GLSL. No `.clang-format`, `.clang-tidy`, CI, tests, `justfile`.

## Ground rules

- One private local branch per phase, named after the phase (`tooling`, `format`, `init`, ...). Nothing is pushed.
- Each phase ends with: build clean, 0 new warnings, shaders validate, then **you** approve the merge to `main`
  (fast-forward or `--no-ff`, your pick). No merge happens without that approval.
- Every phase is verified with real output (build log, `glslc`/`spirv-val`, and for anything touching rendering a
  before/after screenshot of the same camera). "It compiles" is not evidence.
- Phases are ordered so the mechanical, diff-noisy work lands first and later diffs stay readable.

## Review findings (condensed)

Spot-checked by me against the source: validation off (`device.hpp:37`), NVIDIA-only GPU pick (`device.cpp:131`),
`std::cin.get()` in `main.cpp:23`, squared specular (`transparency.frag:501`), "LittleVulkanEngine App"
(`device.cpp:83`), `using namespace` in 11 headers, `swapchainImageViews` sized by frames-in-flight
(`renderer.hpp:109`). Not verified: line numbers elsewhere in the subagent reports.
Correction: the claim that `direct_light.vert` and `fullscreen.vert` are byte-identical is false (they differ at line 5);
check before merging them.

### Uninitialized values (src/)
Mostly disciplined: ~300 Vulkan locals use `{}`, no missing `sType` found. Real risks:
- `frame_context.hpp`: `FrameContext` ~84 members without initializers, built via default-init
  `std::array<FrameContext,N> contexts;` (`rendering_resources.cpp:2747`). Also `CameraData`, `PrevCameraData`,
  `MaterialBatch`, `MeshMaterialSubmeshKey` (used as a hash key), dead member `frameSceneBounds`.
- `pipeline.hpp`: `PipelineConfigInfo` 8 Vk state structs with no initializers/`sType`; hand-written copy
  constructor copies internal pointers (dangling after copy).
- `device.cpp:97,245`: `VkDebugUtilsMessengerCreateInfoEXT` locals without `{}`; `device.hpp` handles and
  `QueueFamilyIndices` uninitialized.
- `mesh.hpp`: `indexCount` unset for non-indexed meshes. `components.hpp`: light matrix/cascade arrays.
  `swapchain.hpp`, `buffer.hpp`, `window.hpp`, `AABB`, octree, `deserialized_scene.hpp` (~30 members), RC push-constant structs.

### Code and architecture
- Portability: hardcoded `C:/msys64`, `C:/Program Files/KTX-Software`, `$ENV{VULKAN_SDK}/Bin/glslc.exe`; GLM not managed;
  CWD-relative asset/shader paths; target named `main`; no presets/CI.
- Correctness: validation hardcoded off; NVIDIA-only GPU selection; `renderFinished` semaphores per frame-in-flight
  not per swapchain image; two frame counters; unchecked `vkQueueSubmit`/fence/begin/end calls; no `VK_CHECK`;
  `loadSceneAsync` shares one command pool across threads.
- Hygiene: 189 debug prints, `std::cin.get()`, dead stutter-counting code, tutorial leftovers, typos
  (`enviroment`, `KeyboardMovemen`, `Gflw`), folders with spaces ("Direct Lighting"), mixed tabs/spaces and member naming.
- Size: `rendering_resources.cpp` 2904 lines; `light_system.cpp` 825; `scene_loader.cpp` 712; `renderer.cpp` frame body ~300 lines.
- Vulkan struct hotspots: 238 hand-filled `sType` sites (WRITE_DESCRIPTOR_SET ~55, barriers 31, image/view 30,
  sampler 10, render pass/framebuffer 33, pipeline layout 15).

### Shaders
Compile and validate clean. Visual/logic issues, highest first:
1. `transparency.frag` `SceneLightingUbo` does not match the C++ struct (`render_passes_buffers.hpp:34-40`).
2. `transparency.frag:501` specular squared; WBOIT weight applies alpha twice and fakes depth; spot shadow compares the wrong space.
3. `direct_light.frag` double Fresnel on IBL specular; sky test uses `length(worldPos)` instead of `.w`; cascade index bound mismatch (64 vs 4, C++ constant says 128).
4. NaN hazards: `pow(1-x,5)` with x>1, `sqrt(1-NdotL²)`, unguarded point-light range.
5. `rc_depth_downsample.comp` out-of-bounds fetch on odd sizes; `rc_build_cascade.comp:332` normal check rejects axis-aligned normals;
   `rc_resolve_indirect.comp` temporal path reads current-frame normal; `rc_merge.comp` push-constant block does not match C++.
6. Duplication: PBR/shadow/light code copied between `direct_light.frag` and `transparency.frag`; Camera/Material/Light UBOs repeated; RC helpers repeated 3x.

## Phases

### Phase 0: Skills and tooling (your step 3)
Branch `tooling`. Install/enable Vulkan, C++ and GLSL skills (list candidates, you choose; installs are your call).
Add `justfile` with `check` (configure, build, shader gate, format check), `AGENTS.md`/`CLAUDE.md` with the project rules.
Exit: `just check` runs on the untouched code and passes.

### Phase 1: clang-format (your step: format the codebase)
Branch `format`.
- Add `.clang-format`. Proposed: `BasedOnStyle: LLVM`, 4-space indent, 120 columns, `PointerAlignment: Left`,
  `IndentCaseLabels`, `NamespaceIndentation: None`, `SortIncludes: CaseInsensitive` with explicit include blocks,
  `LineEnding: DeriveLF`. (The tree is `* text=auto`, so the working copy is CRLF and the index is LF.)
- Exclude `external/` via `external/.clang-format` with `DisableFormat: true`.
- Reformat `src/` only, in one commit; record it in `.git-blame-ignore-revs`.
- Include order is the risky part: `SortIncludes` can reorder headers that depend on order (`core.hpp` GLFW/GLM, `renderer.cpp` forward declaration).
  Start with `SortIncludes: Never`, enable sorting as a separate commit only if the build stays identical.
- Exit: `clang-format --dry-run -Werror` over `src/` is clean; build output unchanged (same 0 warnings); disassembly-level change is nil because the commit is whitespace-only.

### Phase 2: Initialization hardening (your step: uninitialized values)
Branch `init`.
- Default member initializers everywhere flagged (handles `= VK_NULL_HANDLE`, scalars `{}`, matrices `{1.0f}`, arrays `{}`).
- `PipelineConfigInfo`: give each state struct its `sType`; replace the hand-written copy with value-safe wiring at `build()` time.
- `FrameContext` and the arrays that hold it: value-initialize; delete dead `frameSceneBounds`.
- `VkDebugUtilsMessengerCreateInfoEXT x{VK_STRUCTURE_TYPE_...}` style for the two stray locals.
- Gate (done): `-Werror` through `ALPHA_WARNINGS_AS_ERRORS`, and `.clang-tidy` with `cppcoreguidelines-pro-type-member-init` run by `just tidy`.
  Changed from the original plan: `-Wmissing-field-initializers` is off (it flags `VkFoo{sType}`, which zero-fills on purpose) and
  `cppcoreguidelines-init-variables` is off (it flags locals assigned on every path and API out-parameters; zeroing them would hide real misses).
- Exit: build clean, tidy clean on touched files, scene still renders identically (screenshot diff).

### Phase 3: Vulkan struct descriptors (your step: "structs everywhere") (done)
Branch `structs`, merged into `main` as slice branches (`barriers`, `descriptors`, `descriptor-writer`, `validation-baseline`, `images`,
`samplers`, `pipeline-layouts`, `render-passes`), each migrating every site and deleting the old path.
Changed from the original plan: **native Vulkan structs plus thin helpers, not parallel `ImageDesc`/`SamplerDesc`/`PipelineLayoutDesc`/`RenderPassDesc`
data models** (decided in `docs/descriptors-plan.md` section 7 and confirmed by the owner). Barriers are the one place that got value structs, because the raw
struct was 14 fields of ceremony. Render passes were kept; dynamic rendering is parked for Phase 7. Helpers throw `std::runtime_error` with one generic message.
- Barriers: `ImageBarrierDesc`, `BufferBarrierDesc`, `MemoryBarrierDesc`, `cmdPipelineBarrier` (`Core/barriers.*`), 35 sites.
- Descriptors: `createDescriptorSetLayout`, `allocateDescriptorSet`, `DescriptorWriter`: 21 layouts, 91 allocations, 228 writes.
- Images: `imageCreateInfo2D`, `createImageView` (`Core/images.*`): 15 images, 23 views.
- Samplers: `createSampler` (`Core/samplers.*`): 12 sites.
- Pipeline layouts: `createPipelineLayout`, `pushConstantRange` (`Core/pipeline_layouts.*`), with `ArrayView` in `Core/array_view.hpp`: 16 sites.
- Framebuffers and render passes: `createFramebuffer`, `attachmentDescription`, `createRenderPass` (`Core/render_passes.*`): 11 framebuffer sites, 11 passes.
  Subpass and dependency structs stay native on purpose.
- Not done from the original bullets: the `XPass::CreateInfo` convention was not touched (the passes keep their own).
- Gate and proof (all in `docs/handoff.md` section 5): `just check`; `just validation` (needs a GPU; baseline is the two pre-existing
  `-07988` VUIDs in `scripts/validation_baseline.txt`, so "zero errors" is not yet true and is a Phase 4 item); and five call-log harnesses
  (`scripts/descriptor_call_log.ps1` with a header per object type) whose before/after diff was empty for every slice.
- Exit status: build clean, tidy clean, call-log diffs empty, validation at baseline. **Not met: the screenshot diff vs baseline**, because no
  same-camera screenshots were taken (agents cannot capture the window). Until the owner supplies them, Phase 3 is verified at the
  Vulkan-object level, not at the pixel level.

### Phase 4: Correctness and hygiene
Branch `hygiene`.
1. Validation: CMake option / `NDEBUG`-driven, severity-filtered callback. Run once and report the validation-layer output.
2. GPU selection by score (discrete > integrated, must be suitable), remove NVIDIA gate.
3. `VK_CHECK` with `VkResult` name + file/line; apply to the unchecked submit/fence/begin/end calls.
4. Per-swapchain-image `renderFinished` semaphores, one frame-index owner, `swapchainImageViews` sized by image count.
5. Remove `cin.get()`, debug prints behind a tiny logger, dead stutter code, `loadSceneAsync` (or document), tutorial strings, typos.
6. Remove `using namespace` from headers; trim `core.hpp`.
7. Rename folders with spaces to snake_case (separate commit, `git mv` only, updates `CMakeLists.txt`).
- Exit: build clean, validation clean, run from a different working directory works (after asset path fix in Phase 6, or noted).

### Phase 5: Shaders
Branch `shaders`.
- Behavior-neutral first: shared `common/*.glsl` via `GL_GOOGLE_include_directive` (needs `-I` in CMake and the shader gate), delete confirmed duplicates, naming/typos, `max()` guards for NaN hazards.
- Behavior-changing fixes as separate commits, each with before/after screenshots: transparency UBO mismatch, squared specular,
  WBOIT weight, spot shadow space, IBL Fresnel, sky test, downsample bounds, RC normal check, RC temporal normal, RC push-constant block.
- Exit: 21/21 shaders `glslc` + `spirv-val` clean, no new warnings, screenshot comparisons attached.

### Phase 6: Build portability, CI, docs, tests
Branch `portable`.
- CMake: `find_package` for glfw3/glm/KTX (or FetchContent), `find_program`/`Vulkan::glslc`, `CMakePresets.json`, target renamed, asset/shader paths relative to the executable, `CONFIGURE_DEPENDS` glob.
- GitHub Actions (Windows + Linux) that configure, build and compile shaders. Needs your OK to add (it is outward-facing once pushed).
- README: real prerequisites, build/run commands, controls, GPU requirements, fix broken `src/` doc links and `assets` vs `Assets`, drop the stb_image claim, add a timings table if we measure.
- Tests: doctest/Catch2 via CTest for ECS, octree, view frustum, AABB.

### Phase 7 (optional, optional, larger refactors)
Split `rendering_resources.cpp` into per-pass resource owners; data-driven pass order in `renderer.cpp`; per-pass GPU timestamps.

## Open decisions for you
1. Dynamic rendering and `vkCmdPipelineBarrier2` (Vulkan 1.3) vs keeping render passes: decided for Phase 3, render passes kept. Revisit in Phase 7.
2. Phase 6 dependency strategy: system packages vs FetchContent/vcpkg. Default: FetchContent for GLM/GLFW, system KTX.
3. Merge style per phase: decided, `--no-ff`.
4. Whether to ship behavior-changing shader fixes in this pass or list them as known issues.
