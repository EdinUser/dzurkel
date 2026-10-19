#!/usr/bin/env bash
# Run interactively in the Arch runtime VM; optional backends are never requested.
set -euo pipefail
cd "$(dirname "$0")"
source /etc/os-release
[[ "$ID" == arch ]]
[[ "$EUID" != 0 ]] || { printf 'Run as the desktop user, not root.\n' >&2; exit 1; }
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
test -S "$XDG_RUNTIME_DIR/bus" || { printf 'Log into the VM desktop first.\n' >&2; exit 1; }
shopt -s nullglob
packages=(dzurkel-[0-9]*-x86_64.pkg.tar.zst)
[[ ${#packages[@]} == 1 ]] || { printf 'Expected exactly one Dzurkel package.\n' >&2; exit 1; }
printf 'Installing the package and its required runtime dependencies only.\n'
sudo pacman -U -- "${packages[0]}"
# Stop the previous app before switching launchers/binaries.
if [[ -x "$HOME/.local/bin/dzurkel" ]]; then
    "$HOME/.local/bin/dzurkel" --quit
else
    /usr/bin/dzurkel --quit
fi
backup="$HOME/.local/share/dzurkel-package-backups/$(date -u +%Y%m%dT%H%M%S.%NZ)"
for relative in bin/dzurkel share/applications/dzurkel.desktop share/applications/dzurkel-settings.desktop; do
    path="$HOME/.local/$relative"
    if [[ -e "$path" || -L "$path" ]]; then
        mkdir -p "$backup/$(dirname "$relative")"
        mv "$path" "$backup/$relative"
    fi
done
# Existing file-manager shortcuts may reference this exact user-local path.
mkdir -p "$HOME/.local/bin"
printf '#!/bin/sh\nexec /usr/bin/dzurkel "$@"\n' > "$HOME/.local/bin/dzurkel"
chmod +x "$HOME/.local/bin/dzurkel"
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
/usr/bin/dzurkel --version
printf '\nPrevious user-local launchers/binary backed up under %s\n' "$backup"
printf 'Open /usr/bin/dzurkel --settings from the VM desktop.\n'
printf 'Optional backends currently installed (existing desktop dependencies are preserved):\n'
pacman -Q qt6-multimedia syntax-highlighting kwindowsystem antiword 2>/dev/null || true
