#!/usr/bin/env bash
# Runs only inside the disposable build container, never in the runtime VM.
set -euo pipefail
test -f /run/.containerenv
pacman -Syu --noconfirm --needed base-devel cmake qt6-base qt6-webengine \
    qt6-wayland libzip syntax-highlighting qt6-multimedia kwindowsystem \
    dbus python desktop-file-utils kio pkgconf glib2
builder=$(getent passwd "$BUILDER_UID" | cut -d: -f1 || true)
if [[ -z "$builder" ]]; then
    useradd --create-home --uid "$BUILDER_UID" builder
    builder=builder
fi
builder_group=$(id -gn "$builder")
install -d -o "$builder" -g "$builder_group" /tmp/dzurkel-builder
mkdir -p /work/artifacts
chown -R "$builder:$builder_group" /work
cd /work
runuser -u "$builder" -- env HOME=/tmp/dzurkel-builder PKGDEST=/work/artifacts makepkg --noconfirm --cleanbuild
cp /work/src/build/dzurkel_optional_readers_tests /work/artifacts/
cp /work/src/build/dzurkel_folder_size_tests /work/artifacts/
cp /work/src/build/build-compatibility.json /work/artifacts/
cp /work/src/build/Testing/Temporary/LastTest.log /work/artifacts/build-tests.log
# Verify all optional adapters ship while their backends remain optional metadata.
package="/work/artifacts/dzurkel-$(cat /work/version)-1-x86_64.pkg.tar.zst"
for adapter in audio highlighting desktop folder_size folder_size_gio; do
    bsdtar -tf "$package" | grep -Fx "usr/lib/dzurkel/plugins/libdzurkel_$adapter.so"
done
bsdtar -xOf "$package" .PKGINFO > /work/artifacts/package-metadata.txt
if grep -Eq '^depend = (qt6-multimedia|syntax-highlighting|kwindowsystem|kio|kcoreaddons)([<>= ].*)?$' /work/artifacts/package-metadata.txt; then
    printf 'Optional backends leaked into required package dependencies.\n' >&2
    exit 1
fi
