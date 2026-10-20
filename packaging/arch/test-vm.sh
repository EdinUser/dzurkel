#!/usr/bin/env bash
# Build packages on the Fedora host; keep SDKs off the Arch runtime-test VM.
set -euo pipefail
usage() {
    cat <<'HELP'
Usage: bash packaging/arch/test-vm.sh [--build-only | --stage-only | --install] [USER@HOST]

Build all adapters in a disposable Arch container, test without optional runtime
libraries, then install through pacman in the Arch VM. No guest build tools needed.
USER@HOST is required unless --build-only is selected. SSH config aliases work.
--build-only  Build/test locally; do not contact the VM.
--stage-only  Copy the tested package and print the guest installation command.
--install     Install over interactive SSH (the default; sudo prompts normally).
Needs host Podman, Python 3, Git and SSH. Uses the current uncommitted public source.
HELP
}
mode=install
mode_selected=0
vm=
for argument in "$@"; do
    case "$argument" in
        --help|-h) usage; exit 0 ;;
        --build-only|--stage-only|--install)
            [[ "$mode_selected" == 0 ]] || { usage >&2; exit 2; }
            mode_selected=1
            mode="${argument#--}" ;;
        -*) printf 'Unknown option: %s\n' "$argument" >&2; exit 2 ;;
        *) [[ -z "$vm" ]] || { usage >&2; exit 2; }; vm="$argument" ;;
    esac
done
if [[ "$mode" != build-only && -z "$vm" ]]; then
    printf 'Supply USER@HOST, or select --build-only.\n' >&2
    exit 2
fi
[[ -z "$vm" || "$vm" =~ ^[a-zA-Z0-9_@.:-]+$ ]] || { printf 'Invalid SSH destination.\n' >&2; exit 2; }
if [[ "$mode" == install && ( ! -t 0 || ! -t 1 ) ]]; then
    printf 'Run installation from a terminal for the sudo prompt, or use --stage-only.\n' >&2
    exit 2
fi
cd "$(dirname "$0")/../.."
for tool in podman python3 git; do command -v "$tool" >/dev/null; done
if [[ "$mode" != build-only ]]; then
    ssh -o BatchMode=yes -o ConnectTimeout=5 "$vm" \
        '. /etc/os-release; test "$ID" = arch && command -v pacman >/dev/null'
fi
mkdir -p build-arch-vm/pacman-cache
work=$(mktemp -d "$PWD/build-arch-vm/package.XXXXXXXX")
# Export the active source, not HEAD; ignored helpers/builds/secrets stay excluded.
python3 - "$work" <<'SOURCE'
import os, pathlib, re, subprocess, sys, tarfile
root = pathlib.Path.cwd()
work = pathlib.Path(sys.argv[1])
version = re.search(r'project\(Dzurkel VERSION ([0-9.]+) LANGUAGES CXX\)', (root / 'CMakeLists.txt').read_text()).group(1)
pkgbuild = (root / 'packaging/arch/PKGBUILD').read_text()
assert re.search(r'^pkgver=(.+)$', pkgbuild, re.M).group(1) == version
files = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split(b'\0')
with tarfile.open(work / f'dzurkel-{version}.tar.gz', 'w:gz') as archive:
    for raw in files:
        if not raw:
            continue
        relative = pathlib.Path(os.fsdecode(raw))
        assert not relative.is_absolute() and '..' not in relative.parts
        if os.path.lexists(root / relative):
            archive.add(root / relative, arcname=f'dzurkel-{version}/{relative}', recursive=False)
import hashlib
checksum = hashlib.sha256((work / f'dzurkel-{version}.tar.gz').read_bytes()).hexdigest()
(work / 'PKGBUILD').write_text(pkgbuild.replace('@SOURCE_SHA256@', checksum))
(work / 'version').write_text(version)
SOURCE
cp packaging/arch/build-test-package.sh "$work/build-package.sh"
printf 'Building current source in disposable Arch container: %s\n' "$work"
podman run --rm --userns=keep-id --user 0:0 \
    -e "BUILDER_UID=$(id -u)" -v "$work:/work:Z" \
    -v "$PWD/build-arch-vm/pacman-cache:/var/cache/pacman/pkg:Z" \
    docker.io/library/archlinux:base-devel bash /work/build-package.sh \
    2>&1 | tee "$work/container-build.log"
version=$(cat "$work/version")
package="dzurkel-$version-1-x86_64.pkg.tar.zst"
test -f "$work/artifacts/$package"
printf 'Testing package with optional libraries absent in a fresh runtime container.\n'
podman run --rm -i --userns=keep-id --user 0:0 \
    -v "$work/artifacts:/artifacts:ro,Z" \
    -v "$PWD/build-arch-vm/pacman-cache:/var/cache/pacman/pkg:Z" \
    docker.io/library/archlinux:base \
    bash -s -- "$package" <<'RUNTIME' 2>&1 | tee "$work/artifacts/runtime-check.log"
set -euo pipefail
pacman -Syu --noconfirm --needed dbus
pacman -U --noconfirm "/artifacts/$1"
for package in qt6-multimedia syntax-highlighting kwindowsystem antiword kio kcoreaddons; do
    if pacman -Q "$package" >/dev/null 2>&1; then
        printf 'Unexpected optional runtime dependency installed: %s\n' "$package" >&2
        exit 1
    fi
done
for adapter in audio highlighting desktop folder_size folder_size_gio; do
    test -f "/usr/lib/dzurkel/plugins/libdzurkel_$adapter.so"
done
ldd /usr/lib/dzurkel/plugins/libdzurkel_audio.so | grep -F 'libQt6Multimedia.so.6 => not found'
# Probe resolves adapters relative to the same /usr/bin location as the app.
install -m755 /artifacts/dzurkel_optional_readers_tests /usr/bin/
install -m755 /artifacts/dzurkel_folder_size_tests /usr/bin/
export QT_QPA_PLATFORM=offscreen DZURKEL_TEST_AUDIO_AVAILABLE=0
export XDG_RUNTIME_DIR=/tmp/dzurkel-runtime-test
mkdir -m700 "$XDG_RUNTIME_DIR"
dbus-run-session -- /usr/bin/dzurkel_optional_readers_tests \
    catalogDoesNotDependOnInstalledReaders \
    missingAudioExplainsRequirementAndOpensLibraries \
    missingAntiwordHasDistinctRecoveryAction baselineRemainsUsable
XDG_CURRENT_DESKTOP=XFCE dbus-run-session -- /usr/bin/dzurkel_folder_size_tests \
    realDesktopCalculatorHandlesNestedAndEmptyFolders realDesktopErrorsAndCancellationDoNotBecomeSuccessfulZero
# GLib is already a standard runtime here; hide only our module to check fallback.
mv /usr/lib/dzurkel/plugins/libdzurkel_folder_size_gio.so /tmp/dzurkel-folder-size-gio.so
dbus-run-session -- /usr/bin/dzurkel_folder_size_tests missingDesktopCalculatorKeepsFolderUsable
mv /tmp/dzurkel-folder-size-gio.so /usr/lib/dzurkel/plugins/libdzurkel_folder_size_gio.so
/usr/bin/dzurkel --version
RUNTIME
printf 'Runtime isolation checks passed. Package: %s/artifacts/%s\n' "$work" "$package"
if [[ "$mode" == build-only ]]; then exit 0; fi

python3 - "$work/artifacts" <<'FIXTURES'
from pathlib import Path
import sys, wave
directory = Path(sys.argv[1])
(directory / 'library-check.cpp').write_text('// Highlighting is optional.\nint main() { return 0; }\n')
with wave.open(str(directory / 'library-check.wav'), 'wb') as audio:
    audio.setnchannels(1)
    audio.setsampwidth(2)
    audio.setframerate(16000)
    audio.writeframes(bytes(32000))  # One second of silence; no surprise sound.
FIXTURES
cp packaging/arch/install-test-package.sh "$work/artifacts/install.sh"
remote="Dzurkel-packages/$(basename "$work")"
ssh -o BatchMode=yes -o ConnectTimeout=5 "$vm" "mkdir -p '$remote'"
scp -o BatchMode=yes "$work/artifacts/$package" "$work/artifacts/install.sh" \
    "$work/artifacts/library-check.cpp" "$work/artifacts/library-check.wav" "$vm:$remote/"
if [[ "$mode" == install ]]; then
    printf '\nInstalling in %s through pacman; enter the VM sudo password when prompted.\n' "$vm"
    ssh -t -o BatchMode=yes "$vm" "bash '$remote/install.sh'"
else
    printf '\nStaged package. In the Arch VM run:\n  bash ~/%s/install.sh\n' "$remote"
fi
