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

# Unit tests (doctest via CTest): math, octree, ECS. No GPU needed.
test: build
    ctest --test-dir build --output-on-failure

# Compile every shader with glslc and validate with spirv-val.
shaders:
    ./scripts/check_shaders.ps1

# Behavior-neutral check: compare every shader against a base ref (default main). Use scripts/shader_equiv.ps1 -Expect for intended changes.
shader-equiv base="main":
    ./scripts/shader_equiv.ps1 -BaseRef {{base}}

# Run the app, capture its window and diff it against tests/golden/scene.png (needs a GPU and a display; not in `check`).
screenshot-diff:
    ./scripts/screenshot_diff.ps1

# Rewrite the golden image from the current build. Only do this on a build you have looked at and trust.
screenshot-baseline:
    ./scripts/screenshot_diff.ps1 -Update

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
