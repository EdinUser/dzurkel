#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/source-archive.sh
prepare_source build-arch
[[ $(sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD) == "$version" ]]
checksum=$(sha256sum "$package_dir/dzurkel-$version.tar.gz" | cut -d ' ' -f 1)
sed "s/@SOURCE_SHA256@/$checksum/" packaging/arch/PKGBUILD > "$package_dir/PKGBUILD"
cd "$package_dir"
makepkg --noconfirm
mkdir artifacts
cp ./*.pkg.tar.zst PKGBUILD "dzurkel-$version.tar.gz" artifacts/
cd artifacts
sha256sum ./* > SHA256SUMS-arch-x86_64
