#!/usr/bin/env bash
# Build, test, and install the checked-out source. Never installs OS packages.
set -euo pipefail

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
prefix=${HOME}/.local
build_dir=${source_dir}/build-fedora
stage_dir=
jobs=2
kde=ON
check_only=false

usage() {
    cat <<'EOF'
Usage: packaging/fedora/install.sh [options]
  --check             Check required tools/packages; do not build or install
  --prefix PATH       Absolute installation prefix (default: $HOME/.local)
  --build-dir PATH    Build directory (default: checkout/build-fedora)
  --destdir PATH      Absolute staging root; does not alter a live installation
  --jobs NUMBER       Parallel build jobs, 1-64 (default: 2)
  --qt-only           Build without KDE activation-token integration
  --help              Show this help

Run as your regular user. OS dependencies must be installed separately.
EOF
}
die() { printf 'Dzurkel: %s\n' "$*" >&2; exit 1; }
value() { [[ $# -ge 2 && -n $2 ]] || die "$1 needs a value"; }
while [[ $# -gt 0 ]]; do
    case $1 in
        --check) check_only=true; shift ;;
        --prefix) value "$@"; prefix=$2; shift 2 ;;
        --build-dir) value "$@"; build_dir=$2; shift 2 ;;
        --destdir) value "$@"; stage_dir=$2; shift 2 ;;
        --jobs) value "$@"; jobs=$2; shift 2 ;;
        --qt-only) kde=OFF; shift ;;
        --help|-h) usage; exit 0 ;;
        *) die "unknown option: $1 (use --help)" ;;
    esac
done
[[ $prefix = /* && $prefix != / ]] || die '--prefix must be an absolute directory other than /'
[[ -z $stage_dir || $stage_dir = /* && $stage_dir != / ]] || die '--destdir must be an absolute directory other than /'
[[ $jobs =~ ^[1-9][0-9]?$ && $jobs -le 64 ]] || die '--jobs must be an integer from 1 to 64'
[[ -r /etc/fedora-release ]] || die 'this installer targets Fedora; use the documented CMake build elsewhere'

packages=(gcc-c++ cmake make pkgconf-pkg-config libzip-devel qt6-qtbase-devel qt6-qtpdf-devel qt6-qtmultimedia-devel dbus-daemon python3 desktop-file-utils)
[[ $kde = OFF ]] || packages+=(kf6-kwindowsystem-devel)
missing=()
for package in "${packages[@]}"; do
    rpm -q --quiet "$package" || missing+=("$package")
done
if [[ ${#missing[@]} -gt 0 ]]; then
    printf 'Missing build/test packages. Run this command yourself:\n  sudo dnf install' >&2
    printf ' %s' "${missing[@]}" >&2
    printf '\n' >&2
    exit 1
fi
for tool in cmake c++ ctest dbus-run-session python3 desktop-file-validate; do
    command -v "$tool" >/dev/null || die "required tool is not on PATH: $tool"
done
printf 'Required Fedora build/test dependencies are available.\n'
$check_only && exit 0
[[ $EUID -ne 0 ]] || die 'run as your regular user; choose a writable prefix or --destdir'

# A dedicated directory avoids changing the developer's active build settings.
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_BINDIR=bin \
    -DCMAKE_INSTALL_DATADIR=share -DBUILD_TESTING=ON \
    -DDZURKEL_WITH_KDE_ACTIVATION="$kde" \
    -DDZURKEL_ENABLE_IPO=ON -DDZURKEL_GC_SECTIONS=ON
cmake --build "$build_dir" --parallel "$jobs"
ctest --test-dir "$build_dir" --output-on-failure
desktop-file-validate "$build_dir/dzurkel-application.desktop"
desktop-file-validate "$build_dir/dzurkel-settings.desktop"

if [[ -z $stage_dir && -x $prefix/bin/dzurkel ]]; then
    # Contact the current user bus before replacing an installed resident binary.
    # Refuse to install if the old process cannot be shut down safely.
    "$build_dir/dzurkel" --quit || die 'could not stop the resident service; quit it in your desktop session and retry'
fi
if [[ -n $stage_dir ]]; then
    DESTDIR="$stage_dir" cmake --install "$build_dir" --strip
    printf 'Staged installation under %s (configured prefix %s).\n' "$stage_dir" "$prefix"
else
    # Discard inherited DESTDIR so the selected mode matches the printed result.
    env -u DESTDIR cmake --install "$build_dir" --strip
    printf 'Installed Dzurkel into %s. Open Dzurkel Settings from the application menu to enable Space previews.\n' "$prefix"
fi
