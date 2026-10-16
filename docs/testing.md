# Validation

## Environment

Validated on 2026-10-16 with Fedora 44 KDE, Plasma 6.7.5, Dolphin 26.08.1,
Qt 6.11.2, CMake 4.3.0, and GCC 16.2.1. The desktop session uses Wayland.

## Milestone 4 automated checks

Configure and build using the README, then run:

```bash
cmake --preset compact
cmake --build --preset compact
ctest --preset compact
```

Five suites pass:

- Qt Test: 25 checks including setup/cleanup, resource validation, special and
  Unicode filenames, localhost URLs, command parsing, window reuse, and
  Space/Escape dismissal with child focus, Shift+Space dismissal, Ctrl+Space
  preservation, 85% initial geometry, enlargement beyond the default, resize
  retention on reopening, live light/dark palette propagation, Settings link
  keyboard behavior and dialog reuse, shortcut backup/restore with alternate
  bindings and unrelated toolbar edits preserved, malformed/custom-scheme
  rejection, and refusal to overwrite newer managed-shortcut changes.
- Preview providers: 23 checks including setup/cleanup cover PNG/JPEG/BMP,
  orientation, image budgets, UTF-8/16/32 and Latin-1, newline normalization,
  text truncation/binary fallback, multipage/encrypted/damaged/oversized PDFs,
  directories/links/FIFOs, safe file replacement, bounded request coalescing,
  cancellation, child-widget dismissal, and repeated provider/document release,
  including actual-document release with the retained empty PDF runtime.
- Isolated D-Bus: eight checks including setup/cleanup, absent-instance no-ops,
  headless secondary forwarding, caller-relative paths, visibility-based Toggle,
  resource errors, ten show/hide cycles, eight simultaneous startup contenders,
  graceful Quit, restart after a killed primary, exact synthetic activation-token
  forwarding for Show/Toggle without a GUI backend, and invalid-option rejection
  without changing the previous preview.
- Headless CLI: 12 cases covering help/version, conflicting commands, unexpected
  file arguments, missing arguments/files, and unsupported schemes/options.
- Staged installation: exact three-file manifest, desktop permissions and IDs,
  stripped binary version, and Show/Hide/Quit through headless secondary commands
  against the installed executable on its own session bus.

D-Bus tests use `dbus-run-session` and Qt's offscreen backend. They require local
Unix sockets; restricted sandboxes may need permission to create a test bus.
Tests never use the desktop's normal session bus. Offscreen dismissal cannot
prove Wayland focus restoration to Dolphin.

The default KDE WindowSystem build and a separate Qt-only build both compile.
The application desktop entry passes `desktop-file-validate`. A staged install
contains the executable, executable Dolphin service menu, and hidden application
identity with the configured absolute command. Light/dark offscreen captures
were visually inspected; these do not verify Breeze theme switching on Plasma.

## Sanitizers and PDF cleanup

An AddressSanitizer/UndefinedBehaviorSanitizer build passes all five suites with
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

## Fedora packaging and optimization

Release and MinSizeRel/LTO builds pass all five suites. The Fedora script was
also run from a fresh build directory with prefix `/opt/Dzurkel test` and a
checkout-local `DESTDIR`. Build, tests, desktop validation, and stripped install
succeeded without writing to `/opt` or modifying the source. A fresh Fedora VM
has not been tested; the declared package preflight was checked on the environment
above. This is a source installation script, not an RPM or byte-identical build
guarantee. See [installation](installation.md).

The repeatable [performance harness](performance.md) records ready-state latency,
RSS, PSS, private pages, threads, and repeated-provider stability. Its raw reports
live in ignored build directories. Reported latency excludes any claim about
first-painted-frame timing. The performance document records measured results
and distinguishes shared dependency size from the installed executable.

The final compact run completed 2,000 previews and clean shutdown. Hidden private
memory varied by only 4 KiB during the final 100 cycles. A separate native Wayland
run completed 40 previews; native private memory was 39.5 MiB after ten cycles.
Thirty repeated PDF reloads also exited cleanly under ASan/UBSan with leak
detection enabled. See the performance report for fixture details and limits;
the offscreen plateau does not establish a native long-run memory plateau.

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

The Milestone 3 native Wayland run repeated the same image/PDF/text/information
lifecycle successfully with KDE activation enabled; diagnostics reported active
windows. Its hidden RSS sample was 113,588 KiB. Screen inventory showed two
1920×1080 displays at scale 1. This does not establish resizing, focus restoration,
or screen transfer through Dolphin: those interactions remain manual below.

## Dolphin acceptance remains manual

Version 0.5.1's compact stripped build is installed locally with the executable, service menu, and hidden
application identity. The user confirmed that resizing works and reported that
the window "kind of remembers the size." Size retention is process-local and
may be reduced to fit a smaller display; full cross-monitor retention remains
unverified. Shortcut assignment,
selected-file delivery through Dolphin, keyboard focus restoration, display
scaling, multiple monitors, and themes still require the
[desktop acceptance checklist](dolphin-integration.md). No existing shortcut has
been changed by the implementation.

The preview now has a Settings link and a desktop setup dialog. Automated setup
tests use temporary configuration/backup paths; the agent did not apply shortcuts
to the user's Dolphin configuration. Opening Settings performs no writes. The
dialog screenshot was inspected; end-to-end setup, restart, Space invocation and
Restore through the real Dolphin installation remain manual checks.

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
