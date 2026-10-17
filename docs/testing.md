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

The current development checkout has ten suites:

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

- Interactions on an isolated bus: image/PDF zoom, fit/reset, fractional
  Ctrl+wheel input, ordinary image scrolling, bounded image detail, provider
  replacement, native PDF/text arrows versus file-navigation shortcuts, previous/
  next buttons and boundaries, natural filename ordering, hidden anchors,
  directory/link/FIFO exclusion, deletion, listing budgets/cancellation, and
  original SMB/SFTP URL preservation through a fake KIO-FUSE service.

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

## Experimental SFTP follow-up

The SFTP development changes build with KDE activation enabled and disabled.
All seven normal suites pass (22.79 seconds). The resource suite parameterizes
copy limits, snapshots, special objects, access failure, cancellation/replacement
and timeout across SMB and opt-in SFTP using a fake KIO-FUSE service. It also
checks default denial, optional ports/IPv6/Unicode, invalid URLs, CLI admission,
policy reset and that other schemes stay disabled. The D-Bus suite verifies
live enable/disable changes on the resident service and preservation of the
previous preview when admission is denied. Configuration is isolated from the
user's preferences.

Settings tests verify keyboard toggling, persistence, reopening/external changes
and the accessible SFTP information control. The new checkbox test also passed
with real Wayland windows on an isolated bus. The generated `settings-sftp.png`
layout was visually inspected. Resource and Settings suites passed ASan/UBSan
with leak detection and the existing narrow Qt PDF suppression (14.29 seconds);
other sanitizer suites were not rerun for this follow-up.

The optional [real-server harness](../tests/integration/SftpSmoke.py) starts its
temporary OpenSSH server but cannot create a private KIO-FUSE mount in this
environment (`Operation not permitted`). It cleanly reports `unavailable` with
exit status 77. This does not validate the actual SFTP worker/server transport,
authentication or relative performance. See [compatibility](compatibility.md)
for prerequisites, invocation and the outside-tester checklist. At the user's subsequent request, the Fedora installer rebuilt and installed
this experimental version under `~/.local`, passing all seven suites in 21.30
seconds. KDE's service cache was refreshed and the installed SFTP-capable menu
verified. The SFTP preference was not enabled automatically; user field testing
is pending.

Subsequent user field feedback on 2026-10-17 confirmed direct SFTP previews on
their normal Fedora KDE setup, and previews through a mounted S3 volume. Exact
formats/authentication details and failure scenarios were not specified. The S3
result is mounted-path access, not direct s3:// support. SFTP remains experimental
while broader validation is incomplete.

## Zoom and folder-navigation validation

On 2026-10-17 the development build and all eight suites passed (12.45s).
The interaction suite also passed ASan/UBSan with leak detection (2.41s), using
only the existing narrow Qt PDF suppression. Native Wayland image zoom, PDF zoom
and mixed-file navigation widget checks passed; a final PDF check additionally
verified visible rendered page content after zoom/fit. Light/dark widget layouts
were inspected. These checks use temporary files/configuration and an isolated
session bus; they do not establish real file-manager focus or remote transport
acceptance.

Still verify the installed workflow manually: physical touchpad input, alternate
keyboard layouts, high-DPI/multiple monitors, repeated previews, and sibling
navigation over actual SMB/SFTP locations, including unavailable/changed folders.
The controlled remote tests cover URL preservation and listing behavior through
a fake KIO-FUSE service. Previously confirmed single-file SMB/SFTP access does
not by itself verify the new sibling-navigation workflow.

### Zoom, arrows and GIF follow-up

The 2026-10-17 follow-up adds directional 10 percentage-point zoom steps, image
fit capped at 100%, fitted-image/information Up/Down browsing, and zoomed-image
Up/Down scrolling even when a toolbar button has focus. Alt+all arrows browse;
PDF/text unmodified arrows remain native. Tests cover 121% snapping, exact-step
floating-point boundaries, small-image size retention and deliberate enlargement.

Synthetic GIFs verify three frame colors/delays, infinite and single-pass looping,
actual visible frame changes, pause/resume, replay, zoom retention across frames,
replacement/dismissal cleanup, and static fallback on frame-count/memory limits.
Pillow generated the committed synthetic fixtures; it is not a test/runtime
requirement. Main build and all eight suites pass (13.51s). Five relevant native
Wayland widget cases plus setup/cleanup pass (2.648s). Physical input, real remote
GIFs and broader desktop acceptance still require user testing.

The expanded GIF/zoom/arrow interaction suite also passed ASan/UBSan with leak
detection (3.87s), using only the existing narrow Qt PDF suppression.

## Preview library / CSV checks

The library suite covers reader IDs/source budgets, signature priority, quoted
multiline records and escaped quotes, empty/ragged fields, literal leading zeros
and formula-like strings, comma/semicolon/tab detection, UTF-8 BOM/UTF-16/Latin-1,
byte/row/column/cell/field limits, incomplete-record omission, cancellation,
malformed-text and binary fallbacks, sparse read-only models, sheet switching,
escaped tooltips, table keyboard navigation/dismissal and local/fake SMB/SFTP
preparation with original URL retention. Settings checks include the CSV support
information control. Real share CSV performance and ambiguous delimiter behavior
remain user acceptance checks. XLSX has its own suite described below.

Foundation validation: all nine main-build suites passed (17.33s). Native
Wayland table/grid, sheet switching and navigation/dismissal checks passed
(three cases plus setup/cleanup, 1.213s). The dark-theme grid screenshot was
inspected. Remote coverage uses a fake service; real-share acceptance remains
pending.

The library suite passed ASan/UBSan with leak detection (1.34s). The optimized
Fedora installer passed all nine suites (29.64s) and the installed binary was
verified against that build. Installed-app CSV acceptance is pending user testing.

## XLSX stored-value checks

`dzurkel_xlsx` tests shared/inline/rich strings as plain text, long numeric
lexemes, booleans, saved formula results (including zero and empty strings),
visible missing-result markers/counts, error values, unknown style fallback,
multiple sheets, Strict OOXML namespaces and
implicit coordinates. It verifies actual-cell dimensions despite inflated
declared ranges, partial row/column/cell limits, malformed XML/DTD rejection,
invalid shared-string references, missing package parts, external-sheet and
path-traversal rejection, ZIP entry/expanded-byte/part/field limits, and
cancellation during loading. Local and fake SMB snapshot preparation both use
the shared read-only grid and keyboard dismissal.

The review refinements add 1900/1904 calendar cases, the fictional 1900 leap
day, built-in/custom date/time formats, elapsed durations, millisecond precision,
midnight rounding and raw fallback for unsupported formats or invalid serials.
Each format case runs against both transitional and Strict packages and checks
literal cells and saved formula results. Malformed style XML, invalid format IDs,
oversized format fields and invalid workbook date-system flags are rejected.
A UI check verifies that partial-preview and missing-result notices appear
together and that unavailable formula results occupy visible cells.

Shortcut checks now cover the visible pre-setup conflict summary, retention of
Dzurkel's own alternate bindings, restoration after unrelated shortcut edits,
and refusal to edit symbolic-link configurations or compete with a setup lock.
Lifecycle checks cover immediate reopening while a dismissed worker is still
running and prompt destruction of a controller with a cooperative worker.
Native decoders that do not cooperate can still delay shutdown; this test does
not establish a hard shutdown deadline for arbitrary inputs.

Validation on 2026-10-17: all ten main-build suites passed (27.40s); after the
final date-rounding/style-processing changes, XLSX passed again (0.72s).
The new notice, conflict-summary, immediate-reopen and cooperative-shutdown
checks also passed with the native Wayland backend. Light offscreen and dark
Wayland screenshots of the notices/settings were visually inspected. These
checks use temporary resources and isolated D-Bus sessions; they do not establish
real Dolphin focus restoration, multi-monitor placement or real-server behavior.
No new sanitizer run was performed for these refinements. See
[the short Debug baseline](performance.md#review-refinement-baseline-2026-10-17)
for the accompanying latency measurement.

An optional private sample check uses `DZURKEL_XLSX_SAMPLE=/absolute/path.xlsx`
with `dzurkel_xlsx_tests inspectUserSample`. It prints counts/timing only and
writes an ignored build-directory screenshot. Do not add personal workbooks
or sample screenshots to Git. A native Wayland run loaded a small ordinary
workbook in 11 ms (parser only), with 180 rows, 11 columns and 1,888 stored cells;
the rendered grid was visually inspected. This is one local sample, not a
performance guarantee for other workbooks or remote locations.

The main build passed all ten suites (28.39s). Real remote XLSX performance and
broader workbook compatibility remain user acceptance checks. XLS/XLSB/XLSM,
formula calculation and exact Excel display formatting remain outside scope.

The XLSX suite passed ASan/UBSan with leak detection (1.72s), using only the
existing narrow Qt PDF suppression.

The optimized Fedora install passed all ten suites (27.86s); the installed
executable was verified against that build. Installed XLSX acceptance is pending
user testing.

## Compact preview UI and image window sizing

The showcase-inspired layout uses the native title bar for filenames, icon
navigation and zoom controls, an information button, and a compact summary
footer. The full path, encoding and delimiter details are accessible through
hover information and a scrollable, keyboard-accessible dialog. Partial previews
and unavailable formula results remain visible; switching sheets updates the
row/column summary. Table sections use compact fixed starting sizes and remain
resizable without scanning the whole workbook.

Regression checks cover escaped filenames/metadata, absence of the old filename
and path header rows, encoding/delimiter text kept out of the footer, non-overlap
of navigation controls, dialog activation with Space and independent Escape
handling, natural image sizing, a centered 40 × 30 image in a 400 × 400 window,
a medium image at 100%, oversized image fitting, manual resizing/zooming,
maximized windows and document-size restoration across image/information views.
The cold small image is checked at its first Show event to catch oversized
initial placement.

All ten Debug suites passed during refinement (29.20s). After the final layout
correction, the focused table/information check passed again. Six native Wayland
cases passed: natural image sizing; temporary information sizing; repeated
information-to-PDF reopening; table details/dialog access; multi-sheet summary
updates; and combined partial/unavailable-result notices. Light offscreen and
dark native Wayland image, table and information screenshots were inspected.
Artifacts and Qt Test logs live in the ignored build directory as
`ui-refinement-*.png` and `ui-refinement-wayland-*.txt`.

Tests use temporary files and isolated buses. Moving between monitors with
mixed scaling, real Dolphin focus restoration and human keyboard interaction
still require desktop acceptance; the automated checks are not a latency study.

The optimized Fedora reinstall passed all ten suites (29.17s). The installed
binary was verified byte-for-byte against the stripped optimized build, and both
desktop entries matched. This installs the compact UI and image-sizing changes.

## Wayland frame-placement regression

The first UI-refinement acceptance checks verified widget sizes and screenshots
of client content; they did not verify the compositor's global frame placement.
User screenshots exposed bottom/right clipping during image resizing. A read-only
KWin geometry probe reproduced a 1,076-pixel-high frame inside a 1,036-pixel work
area and a wide preview extending past the right edge. Qt's global coordinates
and estimated available geometry were insufficient for this check.

`WaylandPlacementTest.py` is an opt-in regression check on a real KDE Wayland
session. It creates generated images, text and an unsupported resource, runs its
own Dzurkel process on a private bus, and uses a temporary KWin observer filtered
to that PID to read actual frame/work-area rectangles. It never calls the user's
resident Dzurkel service or changes persistent KWin configuration. `--activate`
activates only the initial generated window, then checks that later transitions
retain focus continuously. It also requires one native window ID throughout
navigation, no hidden/closed events, and multiple actual sizes, including a burst
of requests. The observer is unloaded and its child processes are stopped.
It uses the public [KWin scripting API](https://develop.kde.org/docs/plasma/kwin/api/)
to observe the application placement adapter. A test-only
`DZURKEL_TEST_KWIN_BUS_ADDRESS` override lets the isolated preview process reach
the desktop compositor without sharing the resident application's service bus.

Run from the checkout on the real desktop bus, without `dbus-run-session`:

```bash
python3 tests/integration/WaylandPlacementTest.py --initial small --activate \
  --report build/wayland-placement-small.json
python3 tests/integration/WaylandPlacementTest.py --initial large --activate \
  --report build/wayland-placement-large.json
```

This optional check requires `python3-dbus` and `python3-gobject`. The ordinary
Qt/CMake suites remain isolated and do not require these Python modules, KWin
scripting access or an active desktop. The Qt image-sizing regression also checks
that automatic placement does not emit dismissal; an explicit hide still does.

Validation of the earlier placement fix: all ten Debug suites passed (30.19s). The
small-first and large-first compositor runs each passed nine actual frame-bound
checks with focus retained, including returns through text and metadata views.
The native Qt checks of manual resizing, non-dismissal and maximized/reopened
PDF restoration also passed (three cases plus setup/cleanup, 4.828s). Unlike the
previous client-only screenshot checks, these compositor reports include window
decorations and the panel-reserved work area. Mixed-DPI layouts and non-default
KWin placement policies remain separate acceptance scenarios.

That earlier workaround remapped the native surface. Final-position/focus
checks passed, but missed temporary focus transfer and taskbar blinking reported
by the user. It has been replaced with in-place compositor geometry updates.
The strengthened probe checks every observed focus change and native identity,
not just the final state. The orientation test also cancels at the metadata
callback and verifies no image was decoded, covering background sizing before
pixel loading.

Final optimized reinstall: all ten suites passed (29.97s), and the installed
binary matched the stripped optimized build byte-for-byte. Small-first and
large-first runs against that installed binary each passed eleven compositor
checks, including rapid navigation and information-to-image-to-document
restoration. Each run retained one native window ID, continuous focus, no
hidden/closed events, and on-screen frames within the panel-reserved work area.

The optimized placement-fix reinstall passed all ten suites (29.99s). Its
432,448-byte installed executable matched the stripped build exactly. The
installed executable then passed all nine small-first compositor checks with
focus retained; results are in `build/wayland-placement-installed.json`. Test
windows, private buses and temporary KWin observers were cleaned up.

## Initial public packaging validation (2026-10-17)

The current checkout passed all ten CTest suites in Debug (29.18s), including
Settings startup without a file, forwarding to the existing service, preservation
of the preview URL, activation-option validation, and the staged four-file
installation. The initial sandbox run could not bind D-Bus sockets; the successful
run used isolated buses outside that restriction. No personal shortcuts were
changed. CI and RPM validation results are recorded in the GitHub workflow runs.

The README table screenshot was captured from the real Qt preview using a
synthetic CSV listing supported formats; it contains no personal files or paths.

## RPM desktop acceptance (2026-10-17)

The user installed `dzurkel-0.6.0-1.fc44.x86_64.rpm` after backing up and
removing the user-local installation. RPM verification passed. The first Settings
launch reached an old resident user-local process, which rejected the new
`ShowSettings` method. After stopping that process, the installed
`/usr/bin/dzurkel` started on the native Wayland backend.

The user confirmed that Settings opened, the Space binding was saved correctly,
and the application worked as expected. This establishes basic packaged desktop
operation and shortcut setup; it does not establish exhaustive format coverage,
long-session shortcut reliability, mixed-DPI behavior, or package upgrades
on the real desktop. Those checks remain separate.

The user subsequently removed the RPM. The package and all four installed
application/menu files were verified absent. The current source checkout was
rebuilt and restored into `~/.local` with the Fedora installer; all ten suites
passed (30.31s). The restored application desktop entries and executable service
menu matched the generated files. Per-user settings and shortcut backups were
retained throughout the switch.

The intermittent loss of Dolphin's live Space binding remains open as
[issue #4](https://github.com/EdinUser/dzurkel/issues/4). Successful setup does not
resolve that issue. See the [documented workaround](dolphin-integration.md#space-stops-working-while-the-context-menu-works).

## Reactivating an already-open preview

`DBusTest::visibleShowRequestsFreshActivation` checks that an explicit Show with
fresh activation context requests activation for an already-visible preview,
reuses the resident process, and does not replay the token after loading. It
failed with the original controller/window visibility guards and passed after
the fix. The ordinary D-Bus suite runs this regression in CI on an isolated bus.

`PreviewTest::visiblePreviewReactivatesOnExplicitShow` is an opt-in test on a
real KDE Wayland session:

```bash
QT_QPA_PLATFORM=wayland DZURKEL_TEST_NATIVE_FOCUS=1 \
  ./build/dzurkel_preview_tests visiblePreviewReactivatesOnExplicitShow
```

It creates its own preview and caller windows, uses a temporary KWin script to
focus only the caller, and obtains fresh activation tokens through the existing
KDE API. The preview must reactivate through the application request, not through
the script. It checks same-file and replacement-file requests, minimized-window
restoration, stable native window identity, no extra dismissal during Show,
Escape dismissal, and focus preservation when asynchronous folder navigation
finishes after switching to the caller. Temporary scripts are unloaded.

The native focus regression passed, as did the existing small-first compositor
placement run: eleven checks retained focus and one native window without
unmapping. Reports are in the ignored `build/` directory. The native regression
is skipped in offscreen CI because a compositor is required. It does not
synthesize actual Dolphin keyboard input; the installed Dolphin Space workflow
still requires user acceptance. Tokenless requests remain subject to compositor
focus policy. This fix does not resolve Dolphin's separate shortcut-loss issue.

All ten suites passed in the optimized Fedora build (29.31s), and the native
focus regression passed against both Debug and optimized builds. The user-local
installation was updated; its executable matched the stripped optimized build
byte-for-byte. The existing draft RPM was not rebuilt as part of this fix.
The user subsequently confirmed the real Dolphin sequence now refocuses the
open preview correctly when Space is pressed after selecting another file.

## Focused visual refinement

The native widget title now contains only the filename; KWin's actual caption
was verified as `small.png — Dzurkel`, removing the duplicated application name.
The compact toolbar, settings icon, native frame, image sizing, zoom, table
model/tabs and preview rendering retain their existing behavior.

Text summaries now include encoding and physical line count from the bounded
decoded payload. Decoder regressions cover CR/CRLF, Unicode line breaks, UTF-8,
UTF-16/32, Latin-1, blank lines and final-newline handling. CSV/TSV summaries
identify rows and columns; XLSX retains sheet counts and active-sheet dimensions.
PDF page counts use normal singular/plural wording.

The information dialog adds an explicit Name field and uses compact two-column
metadata with smaller secondary labels, content-based height and a bounded width.
Long-path tests verify escaping, text height, scroll access, Escape dismissal,
400-pixel preview widths and visible Settings. Offscreen and native Wayland
light/dark captures were visually inspected. Generated screenshots remain in the
ignored `build/` directory; the public README screenshot uses synthetic CSV data.

The native refocus regression and all eleven small-first KWin placement checks
passed after these changes, retaining focus and a single native window. This
does not establish mixed-DPI or every desktop theme's behavior.

The optimized Fedora install passed all ten suites (29.72s). The installed
executable was verified byte-for-byte against the stripped optimized build.
