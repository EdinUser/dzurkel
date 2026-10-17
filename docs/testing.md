# Validation

## Environment

Validated on 2026-10-17 with Fedora 44 KDE, Plasma 6.7.5, Dolphin 26.08.1,
Qt 6.11.2, CMake 4.3.0, and GCC 16.2.1. The desktop session uses Wayland.

## Milestone 4 automated checks

Configure and build using the README, then run:

```bash
cmake --preset compact
cmake --build --preset compact
ctest --preset compact
```

The current development checkout has seven suites:

- Qt Test: 25 checks including setup/cleanup, resource validation, special and
  Unicode filenames, localhost URLs, command parsing, window reuse, and
  Space/Escape dismissal with child focus, Shift+Space dismissal, Ctrl+Space
  preservation, 85% initial geometry, enlargement beyond the default, resize
  retention on reopening, live light/dark palette propagation, Settings link
  keyboard behavior and dialog reuse, shortcut backup/restore with alternate
  bindings and unrelated toolbar edits preserved, malformed/custom-scheme
  rejection, and refusal to overwrite newer managed-shortcut changes.
- Preview providers and lifecycle checks cover PNG/JPEG/BMP,
  orientation, image budgets, UTF-8/16/32 and Latin-1, newline normalization,
  text truncation/binary fallback, multipage/encrypted/damaged/oversized PDFs,
  directories/links/FIFOs, safe file replacement, bounded request coalescing,
  cancellation, child-widget dismissal, and repeated provider/document release,
  including actual-document release with the retained empty PDF runtime.
  Information-only views also compact temporarily, preserve normal/maximized
  preview dimensions across repeated fallback views and hide/reopen, and keep
  the next image's decode target at preview resolution.
  Cold ZIP/ODT cases record compact dimensions at the first Show event and then
  verify large PDF restoration. Hide/Toggle and supersession before first
  mapping prevent obsolete worker results from presenting a window.
- Isolated D-Bus: eight checks including setup/cleanup, absent-instance no-ops,
  headless secondary forwarding, caller-relative paths, visibility-based Toggle,
  resource errors, ten show/hide cycles, eight simultaneous startup contenders,
  graceful Quit, restart after a killed primary, exact synthetic activation-token
  forwarding for Show/Toggle without a GUI backend, and invalid-option rejection
  without changing the previous preview.
- Headless CLI: 12 cases covering help/version, conflicting commands, unexpected
  file arguments, missing arguments, password-bearing SMB URLs, and unsupported schemes/options.
- Staged installation: exact three-file manifest, desktop permissions and IDs,
  stripped binary version, and Show/Hide/Quit through headless secondary commands
  against the installed executable on its own session bus.
- Settings on an isolated bus: information-button keyboard accessibility and
  persistent details, apply/restore with Dolphin closed, restart cancellation,
  waiting for every window before applying or restoring, refused-close timeout,
  unreachable/new-process rejection, and reporting a reopen failure while
  retaining the applied change and backup. All configuration paths are temporary.
  An additional opt-in check gracefully closed a real installed Dolphin on a
  separate bus with temporary XDG configuration; it is skipped by default.

D-Bus tests use `dbus-run-session` and Qt's offscreen backend. They require local
Unix sockets; restricted sandboxes may need permission to create a test bus.
Tests never use the desktop's normal session bus. Offscreen dismissal cannot
prove Wayland focus restoration to Dolphin.

Settings restart focus tests cover delayed Dolphin activation, one activation
request per apply/restore, close/reopen and minimize/restore cancellation,
destruction, readiness timeout, and worker cancellation. They use an isolated
bus and temporary configuration; they do not prove compositor focus acceptance.
On native KDE Wayland, apply and restore with Dolphin running and check that
Settings receives keyboard input after Dolphin reopens. Include slow startup,
close prompts, multiple windows, switching applications during restart, and
closing/minimizing Settings before Dolphin appears. Verify no later activation
or always-on-top behavior. Qt-only and KDE Frameworks before 6.19 use a single
best-effort Qt activation request; newer KDE builds capture a Wayland activation
token from the confirmed restart action. Compositor policy can reject it,
particularly after intervening user input.

The default KDE WindowSystem build compiles. A separate Qt-only build was last
checked during the Settings focus follow-up, before the cold-placement change.
The application desktop entry passes `desktop-file-validate`. A staged install
contains the executable, executable Dolphin service menu, and hidden application
identity with the configured absolute command. Light/dark offscreen captures
were visually inspected; these do not verify Breeze theme switching on Plasma.

## Sanitizers and PDF cleanup

Earlier AddressSanitizer/UndefinedBehaviorSanitizer validation passed all six
suites with leak detection and a narrow Qt PDF suppression. After the latest
cold-placement and queued-presentation changes, the preview suite was rerun and
passed with the same settings. This is not a claim that every sanitizer suite
was rerun after that follow-up. To run all suites:

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

Original Release and MinSizeRel/LTO checks passed the five suites then present;
the current Fedora MinSizeRel/LTO install reran all six successfully. The Fedora script was
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

## Desktop feedback and remaining acceptance

Version 0.6.0's compact stripped build, including the Settings focus and cold
information-placement fixes, is installed locally with the executable, service menu, and hidden
application identity. The user confirmed that resizing works and reported that
the window "kind of remembers the size." Size retention is process-local and
may be reduced to fit a smaller display; full cross-monitor retention remains
unverified. Shortcut assignment,
selected-file delivery through Dolphin, keyboard focus restoration, display
scaling, multiple monitors, and themes still require the
[desktop acceptance checklist](dolphin-integration.md). Automated validation has
not changed the user's Dolphin shortcut configuration.

The preview now has a Settings link and a desktop setup dialog. Automated setup
tests use temporary configuration/backup paths; the agent did not apply shortcuts
to the user's Dolphin configuration. Opening Settings performs no writes. The
redesigned dialog was captured and inspected with the native KDE theme. Hover
details also open as keyboard-accessible persistent information dialogs. A
running Dolphin triggers explicit restart confirmation for both setup and
restore; its graceful D-Bus quit was verified against an isolated real Dolphin.
Native Wayland tests also exercised setup/restore with two simulated windows;
the enabled-state and restart-confirmation captures were inspected. Tests force
Qt-owned modal widgets so they can answer prompts; production retains the
platform's dialog backend.
User-session window/tab restoration, Space invocation and Restore through the
real Dolphin installation remain manual checks.

The user confirmed that the 0.6.0 settings work, then reported that reopened
Dolphin covered Settings. A one-time activation handoff is now implemented and
installed; isolated-bus tests cover readiness, apply/restore, cancellation and
timeout. The user has not explicitly confirmed native focus acceptance after
this fix. Exact window/tab restoration and the other desktop checks remain open.

The user subsequently reported a fresh-process ODT information view at the
large loading window's top-left position. Initial provider selection now happens
before mapping; information results map compactly. Targeted native Wayland tests
passed for first-open ZIP/ODT, repeated information/PDF reopening and cancellation
before mapping. A temporary read-only KWin observer measured centred frames
`(2690, 348, 380, 340)` for ZIP and `(2690, 339, 380, 358)` for ODT within usable
area `(1920, 0, 1920, 1036)`; reopened PDFs were `(2064, 45, 1632, 946)`.
The observer was unloaded without changing window rules. The user explicitly
confirmed that the first icon opening is fixed. These results do not complete
the broader shortcut, theme, scaling or cross-monitor acceptance matrix.

A read-only process check after dismissal confirmed one resident process with
four sleeping threads (main, D-Bus, two Wayland event threads), using 94,732 KiB
RSS in total. The user saw htop's row count drop after idling. Shared thread RSS
must not be summed; this is expected resident behaviour, not evidence of
duplicate instances. It is a single observation, not a new long-run benchmark.

Temporary compact information-window sizing and restoration passed three
targeted native Wayland tests, including four cold-information/PDF close/reopen
cycles with content teardown and late compositor-configure checks. Captures show
the icon and metadata immediately in the compact view and a restored large PDF
view. Initial native attempts encountered exposure timeouts; standalone reruns
passed. This does not resolve the separate Dolphin shortcut acceptance checks.

Reopening after an information view restores normal dimensions before mapping,
so Wayland places the larger window rather than expanding it at the compact
window's position. The regression checks these dimensions before provider
completion. A temporary read-only KWin observer restricted to generated test
files verified the actual PDF frame at `(144, 45, 1632, 946)` within the usable
`1920 × 1036` screen across four close/reopen cycles. Qt's client geometry alone
does not establish compositor placement. Custom-size tests now wait for initial
native placement before simulating a user resize. The observer was unloaded
after validation; no KWin settings or window rules were changed.

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

## SMB and asynchronous resource loading

The resource suite uses an isolated fake KIO-FUSE service and real helper
processes. It verifies exact URL delivery, username/Unicode handling, image/PDF/
text snapshots, source mutation after copying, bounded text and oversized PDF
handling, directory/link/FIFO information views, authentication failure, missing
local files, responsive dismissal/replacement, stale-result rejection, timeout
and backing-directory cleanup. It does not contact a real SMB server.

CLI/D-Bus tests now distinguish synchronous URL errors from asynchronous
filesystem failures. The staged executable exercises its own helper mode; the
installation remains three files. Existing window placement, decoder lifecycle
and settings suites remain required regressions.

Validation on 2026-10-17: the main and Qt-only builds compile, and all seven
main-build suites pass. The optional live test decoded and exposed real PDF and
JPEG previews in the native KDE Wayland session through authenticated SMB URLs,
without using `/mnt`. Username-free URLs failed access even after Dolphin login;
using the same username-qualified URL as Dolphin succeeded. No credentials or
server files were changed. Controlled slow-share dismissal/replacement and
timeout tests also pass with Wayland windows and an isolated fake service.

To repeat the opt-in real-share check (it briefly opens a preview window):

```bash
DZURKEL_LIVE_SMB_URL='smb://user@server/share/example.pdf' \
  QT_QPA_PLATFORM=wayland ./build/dzurkel_resource_tests liveAuthenticatedSmbPreview
```

Run only that test on the desktop bus; the default suite uses its own fake
service and skips the live check. No file contents are printed.

Remaining native checks: trigger the updated installed Dolphin action/shortcut;
real SMB text content (covered by the fake service so far); authentication
cancellation/retry; actual connection loss; and visual acceptance of slow loader
placement and repeated navigation/reopening. Keep existing mounts and server
files unchanged. The development implementation was subsequently reinstalled under `~/.local`
at the user's request: the Fedora MinSizeRel/LTO installer passed all seven
suites (18.41 seconds), installed both desktop entries and the stripped binary,
and KDE's service cache was refreshed. The installed menu accepts file and smb.

Final resource-loading sanitizer check: ASan/UBSan builds compiled and the
preview/resource suites both passed with leak detection and the existing narrow
Qt PDF suppression (12.77 seconds). Other sanitizer suites were not rerun for
this cycle. The final normal run passed all seven suites in 18.76 seconds.
