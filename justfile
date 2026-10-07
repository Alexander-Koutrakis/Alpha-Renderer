# The single verification gate. Run `just check` before claiming anything is done.
# Placeholders below fail loudly on purpose: an unconfigured gate must never look green.
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

default:
    @just --list

# Everything that must be green. Unit tests are added in Phase 6 (see PLAN.md);
# until then there is no `test` recipe rather than a fake green one.
check: build shaders format-check tidy

# Configure and build with warnings visible (Ninja + MinGW, Debug).
build:
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DALPHA_WARNINGS_AS_ERRORS=ON
    cmake --build build

# Compile every shader with glslc and validate with spirv-val.
shaders:
    ./scripts/check_shaders.ps1

# clang-format conformance.
format-check:
    if (-not (Test-Path .clang-format)) { Write-Error "format-check: .clang-format missing"; exit 1 }
    $files = Get-ChildItem src -Recurse -Include *.cpp,*.hpp,*.inl | ForEach-Object FullName; clang-format --dry-run -Werror $files

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
