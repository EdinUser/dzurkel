# Performance and footprint

Dzurkel uses shared Qt/KDE libraries and one resident process. It keeps one
worker and one coalesced pending request, bounds image/text/PDF input, and frees
the visible preview on dismissal. It does not index files or start a decoder
process for every preview. The results below describe small synthetic fixtures
on the tested Fedora 44 machine, not arbitrary documents or other hardware.

## Build configurations

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --preset compact
cmake --build --preset compact
ctest --preset compact
cmake --install build-compact --strip
```

`release` is the ordinary optimized baseline. `compact` uses `MinSizeRel` (`-Os`
with GCC), link-time optimization, and removal of unused code/data sections.
The Fedora installer uses these compact settings and strips the installed
executable. Tests and benchmark programs are built but never installed. Debug
and sanitizer builds remain separate and retain their development settings.
The optimization switches are explicit CMake options:
`DZURKEL_ENABLE_IPO` and `DZURKEL_GC_SECTIONS`. Unsupported requested LTO fails
configuration instead of silently producing a different build.

No architecture-specific instructions, fast-math assumptions, reduced safety
checks, or disabled preview types are used. Stripping saves disk space; it does
not remove an equivalent amount of resident RAM, since debug sections normally
are not mapped into the running process.

## Reproduce latency and stability measurements

The presets enable `DZURKEL_BUILD_BENCHMARKS`. Run each measurement separately,
after builds and tests finish:

```bash
dbus-run-session -- ./build-compact/dzurkel_benchmark \
  --cycles 500 --output build-compact/benchmark-stability.json

dbus-run-session -- ./build-compact/dzurkel_benchmark \
  --cycles 10 --platform wayland --output build-compact/benchmark-wayland.json
```

Wayland mode opens real preview windows and needs a running desktop session.
Offscreen is the default. `--kinds pdf` or `--kinds image,text` isolates providers;
`--executable /absolute/path/to/dzurkel` measures another build or the installed
binary. Use a separate bus: the harness refuses to use an already-owned service.
It creates and later terminates only its own primary process.

Each cycle opens a 1600×1200 PNG, a one-page vector PDF, a roughly 68 KiB text file,
and an unsupported binary. Every preview is followed by Hide. Assertions check
the expected URL, provider kind, headless CLI forwarding, and clean primary exit.
The JSON report contains CLI and ready-state min/median/p95/max timings and a
memory/thread sample after each cycle. Measurements are observations, not
hardware-independent pass thresholds.

`cli_ms` includes process startup, validation, forwarding, and acknowledgement.
`ready_ms` also waits for the expected non-loading D-Bus state, polling every
2 ms. It does not measure the first painted frame or completed PDF rendering.
`cold_ready_ms` is one fresh-process text opening with a warm OS cache, not a
statistical disk-cold startup measurement.

## Memory accounting and PDF runtime

Linux RSS includes shared libraries and mapped pages. PSS apportions shared
pages across processes. The harness reads `/proc/<child>/smaps_rollup` and reports
private clean plus private dirty pages separately; these exclude shared pages
but include allocator arenas and caches, not just live application objects.
PSS changes with other running Qt applications and the benchmark itself.
Sanitizer memory totals are unsuitable for comparison with production builds.

Repeated PDF opening originally restarted Qt's PDF runtime each time. Source
inspection and provider-isolation runs identified this as the main source of
reload cost and allocator retention in the tested Qt version. After the first
successful PDF preview, the controller now retains an **empty** `QPdfDocument`
to keep that runtime initialized. Actual documents, backing files, and viewer
widgets are still released on dismissal; a regression test checks eight
successive documents. The empty runtime is released at application shutdown.
Text-only startup does not construct it.

The implementation uses public Qt objects. Qt 6.11.2's
[runtime initialization and destruction](https://github.com/qt/qtwebengine/blob/v6.11.2/src/pdf/qpdfdocument.cpp#L99)
explain why retaining one document object avoids repeated PDFium startup.
The separate malformed/password-protected PDF parser limitation and its narrow
sanitizer suppression remain documented in [testing](testing.md).

## Measured results: 2026-10-16

These benchmark and size results describe version 0.5.0. Version 0.5.1 adds a
lazily created Settings dialog and shortcut setup using existing Qt libraries;
its stripped executable is 196,160 bytes (191.6 KiB), with no additional shared
library dependency. The figures below are retained as the 0.5.0 optimization
baseline; the long-run measurements have not been repeated for 0.5.1.

Fedora 44, Qt 6.11.2, GCC 16.2.1; shared system libraries and KDE activation
enabled. The original Debug executable was 6,258,680 bytes. The size-focused
stripped executable is **133,504 bytes (130.4 KiB)**; including both desktop
entries, the three-file installation totals **134,014 bytes**. This is about
98% less executable disk space than the original Debug installation. Build
symbols and test artifacts remain in build directories, outside the installation.

| Executable configuration | Bytes |
| --- | ---: |
| Original Debug, with symbols | 6,258,680 |
| Ordinary Release, stripped | 170,480 |
| Compact MinSizeRel + LTO + section removal, stripped | 133,504 |

The compact executable is also 21.7% smaller than the stripped Release baseline.

The final compact run completed **500 cycles / 2,000 previews** offscreen, with
no concurrent builds or test runs, followed by clean primary shutdown:

| Fixture | Median ready state | p95 ready state |
| --- | ---: | ---: |
| Image | 33.9 ms | 44.2 ms |
| PDF | 26.5 ms | 36.3 ms |
| Text | 28.7 ms | 39.0 ms |
| Information | 26.1 ms | 35.4 ms |

Fresh-process text ready state was 71.1 ms (one sample). Initial text private
memory was 10.9 MiB. After all providers warmed up, hidden private memory was
25.7 MiB at cycle 100 and **29.4 MiB at cycle 500**. Cycles 400–500 varied only
from 30,148 to 30,152 KiB private memory; threads remained at nine. Final RSS was
77.8 MiB and PSS 33.5 MiB, illustrating why RSS alone overstates private cost.
This observed plateau applies to these fixtures and this run, not all inputs.

Before retaining the PDF runtime, a 100-cycle compact run grew from 23,892 to
38,828 KiB private memory between cycles 10 and 100 and had median PDF ready
state of 114.1 ms. In the optimized run, cycles 10–100 grew from 24,608 to
26,360 KiB, and median PDF ready state was 26.5 ms. The runs used the same
fixtures, but this is an observational comparison rather than a controlled
hardware benchmark. The PDF engine change provides the substantial runtime
gain; executable stripping mainly saves disk space.

A separate native Wayland run completed 40 previews with clean shutdown. Median
ready-state times were 55.5 ms (image), 52.9 ms (PDF), 57.1 ms (text), and 53.3 ms
(information); the first PDF is included and its setup raises PDF p95 to 189.9 ms.
Fresh-process text ready state was 164.2 ms. After ten cycles, hidden private
memory was **39.5 MiB**, PSS 58.9 MiB, and RSS 134.6 MiB with eleven threads.
Native platform/rendering costs therefore differ materially from offscreen.
This short native run establishes lifecycle behavior, not a long-run native
memory plateau or Dolphin focus-return acceptance.

## Remaining savings

The app's executable is small compared with its shared dependencies. On the
tested machine, Qt PDF's shared library alone is about 24 MiB on disk; Qt Core,
Gui, and Widgets are also shared with other desktop applications. They are not
bundled into the Dzurkel executable. Their incremental installation cost depends
on what Fedora already has installed. Development headers, static build archives,
test binaries, benchmark reports, and build directories are not installed.

Further runtime savings should target measured document/render caches and idle
allocator retention. Lazy PDF isolation could change the tradeoff between initial
footprint and repeated-preview latency; it needs a separate design decision.
The viewer-dependency/bundling policy remains deferred. Shared libraries should
not be removed just to reduce this checkout's apparent footprint.
