# The single verification gate. Run `just check` before claiming anything is done.
# Placeholders below fail loudly on purpose: an unconfigured gate must never look green.
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

default:
    @just --list

# Everything that must be green.
check: build shaders format-check tidy test

# Configure and build with warnings visible (Ninja + MinGW, Debug).
build:
    cmake --preset debug
    cmake --build --preset debug

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
