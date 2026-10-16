# The single verification gate. Run `just check` before claiming anything is done.
# Placeholders below fail loudly on purpose: an unconfigured gate must never look green.
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

default:
    @just --list

# Everything that must be green.
check: build shaders format-check tidy test release

# Configure and build with warnings visible (Ninja + MinGW, Debug).
build:
    cmake --preset debug
    cmake --build --preset debug

# Release build (-O3, -Werror, no validation layers) and its unit tests, so the optimised configuration cannot rot.
release: shaders
    cmake --preset release
    cmake --build --preset release
    if (-not (Select-String -Path build/release/CMakeCache.txt -Pattern 'ALPHA_ENABLE_VALIDATION:BOOL=OFF' -Quiet)) { Write-Error "release: validation layers are on in the release build"; exit 1 }
    ctest --test-dir build/release --output-on-failure

# Unit tests (doctest via CTest): math, octree, ECS, shader block layouts. No GPU needed.
test: build shaders
    ctest --test-dir build --output-on-failure

# Compile every shader with glslc, validate with spirv-val, and dump the block layouts the layout test reads.
shaders:
    ./scripts/check_shaders.ps1
    ./scripts/dump_block_layouts.ps1

# Behavior-neutral check: compare every shader against a base ref (default main). Use scripts/shader_equiv.ps1 -Expect for intended changes.
shader-equiv base="main":
    ./scripts/shader_equiv.ps1 -BaseRef {{base}}

# Run the app on each test scene (the sample scene, tests/scenes/glass and the two lights scenes), capture its window and
# diff it against tests/golden/ (needs a GPU and a display; not in `check`).
# lights_b has the same lights as lights_a in the reverse order, so it must render like lights_a. The GI speckle noise in
# these scenes is about 1.2% of pixels (mean 0.6); a shadow slot mix-up gave 48% (mean 15).
screenshot-diff:
    ./scripts/screenshot_diff.ps1
    ./scripts/screenshot_diff.ps1 -Scene glass
    ./scripts/screenshot_diff.ps1 -Scene lights_a -MaxBadFraction 0.03 -MaxMeanDiff 2.0
    ./scripts/screenshot_diff.ps1 -Scene lights_b -Baseline tests/golden/lights_a.png -MaxBadFraction 0.03 -MaxMeanDiff 2.0

# Rewrite a golden image from the current build (scene = "" for the sample scene, glass or lights_a). Only do this on a build
# you have looked at and trust.
screenshot-baseline scene="":
    ./scripts/screenshot_diff.ps1 -Update {{ if scene == "" { "" } else { "-Scene " + scene } }}

# clang-format conformance.
format-check:
    if (-not (Test-Path .clang-format)) { Write-Error "format-check: .clang-format missing"; exit 1 }
    $files = Get-ChildItem src,tests -Recurse -Include *.cpp,*.hpp,*.inl | ForEach-Object FullName; clang-format --dry-run -Werror $files

# clang-tidy initialization checks over src/ (needs build/compile_commands.json from `just build`).
tidy: build
    ./scripts/run_tidy.ps1

# Run the app with validation layers on and compare VUIDs with scripts/validation_baseline.txt (needs a GPU; not in `check`).
validation:
    ./scripts/validation_check.ps1
    ./scripts/validation_check.ps1 -Scene many_lights

# `validation` plus the layer's synchronization validation (opt-in: slower; a hazard is reported as SYNC-HAZARD-*; needs a GPU; not in `check`).
validation-sync:
    ./scripts/validation_check.ps1 -Sync
    ./scripts/validation_check.ps1 -Sync -Scene many_lights

# Resize the window under validation layers: RenderingResources must rebuild cleanly (needs a GPU; not in `check`).
resize-check:
    ./scripts/resize_check.ps1

# Clone origin's main (or `-Ref x`) into build/cold-clone and build and test it there: proves the repo builds from what git tracks.
# Needs network; not in `check`. Usage: just cold-clone, or ./scripts/cold_clone_check.ps1 -Ref some_branch
cold-clone:
    ./scripts/cold_clone_check.ps1

# Toolchain sanity check.
doctor:
    just --version
    uv --version
    git --version
    cmake --version
    clang-format --version
    clang-tidy --version
    glslc --version
    spirv-val --version

# Compare the Radiance Cascades GI with the brute-force reference (ALPHA_RC_MODE=reference); prints bias, error and correlation
# and writes build/rc-compare/<scene>/ (needs a GPU and an untouched screen; not in `check`). Usage: just rc-compare, just rc-compare glass
rc-compare scene="":
    ./scripts/rc_compare.ps1 {{ if scene == "" { "" } else { "-Scene " + scene } }}
