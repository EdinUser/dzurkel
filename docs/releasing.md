# Fedora packages and GitHub releases

The initial binary target is Fedora 44 x86_64. Other RPM distributions, older
Fedora releases, DEB, Arch and AppImage are not certified by this workflow.

## Continuous integration

`.github/workflows/ci.yml` runs on pull requests, pushes to `master`, version tags,
and manual dispatch. It builds in Fedora 44 containers on standard GitHub-hosted
Ubuntu runners. The GCC job builds a native RPM and SRPM with `rpmbuild`; the
spec's `%check` runs every registered CTest suite. Clang builds and runs CTest
separately. Compilation and tests run as an unprivileged build user so file
permission checks behave like a normal desktop session. A fresh Fedora container installs the binary package, verifies its
files, exercises preview IPC on a private bus, and checks removal.

This is a container-based build, not a Mock buildroot. A future Copr/Mock pipeline
can consume the same spec and SRPM. The clean installation check catches missing
runtime requirements independently of the development packages used to compile.

Fedora normalizes packaged `.desktop` files to mode 0644. This is valid for the
system service menu in `/usr/share/kio/servicemenus`: KDE trusts that location.
The source installer retains executable permission for user-local menus, which
need separate authorization. See [KDE's service-menu documentation](https://develop.kde.org/docs/apps/dolphin/service-menus/).

Download `fedora-44-x86_64` from a successful workflow run for testing. Artifacts
expire after 14 days; published releases are the durable download location.
Actions are pinned to commit SHAs and Dependabot checks updates monthly.

## Build an RPM locally

Install the dependencies listed in the workflow, including `rpm-build`,
`redhat-rpm-config` and `cmake-rpm-macros`, then commit the intended source and run:

```bash
bash packaging/fedora/build-rpm.sh
```

The script requires a clean checkout and archives **HEAD**, so ignored private
notes and build products cannot enter the source archive. Output is under
`build-rpm/artifacts/`: binary RPM, debug RPMs when generated, source RPM and
`SHA256SUMS`. CMake and spec versions must match. Local build dependencies can
affect the result; CI supplies the Fedora 44 environment.

## Prepare a release

1. Update the CMake version and RPM `Version` together. Increment RPM `Release`
   for packaging-only rebuilds of the same upstream version.
2. Ensure CI passes for the intended commit. Check the package on a real Fedora
   KDE Wayland desktop: previews, Settings, Dolphin Space setup/restoration,
   scaling/focus, upgrade from the previous package and removal.
3. Create and push a matching tag, for example `v0.6.0`. Tag creation is a
   maintainer action; the workflow does not create tags itself.
4. The tag workflow repeats checks and creates a **draft** GitHub release with
   RPMs and checksums. It refuses to overwrite assets on a published release.
5. Edit the draft notes with compatibility, installation instructions, desktop
   results and known limitations; publish after review.

For example, after downloading release assets into one directory:

```bash
sha256sum --check SHA256SUMS
sudo dnf install ./dzurkel-<version>-1.fc44.x86_64.rpm
dzurkel --settings
```

Checksums detect corruption; they are not package signatures. Initial RPMs are
unsigned and there is no DNF update repository. Signing and a Copr update channel
are follow-up work. Package scripts do not launch desktop programs or edit users'
shortcuts. The Settings launcher provides setup after any installation method.

## Public repository checklist

Before initial publication, inspect tracked files and reachable history for
secrets, private notes and personal fixtures. Confirm the license and asset
provenance. This repository's test GIFs and protected PDF are generated synthetic
fixtures; see `tests/fixtures/README.md`. Review screenshots individually before
adding them. Pattern scans cannot establish the absence of every possible secret.
