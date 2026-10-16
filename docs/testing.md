# Validation

## Environment

Validated on 2026-10-16 with Fedora 44 KDE, Plasma 6.7.5, Dolphin 26.08.1,
Qt 6.11.2, CMake 4.3.0, and GCC 16.2.1. The desktop session uses Wayland.

## Milestone 2 automated checks

Configure and build using the README, then run:

```bash
ctest --test-dir build --output-on-failure
```

Four suites pass:

- Qt Test: 19 checks including setup/cleanup, resource validation, special and
  Unicode filenames, localhost URLs, command parsing, window reuse, and
  Space/Escape dismissal with child focus.
- Preview providers: 22 checks including setup/cleanup cover PNG/JPEG/BMP,
  orientation, image budgets, UTF-8/16/32 and Latin-1, newline normalization,
  text truncation/binary fallback, multipage/encrypted/damaged/oversized PDFs,
  directories/links/FIFOs, safe file replacement, bounded request coalescing,
  cancellation, child-widget dismissal, and repeated provider/document release.
- Isolated D-Bus: six checks including setup/cleanup, absent-instance no-ops,
  headless secondary forwarding, caller-relative paths, visibility-based Toggle,
  resource errors, ten show/hide cycles, eight simultaneous startup contenders,
  graceful Quit, and restart after a killed primary.
- Headless CLI: 12 cases covering help/version, conflicting commands, unexpected
  file arguments, missing arguments/files, and unsupported schemes/options.

D-Bus tests use `dbus-run-session` and Qt's offscreen backend. They require local
Unix sockets; restricted sandboxes may need permission to create a test bus.
Tests never use the desktop's normal session bus. Offscreen dismissal cannot
prove Wayland focus restoration to Dolphin.

## Sanitizers and PDF cleanup

An AddressSanitizer/UndefinedBehaviorSanitizer build passes all four suites with
leak detection enabled and a narrow Qt PDF suppression:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1 \
LSAN_OPTIONS="suppressions=$PWD/tests/sanitizers/qt-pdf.lsan-supp" \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-sanitize --output-on-failure
```

On Fedora, sanitizer runtimes require `libasan` and `libubsan`. The suppression
covers only `FPDFAvail_Create`: Qt 6.11.2 leaves that allocation after unsuccessful
or password-protected loads because its
[`close()` path](https://github.com/qt/qtwebengine/blob/v6.11.2/src/pdf/qpdfdocument.cpp#L729)
returns when no parsed document exists. Small allocations can therefore
accumulate if those inputs are previewed repeatedly on this Qt version. This
library limitation remains unresolved; no suppression covers Dzurkel allocations.

The PDF widget explicitly stops and releases renderer threads through the public
render-mode setter before deletion, avoiding the installed Qt version's viewer
thread leak. Document and backing-file ownership are tested across replacements.

## Native Wayland lifecycle and memory

An isolated-bus native Wayland check successfully cycled through image, PDF,
text, and information previews twice, then hid, reopened a PDF, and gracefully
quit one resident process. Secondary commands used an invalid GUI backend to
verify headless forwarding. Qt diagnostics reported native Wayland activation.
Rendered image/text/PDF/information captures were also visually inspected.

Two Debug-build samples measured 92,840–94,876 KiB RSS while hidden after eight
content replacements. Earlier Milestone 1 filename-only readings were 65,412–65,428 KiB.
These process RSS samples include Qt/PDF/font/platform caches and are not private
heap usage or a sustained leak/performance benchmark. The parser limitation
above is independent of these samples.

## Dolphin acceptance remains manual

The user-local executable and service menu are installed. Shortcut assignment,
selected-file delivery through Dolphin, keyboard focus restoration, display
scaling, multiple monitors, and themes still require the
[desktop acceptance checklist](dolphin-integration.md). No existing shortcut has
been changed by the implementation.

`desktop-file-validate` flags `MimeType` and `Actions` for KDE's documented
`Type=Service`: its freedesktop schema does not recognize that KDE combination.
The generated file follows the KDE service-menu format; actual discovery and
shortcut invocation must still be checked in Dolphin.

## Earlier Milestone 0

The original two suites passed (17 Qt checks and seven CLI cases), and native
Wayland startup reached an active filename preview. The full Dolphin
Select → Space → Preview → Space/Escape → Return flow has not been recorded as
verified. Milestone 1 changed dismissal from process exit to hiding a resident window.
Milestone 2 adds actual content, so repeat desktop acceptance against the updated
binary. Milestone 3 remains the desktop integration acceptance phase.
