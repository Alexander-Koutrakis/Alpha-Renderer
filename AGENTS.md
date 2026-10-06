# Alpha Renderer: working rules

Rules for any agent (Claude Code or Codex) changing this repo.

## Principles

- **Verify before claiming done.** Run `just check` and report its output. Never claim success from "it compiles" or "it looks right". For rendering, show a screenshot, image diff or validation-layer output.
- **Encode lessons in structure.** A mistake that repeats becomes a script, lint rule or test, not a longer prompt. Log it in `docs/friction-log.md`.
- **Fix root causes.** No silent fallbacks, no "just in case" guards, no loosened tolerances, no stubbed tests to get green.
- **Prefer deleting to adding layers.** Migrate callers, then remove the legacy path.
- **Memory is not authoritative.** Before a consequential decision, cite the current file or command output.
- **Narrow permissions.** No `Bash(*)`. Prefer ask over allow for anything one-way.

## Commands

- `just check`: the single verification gate: `build` (CMake/Ninja, Debug, must stay at 0 warnings), `shaders` (glslc + spirv-val on all 21 shaders), `format-check` (clang-format, fails until Phase 1 adds `.clang-format`).
- `just build`, `just shaders`, `just format-check`: the pieces on their own.
- No unit tests yet; they arrive in Phase 6 of `PLAN.md`.
- `just doctor`: toolchain check.

## One-way doors (always ask)

`git push`, force-push, merge, tag or release, deleting files outside a worktree, editing `.claude/settings*` or hooks, publishing anywhere, sending messages, purchases, accepting licences, anything touching company systems or credentials.

## Work style

- One task card per job (`docs/task-card-template.md`). Work in a worktree under `.worktrees/` (see the `worktree-flow` skill).
- Unattended findings go to `findings/`, not straight into fixes.

## Layout

- `src/`: engine code. `Rendering/Core` (Vulkan device, swapchain, pipelines, descriptors), `Rendering/Resources`, `Rendering/RenderPasses/*`, `ECS`, `Systems`, `Scene`, `Resources` (scene loading), `Math`, `Engine`.
- `shaders/`: GLSL 450 (vert/frag/comp), compiled with glslc to Vulkan 1.3 SPIR-V.
- `Assets/`: Unity-exported sample scene (JSON + HDR cubemap faces).
- `external/`: vendored third-party code (imgui, json, tinygltf, stb, SMAA tables). Never reformat or edit.
- `PLAN.md`: the phased CV-polish plan. One `polish/<phase>` branch per phase, merged after approval.
- `scripts/`: helper scripts used by `just`.

## Environment

- Windows 11, PowerShell (just recipes) and Git Bash. Scripts must handle Windows paths and CRLF (`.gitattributes` is `* text=auto`).
- Toolchain: MinGW GCC (msys64), CMake + Ninja, Vulkan SDK 1.3.290 (`VULKAN_SDK` must be set), LLVM clang-format/clang-tidy, KTX-Software, GLFW and GLM from msys64.
- C++17. No validation-layer or GPU-run check exists in `just check`; rendering changes need a screenshot against the same camera.
