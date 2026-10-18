#!/usr/bin/env bash
# Build from committed public source, never from ignored/private files.
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/source-archive.sh
prepare_source build-deb
cp -a packaging/debian "$package_dir/dzurkel-$version/debian"
chmod +x "$package_dir/dzurkel-$version/debian/rules"
[[ $(dpkg-parsechangelog -l "$package_dir/dzurkel-$version/debian/changelog" -S Version) == "$version-1" ]]
mv "$package_dir/dzurkel-$version.tar.gz" "$package_dir/dzurkel_$version.orig.tar.gz"
cd "$package_dir/dzurkel-$version"
dpkg-buildpackage -us -uc
mkdir -p "$package_dir/artifacts"
cp "$package_dir"/*.{deb,dsc,changes,buildinfo,tar.xz,tar.gz} "$package_dir/artifacts/"
cd "$package_dir/artifacts"
sha256sum ./* > SHA256SUMS-debian-13-amd64
