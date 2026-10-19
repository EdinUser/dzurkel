# Runtime baseline and OS compatibility

The useful minimum is Qt 6 base runtime components, Qt PDF/PDF Widgets and
libzip. It covers file information, basic images, text, Markdown, CSV/TSV,
PDF and the existing bounded XLSX/DOCX/EPUB readers. Existing RTF/FB2/SFB/MOBI
readers remain included. Individual format/variant and resource limits still
apply; see [preview behavior](previews.md).

Audio/video, syntax highlighting, legacy DOC extraction, extra image plugins and
specialised desktop activation are optional. Missing optional readers produce
file information explaining the missing component and an **Open Settings**
action. Missing highlighting leaves plain text. Unknown formats, corrupt files
and unsupported variants keep their own explanations.

Normal distribution packages build and ship media, highlighting and desktop
adapters where their development dependencies exist. Only the adapters link
Qt Multimedia/Multimedia Widgets, KSyntaxHighlighting and KF6 WindowSystem; the executable does not
require those libraries at startup. Adapters are loaded from application-relative
build/install locations and must match the Dzurkel release and Qt runtime.
Successful modules remain loaded until exit. Refresh status can retry previously
missing/unloadable adapters; replacing loaded backends requires restarting.

Settings supplies copyable installation commands for the reviewed Fedora 43/44,
Debian 12/13 and Arch package combinations. A missing Dzurkel adapter cannot be
fixed by installing only its backend library, so that case gives adapter-package
guidance instead of an ineffective library-only command. Unknown releases and
unreviewed derivatives receive explanatory guidance. Commands are never executed
by Dzurkel; no repositories are enabled automatically. The user inspects and runs
the command with their package manager.

Distribution package grouping still matters: Arch supplies Qt PDF in
[qt6-webengine](https://archlinux.org/packages/extra/x86_64/qt6-webengine/), which
also depends on FFmpeg. Optional audio/video in Dzurkel does not remove that separate
package-level dependency.

## Build-time calculation

Each executable build generates `build-compatibility.json`. Installation places
it under the configured data directory, normally
`/usr/share/dzurkel/build-compatibility.json`. Python and readelf are build tools;
the report is static data and needs neither tool on end-user machines.

Open **Settings → Integration → System requirements → OS compatibility…** for
the installed build's runtime requirements and per-distribution results. The
summary distinguishes reviewed dependency candidates from validated minimums;
missing or incompatible reports show an explicit unavailable message. Build-tree
executables read their adjacent report, and installed executables read the report
from their configured data directory relative to the executable.

The calculation runs offline from the reviewed
[distribution catalog](../packaging/compatibility/platforms.json). It does not
query package repositories, derive an OS floor from the build host, or repeat
version calculations on each user's machine. Runtime Settings only checks actual
components and identifies the local distribution for installation guidance.

The report contains:

- Baseline library-version requirements and the earliest reviewed release meeting
  each one. The highest lower bound is followed by a check that one release
  actually provides the whole set together for the reviewed architecture/repos.
- A separate conservative candidate calculation for the built binary's Qt
  versions and libzip SONAME. A newer-system build is not assumed runnable on an
  older baseline candidate. Build-time ELF inspection also records direct
  GLIBC/GLIBCXX/CXXABI requirements and rejects optional startup dependencies.
- Optional adapters' ELF requirements, separately from the executable baseline.
- Evidence coverage and limitations. Direct ELF requirements exclude transitive
  dependencies and platform plugins; native package installation must resolve
  those. Package versions alone do not establish desktop support.

The initial catalog reviews Debian 12/13, Fedora 43/44 and an Arch repository
snapshot. It does not establish Fedora's earliest historical compatible release.
RHEL currently lacks a complete reviewed package set, and Arch's separately
observed package pages need a coherent snapshot check. Unknown evidence remains
unknown; it never becomes an automatic support promise.

`earliest_reviewed_candidate` is a dependency candidate. `supported_minimum` stays
empty until a matching release validation record exists. Records require the
Dzurkel version, architecture, exact baseline requirements, `baseline-native`
profile, an HTTPS evidence URL and passed package-install, text, image, PDF, CSV,
XLSX, DOCX, EPUB and desktop checks. Native package ABI dependency resolution
and real desktop validation remain release gates. Compiler/header/CMake versions
are not presented as the end-user minimum.

The report's `build_sha256` identifies the executable before packaging strips or
signs it. Release package checksums identify the delivered files instead.

## Baseline build and release validation

```bash
cmake --preset baseline
cmake --build --preset baseline
ctest --preset baseline
```

This builds the useful minimum without audio/video, highlighting or KDE SDKs. Existing
compatible adapters can still be added afterward. There is no separate reduced
text-only product/package. The ordinary build uses `DZURKEL_AUDIO=AUTO|ON|OFF`
(default AUTO); ON requires Qt Multimedia and Multimedia Widgets development
files, OFF omits the audio/video adapter. The existing `DZURKEL_AUDIO` option
and `libdzurkel_audio.so` filename are retained for build/package compatibility;
the adapter now implements the versioned media interface for both views.
Highlighting has the equivalent existing build modes, and desktop
activation retains its explicit build option.

The Debian 12 CI gate builds against its native Qt 6.4 without optional SDKs,
runs the baseline/staged-install checks, and uploads the compatibility report and
test log. Adding the gate is not evidence that its remote run has completed.
Release maintainers review evidence before recording a supported OS floor.

Local baseline verification on Debian 12 with Qt 6.4.2 and libzip 1.7.3 passed
all 20 CTest suites in an isolated Debug build without optional SDKs. This
headless result validates the baseline build and staged installation, while
native package/desktop validation remains separate from a supported OS claim.

To test expansion from missing libraries, use the
[Arch VM package workflow](arch-vm-testing.md). It ships adapters and keeps build
dependencies out of the runtime VM, rather than omitting adapters from the product.
