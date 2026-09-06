#!/usr/bin/env bash
set -euo pipefail

install_deps=0
clean=0
run_tests=1
build_type=Release
release_gui=0

for option in "$@"; do
    case "$option" in
        --install-deps) install_deps=1 ;;
        --clean) clean=1 ;;
        --no-tests) run_tests=0 ;;
        --debug) build_type=Debug ;;
        --release-gui) release_gui=1 ;;
        *) echo "Unknown option: $option" >&2; exit 2 ;;
    esac
done

if (( release_gui )) && [[ "$build_type" == "Debug" ]]; then
    echo "--release-gui cannot be combined with --debug." >&2
    exit 2
fi

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    echo "This script must run in the MSYS2 UCRT64 environment." >&2
    exit 2
fi

packages=(
    git
    gettext
    mingw-w64-ucrt-x86_64-toolchain
    mingw-w64-ucrt-x86_64-cmake
    mingw-w64-ucrt-x86_64-ninja
    mingw-w64-ucrt-x86_64-pkgconf
    mingw-w64-ucrt-x86_64-SDL2
    mingw-w64-ucrt-x86_64-SDL2_image
    mingw-w64-ucrt-x86_64-SDL2_mixer
    mingw-w64-ucrt-x86_64-SDL2_ttf
    mingw-w64-ucrt-x86_64-zlib
)

if (( install_deps )); then
    pacman -S --needed --noconfirm "${packages[@]}"
fi

for command_name in git cmake ninja pkg-config c++ msgfmt; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing command: $command_name (rerun PowerShell with -InstallDeps)." >&2
        exit 2
    }
done

for module in sdl2 SDL2_image SDL2_mixer SDL2_ttf zlib; do
    pkg-config --exists "$module" || {
        echo "Missing pkg-config module: $module (rerun PowerShell with -InstallDeps)." >&2
        exit 2
    }
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-windows"

cd "$repo_root"
git submodule update --init --recursive
if (( clean )); then rm -rf "$build_dir"; fi
testing=OFF
if (( run_tests )); then testing=ON; fi

cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DBUILD_TESTING="$testing" \
    -DFOUR_WINDS_WINDOWS_GUI="$release_gui"
cmake --build "$build_dir" --parallel "${NUMBER_OF_PROCESSORS:-4}"

if (( run_tests )); then
    ctest --test-dir "$build_dir" --output-on-failure
fi

dist_dir="$repo_root/dist/windows"
staging_dir="$repo_root/dist/windows-staging"
rm -rf "$staging_dir"
trap 'rm -rf "$staging_dir"' EXIT
mkdir -p "$staging_dir"
cp "$build_dir/four-winds-reborn.exe" "$staging_dir/"
cp -R "$repo_root/themes" "$staging_dir/"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging_dir/"
cp -R "$repo_root/licenses" "$staging_dir/"

# Compile translations from their editable source on every package build.
# This prevents an updated .po file from silently shipping with a stale .mo.
while IFS= read -r -d '' po_file; do
    msgfmt "$po_file" -o "${po_file%.po}.mo"
done < <(find "$staging_dir/themes" -type f -name '*.po' -print0)

while IFS= read -r dependency; do
    [[ -n "$dependency" ]] && cp -f "$dependency" "$staging_dir/"
done < <(ldd "$build_dir/four-winds-reborn.exe" | awk '/=> \/ucrt64\/bin\// { print $3 }')

rm -rf "$dist_dir"
mv "$staging_dir" "$dist_dir"
trap - EXIT

echo "Built: $dist_dir/four-winds-reborn.exe"
