#!/usr/bin/env bash
# Package the committed checkout; ignored/private files never enter Source0.
set -euo pipefail
cd "$(dirname "$0")/../.."
version=$(sed -n 's/^project(Dzurkel VERSION \([0-9][0-9.]*\) LANGUAGES CXX)/\1/p' CMakeLists.txt)
spec_version=$(awk '/^Version:/ {print $2}' packaging/fedora/dzurkel.spec)
[[ -n "$version" && "$version" == "$spec_version" ]] || {
    echo 'CMake and RPM versions must match.' >&2; exit 1;
}
checkout_status=$(git status --porcelain --untracked-files=normal)
if [[ -n "$checkout_status" ]]; then
    echo 'Commit source changes before packaging; the source archive uses HEAD.' >&2
    exit 1
fi
if [[ ${GITHUB_REF:-} == refs/tags/* && ${GITHUB_REF#refs/tags/} != "v$version" ]]; then
    echo 'Release tag must match the CMake/RPM version.' >&2; exit 1
fi
topdir="$PWD/build-rpm"
rm -rf "$topdir/RPMS" "$topdir/SRPMS" "$topdir/artifacts"
mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
git archive --format=tar.gz --prefix="dzurkel-$version/" HEAD > "$topdir/SOURCES/dzurkel-$version.tar.gz"
rpmbuild -ba --define "_topdir $topdir" packaging/fedora/dzurkel.spec
mkdir -p "$topdir/artifacts"
find "$topdir/RPMS" "$topdir/SRPMS" -type f -name '*.rpm' -exec cp -t "$topdir/artifacts" {} +
cd "$topdir/artifacts"
sha256sum ./*.rpm > SHA256SUMS
