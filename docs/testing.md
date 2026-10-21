# Validation

## Helper shutdown regression coverage (2026-10-21)

Resource preparation, folder navigation and streamed-media source shutdown now
have a subprocess regression suite using the actual request owners and disposable
helpers:

```bash
ctest --test-dir build -R '^dzurkel_helper_shutdown$' --output-on-failure
```

The 28 cases cover normal completion, failed starts, active/cancelled/destroyed
owners at shutdown, independent budget exhaustion and recovery, and replacement
of a folder request on the same owner. They check detachment during `aboutToQuit`
and cancellation/destruction after the application event loop returns, without
subsequent event processing or process-destruction warnings. A test-only
`LD_PRELOAD` shim suppresses SIGKILL delivery to simulate termination remaining
pending; this is not a helper ignoring SIGKILL or a real kernel-blocked I/O test.
Each probe has an eight-second external timeout. The Linux test supervisor adopts,
kills and reaps disposable surviving helpers; the shim is not installed with the
application.

The focused suite passed three consecutive runs (4.27–4.41 s). The active-owner
fault-injection cases for all three families timed out against the original
implementation, confirming that the regressions detect the shutdown failure.
The full build passed and all 35 configured CTest targets passed (127.06 s).
Opt-in native Wayland and private-fixture checks retain their environment gates;
this run does not establish new desktop acceptance or reproduce actual
uninterruptible filesystem I/O.

## 1.0.0 version and documentation preparation (2026-10-21)

Bumped the source version, RPM Version, Debian changelog and Arch pkgver to
1.0.0, with package release 1. The release-specific desktop adapter ID is also
1.0.0; independently versioned media/highlighting/folder interfaces are unchanged.
The full local build passed and all 34 configured CTest suites passed (123.48 s),
including preview/document/table/media regressions, optional-adapter recovery,
CLI/D-Bus behavior and staged installation. The built executable reports
`dzurkel 1.0.0`; its generated compatibility report and all package source version
fields agree.

README, current guides, architecture, contributor/repository guidance, installer
examples and release instructions now describe the completed direct-source
progressive-preview scope. Added [1.0.0 release notes](releases/v1.0.0.md), corrected
stale copy/prefix/pending-continuation descriptions, and retained accepted reader
budgets, lexer-context limits and the Ubuntu one-pixel EPUB observation. Local
Markdown file/anchor checks and `git diff --check` passed. Historical notes,
recordings and measured results keep the versions they actually describe.

The earlier Fedora sanitizer and Ubuntu/Arch desktop runs below used the 0.9.0
development version. They were not rerun or relabelled as 1.0.0 package/sanitizer
acceptance. This change builds locally but does not install on the host or VMs,
commit, tag, publish a release or run tagged release CI. Release publication,
native package gates and asset upload remain the documented release process.

## Arch XFCE package and native lazy-loading acceptance (2026-10-21)

Ran `helpers/vm-arch-vm-dev.sh --stage-only` against the booted Arch VM.
The existing workflow exported the active uncommitted public sources, built
Dzurkel 0.9.0-1 in a disposable Arch container and passed all 34 configured
CTest suites (92.87 s). The package contains all five optional adapters without
making their backends mandatory dependencies. A fresh runtime container passed
baseline/reader recovery, GIO nested-folder/error/cancellation and missing-module
fallback checks (7, 4 and 3 results respectively) with the optional Qt Multimedia,
KSyntaxHighlighting, KWindowSystem, antiword and KIO/CoreAddons packages absent.
Build/runtime evidence remains under
`build-arch-vm/package.fNBmjo8t/{container-build.log,artifacts/}`.

The tested package was staged in the VM at
`~/Dzurkel-packages/package.fNBmjo8t/`. The installer ran in a VM terminal so the
user could enter sudo authentication there. It completed successfully; pacman
reports `dzurkel 0.9.0-1`, 56 files and none missing. The installed executable and
GIO adapter SHA-256 values match their entries in the tested package. The VM
remains an XFCE/Thunar runtime system; no development SDK was installed there.
Its existing Thunar action already targets `/usr/bin/dzurkel` and was preserved.

With `LockedHint=no`, native X11/XTest input exercised that installed app through
the normal managed Thunar Space action. Generated fixtures live in
`~/Dzurkel-test/lazy-acceptance-20261021/`. Framebuffer screenshots confirm:

- The sparse 3 GiB SQL fixture opened at its beginning; Ctrl+End displayed its
  actual final export-completed marker with `End of file` status. Ctrl+Home
  returned to the beginning with `Beginning of file` status. KSyntaxHighlighting
  is absent in this VM, so this checks readable plain-code fallback, not syntax
  colors. No highlighting backend was installed for the test.
- DOCX scrolled beyond the initial portion to paragraphs 104–108 and back to
  paragraphs 83–87, retaining bold formatting and stable reading dimensions.
- Markdown scrolled beyond the initial portion to sections 105–108, retaining
  headings, bold text, visible links, lists and fenced code text. Links were not
  activated.
- Escape dismissed each preview, confirmed by the live service's `Visible`
  property; Thunar opened the next selected fixture through Space. The final
  Thunar window was left at the generated sample directory.

Three already-built test executables were temporarily copied into the VM and
used the installed `/usr/bin/dzurkel` helper. With Qt 6.12.0, GCC 16.2.1,
`QT_QPA_PLATFORM=xcb`, the unlocked real display and isolated application buses,
the native checks passed: focused text EOF/error/anchor/replacement/dismissal
(7 results, 3.01 s), focused DOCX/RTF/FB2/SFB/MOBI/Markdown/EPUB continuation,
eviction, selection/geometry and portion-local formatting (10 results, 7.24 s),
and the full CSV/XLSX suite (70 passed, one optional private sample skipped,
7.80 s). The exact EPUB anchor assertion passed on Arch; this does not resolve
the separately reproduced one-pixel Ubuntu GNOME mismatch below.

Generated-sample screenshots, live-state observations, native logs and the
desktop-check helper are retained privately under
`helpers/work/arch-lazy-desktop-20261021/`. Private-bus portal/GVfs/RealtimeKit
warnings did not fail these tests and establish no real-share support. These
checks cover this Arch XFCE/X11 snapshot, not Arch KDE/Wayland, mixed-DPI/themes,
all real document variants or network shares. No application code, shortcut,
authorization policy, host installation, branch, worktree or commit was changed
for this validation; existing checkout changes were preserved.

## Ubuntu GNOME VM lazy-loading acceptance (2026-10-21)

Ran the existing private `helpers/vm-ubuntu-vm-dev.sh` workflow against the
running Ubuntu 26.04.1 VM. It synchronized the active checkout, including new
public sources, retained overwritten VM sources in its backup directory, built
the Debug configuration and passed all 31 configured CTest suites (92.43 s).
This is the VM's configured suite set, not the host's 34-suite configuration.
The helper installed Dzurkel 0.9.0 under the VM user's `~/.local`, checked its
four runtime adapters and refreshed the executable Nautilus script. The installed
binary compares identical to the tested build. Required SDKs were already present;
no dependency packages were installed. Desktop-file validation passed with the
existing informational hint about multiple Settings menu categories.

Native checks used GNOME Shell 50.1, Qt 6.10.2 and `QT_QPA_PLATFORM=wayland`
against the existing `wayland-0` display, with isolated application buses. The
GNOME screensaver reported inactive before both runs. The focused text checks
passed (7 results, 2.92 s): sparse 3 GiB SQL EOF, failed EOF retaining content,
wrapped/unwrapped anchor and geometry preservation, replacement and dismissal.
The full native XLSX/CSV suite passed (70 passed, one optional private sample
skipped, 8.93 s).

The focused rich-document run had 9 passes and one failure (20.07 s). DOCX,
RTF, FB2, SFB, MOBI and Markdown view continuation/eviction checks passed, as did
EPUB portion-local formatting. `epubWindowEvictsAndNavigatesWithStableGeometry`
failed its exact cursor-top comparison at DocumentTest.cpp:1583: actual 33 px,
expected 32 px. An isolated rerun reproduced the same one-pixel mismatch (2
passes including setup/cleanup, one failure, 15.78 s). Its later assertions were
therefore not reached. The cause has not been established; the test assertion
and application code were not changed to conceal it. Native Ubuntu document
acceptance is not entirely green, despite the complete offscreen pass.

Private-bus portal/app-ID and GVfs warnings were present; they are not evidence
of real remote-share support. These automated runs do not establish manual
Nautilus Space/menu delivery, compositor focus, themes/scaling or packaged
installation acceptance for this update. The VM development installation is
ready for those manual checks. Arch was not changed.

### Installed-app GNOME desktop smoke checks

Subsequently exercised the installed app on the unlocked VM through real
libvirt keyboard input and inspected VM framebuffer screenshots, rather than
Qt-generated key events or an offscreen backend. Files selected each generated
local fixture and Space opened the expected URI and preview kind, as confirmed
by the live application's D-Bus properties and displayed content.

- Sparse 3 GiB SQL: the beginning displayed highlighted SQL; Ctrl+End displayed
  the actual final `EXPORT COMPLETED SUCCESSFULLY; ACTUAL END OF FILE` marker and
  `End of file` status. Page Up followed by Ctrl+Home returned to the beginning
  with `Beginning of file` status. The window dimensions stayed visually stable.
- DOCX: twenty Page Down presses reached paragraph 108 beyond the initial
  portion; four Page Up presses returned to paragraph 87. Bold text remained
  visible and the reading window stayed stable.
- Markdown: twenty-five Page Down presses reached sections 98–100 beyond the
  initial portion. Headings, bold text, link appearance, list items and fenced
  code text remained visible. Links were not activated.
- Escape dismissed each preview and the live service reported `Visible=false`
  and `PreviewKind=none`; Files could open the next fixture through Space.

Generated samples remain in the VM's
`~/Dzurkel-test/lazy-acceptance-20261021/`; screenshots were retained privately
under `helpers/work/ubuntu-lazy-desktop-20261021/`. The final Files window was
left at that sample directory. No GNOME setting, authorization policy, app code
or dependency package was changed. Initial per-action VM connections caused
repeated host libvirt authorization prompts; the remaining checks were batched
under one connection. These selected live desktop checks do not resolve the
repeatable one-pixel EPUB assertion above or establish every format's desktop
appearance, scaling/themes, menu invocation or real network-share behavior.

## Agreed preview scope and practical text EOF (2026-10-21)

The completion agreement keeps lazy loading and direct sources and accepts reader
work/metadata/indivisible-block limits. Dzurkel provides a useful peek and ordinary
scrolling. Unlimited distant traversal, a full-file index, exhaustive visual search
and perfect lexer-state recovery after eviction are not required. The language
selection and bounded document style metadata remain attached for the preview's
lifetime; lost multiline highlighting context is an accepted fidelity limitation.
Comparative benchmarking and broader real-share/distribution testing remain
optional follow-ups, without being claimed complete here.

Text/log/code/SQL previews now expose Beginning/End buttons and Ctrl+Home/Ctrl+End.
The existing TextPortionReader resolves a reader-owned EOF token using TextSource's
bounded QFile seeks and Qt Unicode decoding. It selects the last useful interval
when line-boundary lookahead would otherwise produce an empty EOF portion. No
intervening-content scan, growing index or whole-source copy is needed. The shared
coordinator handles cancellation/stale targets and the shared window replaces only
resident text when a jump succeeds. The editor, highlighter/language, zoom and
native window persist; explicit jumps reset selection. Status confirms Beginning
of file/End of file. Ordinary backward scrolling works from EOF. Failed jumps
retain useful text and a clear error. Structured readers keep best-effort logical
navigation within their existing budgets.

The sparse 3 GiB SQL integration fixture contains valid beginning/end regions and
binary zeroes in the middle. It verifies that opening/jumping does not read through
that middle; the actual final text without a terminating newline is displayed.
It checks keyboard/buttons, returning to the beginning, backward scrolling,
resident-text bounds, zoom/window/highlighter identity and superseded jumps.
Other cases cover UTF-8/16/32 and Latin-1, CRLF, empty/small files, exact intervals,
empty final nominal portions and encoding damage at EOF retaining visible content.

That fixture exposed an existing image-selection issue: a local public-Qt probe
measured ~1.7 GiB read by QImageReader::canRead() while installed image plugins
rejected the 3 GiB SQL file (~4.9 s); MIME detection itself was fast. Image selection
now reuses QImageReader on a 16 KiB header QBuffer. Known-image MIME selection and
direct-source image decoding remain. A counting-QFile test checks at most 32 KiB
of source reads (allowing Qt buffering) and PNG signature precedence over a .sql
suffix. This is bounded signature probing, not a replacement image parser.
The initial EOF test failed its five-second opening wait before this fix; it
passes without raising that wait after the probe is bounded.

The full build and all 34 registered suites passed (137.98 s), including PDF,
images, media, documents, tables, highlighting and integration/installation checks.
All five affected ASan/UBSan suites (text lazy, CSV lazy, library, documents, XLSX)
passed with leak detection (85.69 s), using only the existing Qt PDF suppression
and producing no sanitizer diagnostics. Both focused text/library suites also
passed separately (7.66 s). With the physical screen unlocked, four focused methods passed on native KDE Wayland
(7 results including wrap data/setup/cleanup, 3.25 s), using the existing isolated
application bus/real-KWin override. They exercise EOF, failed EOF, ordinary
forward/backward eviction, geometry and session replacement/dismissal. The full
physical KDE Wayland XLSX/CSV suite also passed (70 passed, one optional private
sample skipped, 11.45 s), including text/PDF/table transitions and resize/reopen
checks with the new toolbar. `git diff --check` passed.
This completes the agreed preview scope; the accepted best-effort limits above
are not unfinished unlimited-reader requirements.
No packages, branches, worktrees or commits were created, and accumulated changes
were preserved. No lexer checkpoint engine or unlimited structured-reader work
was added.

## Progressive DOCX, DOC, RTF, FB2, SFB, MOBI and Markdown (2026-10-21)

All seven production readers now fetch source content beyond their initial
preview, using the existing LazyCoordinator/window/document-record/rich-frame
path. DOCX shares the extracted XLSX libzip QIODevice and existing WordReader
styles, numbering, tables and images. Qt owns XML parsing. RTF retains its
existing parser state; FB2 streams section boundaries and bounded subtrees; SFB
retains line/heading state; MOBI retains PalmDOC/encoding/markup carry. DOC reuses
antiword process setup and the inherited descriptor, with a bounded stdout pipe
for idle backpressure. Markdown uses Qt's HTML-disabled importer for rendering,
structural boundary checks and bounded reference discovery. Existing Markdown
table normalization is shared between compatibility and progressive callers.
No parser/rendering dependency, package installation, source copy, branch,
worktree, commit or connector/geometry replacement was added.

New tests reconstruct DOCX beyond both 200,000 displayed characters and 8 MiB
main-part XML, retaining inherited bold and list numbering across portions and
backward replay. An attribute-heavy DOCX fixture also checks portion boundaries
by XML cost when visible text is small. Other readers reconstruct all 1,600 paragraphs beyond the old
text limit and replay earlier portions exactly. Tests cover retained descriptors
after unlink, source mutation, malformed trailing XML, denied DTDs, multibyte UTF-8
across RTF drains/MOBI records, and Markdown fences, nested list content, tables
and forward reference definitions. The synthetic antiword wrapper verifies the
inherited descriptor, emits more than the old 1 MiB cutoff, fills the pipe while
idle, supports replay, and runs through actual controller/view eviction and
helper cancellation. This proves the integration contract, not antiword's own
allocation behavior on arbitrary real DOC files.

Actual views browse forward and backward with three-frame/content bounds,
nonempty retained text selections and stable geometry. Existing EPUB long-window
format-history refresh, selection/viewport anchoring, portion-local formatting,
images, typography/list/theme and compatibility-helper tests remain included.
A late FB2 failure leaves useful document content visible with an explicit state.

The full build passed. All 34 suites passed in the clean rerun (134.74 s).
A preliminary broad run overlapped executable relinking and had helper-startup
failures; it is superseded by that build-complete run. The standalone document
suite also passed (23.19 s).
Document/text/CSV/XLSX ASan/UBSan suites passed with leak detection (85.08 s), using
only the existing Qt PDF suppression and producing no sanitizer diagnostics.
After sharing Markdown normalization/title handling, the document sanitizer
suite passed again (50.35 s). A final DOCX XML-cost bound prevents attribute-heavy
or invisible markup from accumulating 64 expensive blocks despite little text.
After that change, the full build and all four affected suites passed (50.30 s;
documents 29.74 s), and the document sanitizer suite passed again (49.27 s).
The other three sanitizer suites were not rerun for those document-only changes.
The 34-suite run predates this final DOCX cost-bound adjustment.

With ScreenSaver.GetActive returning false, ten focused methods passed on the
physical KDE Wayland desktop (20 results including data rows/setup/cleanup,
15.53 s), including the final DOCX cost-bound adjustment, using an isolated application bus and the existing real-KWin override.
They cover DOCX continuation/numbering, every other new reader, backward eviction,
text selection, window geometry, late errors, Markdown structures/references,
DOC backpressure/cancellation, and existing EPUB/typography behavior. Portal and
image-plugin diagnostics did not fail these checks. This is generated-fixture
native validation; physical scrolling feel and real shares/distribution behavior
are not established. Earlier geometry frame/focus probes remain separate evidence.

Remaining limits are explicit: eager DOCX metadata, bounded XML subtrees,
indivisible paragraphs/tables/lists/fences, MOBI variants/record metadata,
reference discovery and distant replay can exceed reader budgets. Markdown
reference discovery can delay initial content and is limited to 2,048 definitions/
64 KiB with the request's read/time budget. The stdout pipe does not bound
antiword's own internal allocations. These are application budgets, not an OS
memory sandbox. Comparative first-paint/RSS benchmarks and real remote acceptance
remain outstanding. KSyntaxHighlighting can still lose multiline lexer context
when its opening construct is evicted; this increment does not solve that issue.

## Formatting only the newly loaded portion (2026-10-21)

Rich-document continuation now applies typography and theme-safe source colors
to the inserted QTextFrame only, using the current zoom and palette. Retained
frames are not passed through those formatting loops on each load. Explicit
zoom/theme changes still update the resident document. The existing occasional
format-history compaction remains bounded to resident portions and formats each
reimported frame through this same path.

The new real-helper EPUB test watches QTextDocument::contentsChange and confirms
that loading another portion does not send text/format changes through the
resident heading. It checks that the old frame survives, the new headings/body
use the current zoom, and an explicit zoom reset still updates retained text.
The existing long EPUB test continues through forward/backward eviction and the
format-history refresh, checking selection, visible position, images, lists,
tables and unchanged window geometry. Existing DOCX typography/source-color
checks still pass, including light/dark palette restoration.

The existing KSyntaxHighlighting adapter already updates affected Qt text blocks.
A new test compares append/prepend highlight ranges with a fresh library result,
including a multiline comment spanning inserted segments, and confirms that the
same adapter and earlier block's lexer data survive appends. No lexer or second
formatting backend was added. This does not resolve lexer state whose opening
construct has already been evicted; that remains explicitly outstanding.

The full build passed. All five affected CTest suites passed (23.26 s): documents,
highlighting, optional highlighting fallback, lazy text and CSV. Three focused
native KDE Wayland document methods passed (5 results including setup/cleanup,
3.85 s): segment-local formatting, long rich-window eviction/refresh, and
DOCX typography/list/theme behavior. `git diff --check` passed. No packages,
branches, worktrees or commits were created.

## Window geometry completion for progressive previews (2026-10-21)

Stable first opening, switching and restored manual sizes are requirements of
the lazy-loading work. The existing WindowPlacement helper now tolerates a
filename caption still travelling over Wayland: it prefers an exact caption,
otherwise selects only the sole eligible normal window of this process. Dialogs,
hidden/deleted windows, other processes and ambiguous candidates remain excluded.
Qt visibility precedes native exposure, so first-map requests retain the latest
target until Qt has dispatched the expose event. Later mapped requests continue
to use KWin even when Qt temporarily reports the window as unexposed. No second
placement service, compositor replacement or remapping path was introduced.

A new regression commits reading content synchronously after showing a compact
loader, before native exposure, then changes caption and information/reading
geometry six times. It checks the final reading size and absence of remapping.
The existing CSV/XLSX checks cover column fitting, text/PDF transitions, restored
760×540 manual table size and hide/reopen. Existing image/maximized transitions
and the read-only KWin frame observer provide broader placement coverage.

The rebuilt checkout passed all 34 registered suites (115.30 s). An isolated
session of the already installed KWin, using its `--virtual` framebuffer at
1920×1080, a private D-Bus session and temporary runtime/config directories,
passed the complete XLSX/CSV suite (70 passed, one optional private sample
skipped, 8.61 s), eight focused preview methods (10 results, 8.91 s), and all
11 existing compositor placement checks with `--initial small --activate`.
The observer confirms frame containment, one native window, continuous focus,
no unmapping, restored reading size and rapid replacement. Its report is
`build/wayland-placement-geometry-small.json`.

The physical desktop became locked during validation: ScreenSaver.GetActive
returned true, and native windows stopped becoming exposed. Runs in that state
are not valid desktop acceptance results. After the user unlocked the desktop,
ScreenSaver.GetActive returned false and the complete physical KDE Wayland
XLSX/CSV suite passed (70 passed, one optional sample skipped, 9.29 s). The eight
focused preview methods also passed (10 results, 11.71 s), including the new
first-exposure/caption regression, cold loader and PDF opens, pending-preview
identity, and manual/maximized image/table transitions. The physical KWin frame
observer passed both `--initial small --activate` and `--initial large --activate`
(11 checks each): all frames stayed inside the actual 1920×1036 work area,
keyboard focus and a single native window were retained, no unmapping occurred,
and reading size was restored after temporary previews. Reports are
`build/wayland-placement-geometry-physical-small.json` and
`build/wayland-placement-geometry-physical-large.json`. This closes the recorded
cold/open/transition/reopen geometry follow-up. No desktop lock settings
were changed. No new packages, branches, worktrees or commits were made.

## Progressive EPUB on existing document/helper infrastructure (2026-10-21)

EPUB now reuses Package/SeekableZipSource, the existing spine/HTML/image renderer,
LazyCoordinator/LazyWindow/boundary/status policy, and the document-record writer
and StructuredDecodeJob validator. No new ZIP/XML parser, transport, image codec,
extractor or dependency is introduced. The installed Qt 6.11.2 public text-frame,
resource, cursor and layout APIs supply the rich view; libzip remains 1.11.4.
The helper uses the existing headless document runtime for image plugins.

Initial/continuing portions target 8,192 text units or 64 semantic units, retaining
complete inline/list/table content and transparent container boundaries. Only
one bounded chapter tree and next block cursor are retained; older cache misses
replay the same renderer. The existing 8 MiB XML-part/200,000-node/depth budgets
remain, as do per-portion content and image limits. Books can continue beyond the
old total 200,000 characters and 128 chapters; spine metadata is capped at 2,048.
An excessive indivisible block fails its content budget instead of silently
truncating a supposedly complete continuation. Images have a 2 Mi-pixel portion
budget and use the existing validated native pixel/profile transfer.

The rich view retains three Qt frames, the existing five-entry/64 MiB charged
cache and four reusable resource slots. A public-API probe demonstrated that Qt
retains unique formats after text deletion (1,004 formats for five characters
after 1,000 anchor edits); clear reduced this to two. Above a 4,096-format refresh
trigger, only the resident sanitized portions are reimported through Qt's clear
API. The widget, zoom, visible text position, selection, and window are retained.
The trigger does not cap formats actually needed by resident content.

Tests reconstruct a long chapter past the old text budget, read 140 spine entries,
replay evicted blocks, jump to unloaded fragments (including >1,024-character
existing anchor names), and check source unlink/mutation and late malformed XML.
The actual view browses through forward/backward eviction and format refresh,
checking character/format/frame bounds, selection, a visible text anchor,
window geometry, bold text, nested tables/lists and validated image resources.
Broken links retain useful content and later valid navigation still works.
Existing document-helper tests continue to validate record framing, images,
read-only resource behavior, other formats, extractors and cancellation.

The build passed all 34 registered suites (122.95 s). Document, plain-text,
CSV and XLSX suites passed ASan/UBSan with leak detection (64.96 s), using only
the existing Qt PDF suppression and producing no sanitizer diagnostics. Six
focused document cases passed native KDE Wayland with the real KWin bus override
(8 results including setup/cleanup, 4.02 s): new EPUB continuation/replay and
rich-window checks, resource denial, typography/list behavior, and shared image
transfer. The EPUB window test crossed the format-refresh threshold while
preserving a nonempty text selection and the visible line through 24 forward
loads, backward reloads and chapter navigation.
At this checkpoint the cold-text/CSV native geometry follow-up remained open;
see the later geometry completion above for its fix and validation. This EPUB
increment does not claim complete phase acceptance or measured RSS/first-paint improvement.
Other document formats remain bounded and need their own reader continuation.
No branch/worktree, installation or commit is part of this increment.

## Progressive XLSX worksheets on the shared table path (2026-10-21)

Production XLSX now uses the existing LazyCoordinator/helper record protocol,
TableRowBatch, LazyWindow and LazyTableWindow/SpreadsheetModel path used by CSV.
The existing package/cell/number-format parser is shared with bounded callers.
The capability assessment used installed Qt 6.11.2 and libzip 1.11.4: Qt accepts
an input QIODevice, while libzip's zip_fseek manual restricts seeking to stored,
unencrypted entries. A small read-only device wraps libzip entry reads; one Qt
XML cursor serves forward batches, and older cache misses use bounded replay.
No new ZIP/XML parser, extractor, dependency, source copy or growing index exists.

Initial loads target 64 rows/16,384 characters; three rendered batches (at most
192 rows), five charged cache entries/64 MiB, and the existing 16 MiB logical
reply/128 KiB record bounds apply. Unselected worksheets stay unloaded. Switching
releases the old row window and remembers one reader-owned region token per
sheet. The footer reports resident source rows. Insertion/eviction retains the
visible row anchor, persistent selection, widths and window geometry using Qt.

New XLSX regressions browse 11,000 rows/110,000 cells and more than 16 MiB of
worksheet XML/8 Mi retained-value characters over time, then replay an evicted
older batch. Shared strings, rich text, wide Unicode chunked replies, booleans,
missing formula caches, source unlink/mutation, sparse rows, trailing empty cells,
late errors, sheet switching and stale targets are covered. All existing date/time
cases also run through the progressive reader for transitional and Strict OOXML.
Qt's model tester checks the real grid during eviction and sheet changes.

Metadata/styles/shared strings still use bounded eager parsing. Distant compressed
entry replay can exceed its per-request work budget; exact cross-sheet pixel
restoration and jumps over huge sparse ranges remain open. Document/book
continuation and evicted text highlighting context remain separate increments.
Physical scrolling feel, real shares, other distributions, and comparative
first-paint/peak-memory benchmarks are not claimed by these generated tests.

The all-adapters build passed all 34 registered suites (121.90 s). After
rebuilding all PreviewData consumers to remove stale development objects, XLSX
and both shared lazy suites passed ASan/UBSan with leak detection (32.07 s),
using only the existing Qt PDF suppression and producing no sanitizer diagnostics.
The rebuilt normal XLSX/CSV/text continuation suites passed again (18.24 s).
Native KDE Wayland with the real KWin bus override passed the four new XLSX
checks (6 results including setup/cleanup, 2.08 s), all 30 CSV lazy results
(2.79 s), and all 20 text lazy results (6.07 s).

At this checkpoint the complete native XLSX suite was **not fully green** (see
the later geometry completion above for current results): its CSV
`tablesKeepGeometryIndependentOfReadingAndPdf` case failed at differing cold-text,
transition or reopen size assertions; `tableWidthFollowsColumns:csv-40` also
failed once. A sequential rerun without competing jobs reproduced failures,
and an isolated run failed on the initial text aspect-ratio assertion before
opening a table. The new XLSX row/sheet geometry checks passed throughout.
These failures were recorded as unresolved desktop geometry follow-up; their
differing failure stages alone did not establish a root cause.
No native geometry implementation was changed in this increment.

No installation, commit, branch or worktree was created. Accumulated checkout
changes were preserved.

## Shared text/table reuse and progressive CSV/TSV (2026-10-20)

The OS/library-first requirement is explicit in `docs/architecture.md` and the
active task note. Text and CSV share `TextCodec` (Qt conversion/validation),
`TextSource` (validated QFile/OS seek access and mutation checks), `LazyWindow`,
`LazyBoundaryWatcher`, and loading/error binding. The existing CSV parser was
extracted as a stateful `CsvRecordParser` and retained for bounded callers.
The existing record writer handles larger logical replies in bounded chunks.
`TableRowBatch` plus the existing `SpreadsheetModel`/QTableView use Qt row
notifications and persistent indexes, ready for future XLSX continuation.

Production CSV/TSV now opens a direct persistent source, initially loads at most
64 complete rows with a soft 16,384-character target, and browses adjacent batches
in both directions. Three rendered batches (at most 192 rows), a five-entry/
64 MiB charged cache, 16 MiB logical replies, 128 KiB wire records, and existing
field/column/work budgets bound retained state. Reverse reads use bounded OS
seeks and quote parity from known record boundaries; no growing index or source
copy is created. Initial malformed input falls back to progressive text through
the already-open source; later errors retain useful content. A resident-row range
replaces a misleading initial-only row count during long browsing.

`dzurkel_csv_lazy` checks all splits of a quoted/escaped/multiline CRLF fixture,
then forward and backward reconstruction for six encodings and three separators.
It reads 18,000 rows beyond the former 2 MiB and 10,000-row cutoffs, transfers wide
Unicode records through the shared chunk framing, revisits records after source
unlink, and checks malformed initial/later records and fallback. A Qt
`QAbstractItemModelTester` checks the actual model through eviction; persistent
selection, visible row anchors, horizontal scrolling, manual column widths,
source row labels, resident row count and window geometry are preserved through
forward browsing and return to the beginning. Invalid multipart sizes, offsets,
base64, truncation and unterminated maximum-size wire records are rejected.
Resource tests retain simulated SMB/SFTP identity and now assert uncopied direct
CSV/TSV handoff. Existing plain-text tests exercise the extracted common policy.

The all-adapters build passed all 34 registered suites (106.60 s). Native KDE
Wayland passed all 30 CSV results (3.01 s) and all 20 text results (5.92 s), using
the actual KWin geometry path. Both lazy suites passed ASan/UBSan with leak
detection (15.11 s), with no new suppression or sanitizer diagnostics. Physical
mouse/trackpad feel, real remote-share acceptance, other distributions and measured
first-paint/peak-memory comparisons remain separate. No installation, commit,
branch or worktree was created; accumulated checkout changes were preserved.

The overall phase still needs XLSX worksheet streaming, document/book
continuation and syntax-highlighting context across evicted text. Their existing
bounded readers remain in place; the shared table/window infrastructure does not
make their parsers incremental by itself. PDF, media, images and adapters retain
their existing behavior.

## Shared lazy coordination and progressive plain text (2026-10-20)

This is the first increment of the shared lazy-loading phase. Production plain
text now opens a persistent direct-source helper and loads nominal 4 KiB
portions. Near-boundary scrolling loads adjacent portions; the view retains three
and can revisit evicted content in either direction. The coordinator owns
request IDs/generations, deduplication, one coalesced foreground target, one
lookahead, a five-entry/384 KiB charged cache, deadlines and non-waiting
cancellation. No lifetime content cutoff or growing offset index is introduced.

`dzurkel_lazy` checks UTF-8, UTF-16LE/BE, UTF-32LE/BE and Latin-1 reconstruction,
CRLF/Unicode boundaries, long lines, interior BOMs, backward reads, source
mutation and binary rejection. Helper/controller coverage includes reads beyond
the former 2 MiB cutoff, backward cache misses, unlinking an open source, bounded
cache state, duplicate/pending/stale requests, wrong IDs, cyclic/oversized/
truncated replies, timeouts and cancellation without waiting. Wrapped and
unwrapped views browse forward through eviction and back to the beginning while
checking a live text anchor, resident content and window geometry. A later source
error preserves useful text and geometry; replacement/dismissal release helpers.
Native viewport checks wait for initial exposure/configure before measuring.

The all-adapters build passed all 33 registered suites (105.69 s). After the
final fixed-size status/error presentation refinement, all five affected suites
(highlighting, previews, lazy loading, resources and interactions) passed again
(39.40 s). The final lazy suite passed ASan/UBSan with leak detection (7.21 s),
without a new suppression or sanitizer diagnostics. Its full native KDE Wayland
run passed all 20 results (6.15 s), including the real KWin geometry path.
These are synthetic Qt input and generated local fixtures; physical scroll feel,
real remote shares, other distributions and comparative first-paint/peak-memory
benchmarks remain separate acceptance work. Nothing was installed or committed.

The overall lazy-loading phase remains incomplete: CSV/XLSX and formatted
text/document/book readers still return their bounded initial previews and do
not fetch further content while scrolling. Their parser-specific continuation
work is recorded in `helpers/features/preview-resource-pipeline.md`. The existing
optional highlighting adapter is unchanged; lexer state spanning evicted text is
not yet checkpointed. Selection/copy/search covers resident text. PDF, media,
images and connectors/adapters retain their existing behavior.

## Shared document and book source access (2026-10-20)

DOCX, EPUB, RTF, FB2, SFB, MOBI, Markdown and legacy DOC now share the structured
helper lifecycle with XLSX. DOCX/EPUB reuse its public libzip seekable-source
bridge; sequential readers retain bounded beginning-of-file content. MOBI seeks
only required records. Optional antiword reads the validated descriptor without
a temporary source copy. Preparation no longer creates source snapshots.

Tests cover every document format through local and simulated SMB/opt-in SFTP
resolution, sparse sources over 256 MiB, unused ZIP parts, bounded prefixes,
Markdown text fallback, embedded image pixels/colour profiles, malformed helper
records, missing antiword and cancellation of extractor descendants. Existing
parser, document navigation and XLSX checks remain in place.

The all-adapters build and all 32 registered suites passed (126.99 s). Document
and XLSX suites passed ASan/UBSan with leak detection (35.24 s), with no sanitizer
diagnostics in the test log. The rebuilt native KDE Wayland selection passed all
32 results (12.37 s), including the actual KWin geometry path and extractor
cancellation. Fixtures simulate remote resolution; real-share and other-distro
acceptance remain separate. No installation or connector/adapter changes were
performed. Bounded preview content remains intentional; this does not introduce
on-demand navigation through complete books or documents.

## Direct-source XLSX (2026-10-20)

XLSX now opens the resolved source through libzip's public seekable-source API
inside an isolated helper. The resource/decoder worker hands off a candidate
without reopening it. The XLSX suite covers a valid sparse ZIP larger than
256 MiB with its central directory beyond that gap, no backing-store copy,
vanished-source fallback, ignored unused expanded parts, archive metadata and
consumed-part expansion budgets, and a multi-megabyte Unicode cell reply.
Malformed/oversized/truncated protocol records, duplicate/out-of-range cells,
timeouts, non-waiting cancellation and the two-helper cap are checked.
Existing saved-value/date/formula-marker, partial-table, sheet, geometry and
horizontal-scroll tests continue through the helper/controller path. Local,
simulated SMB and opt-in SFTP routes preserve the original URL and shared grid.

The all-adapters build passed all 32 registered suites (105.89 s). The full XLSX
target passed ASan/UBSan with leak detection (12.24 s); no new suppression was
needed. The native KDE Wayland run through the actual KWin geometry path passed
15 results, including sparse-source loading, large cell transfer, local/remote
handoff, column-based widths and manual geometry restoration. These use generated
fixtures and simulated remote resolution; real-share/other-distribution acceptance
remains separate. No installation or connector/adapter changes were performed.

## Stable replacement and table widths (2026-10-20)

Mapped previews now retain their content, geometry and filename until the
replacement payload is ready. Image header/metadata callbacks no longer resize
the window ahead of decoded pixels. Painting is suspended during the paired
widget/identity/geometry commit. A slow first open maps a compact loader after
150 ms; cancellation and generation guards still prevent stale presentation.

New table widths use column counts and fixed header/style metrics, with a
400-pixel minimum and 85%-of-work-area maximum. Overflow scrolls horizontally.
Manual height survives opening another table; returning through reading previews
retains the current table's manually chosen size. Sheet changes do not refit.
Tests cover delayed replacement without content/title/size changes, compact first
loading, CSV/XLSX with 2/6/40 columns, horizontal overflow and manual-height
preservation. All 32 registered suites passed across focused and remaining
regression runs. Native Wayland checks passed both ordinary Qt resizing and
the actual KWin geometry path: five preview results and ten table results on
the latter. The final table-height follow-up passed the full spreadsheet suite
again (4.73 s). No installation was performed.

## In-memory text and CSV prefixes (2026-10-20)

The resource suite now checks uncopied text/CSV/TSV prefixes through simulated
SMB and opt-in SFTP KIO-FUSE resolution, preserving original URLs and rendering
through the controller. Sparse 256 MiB text and CSV sources produce at most
2 MiB plus one detection byte without a backing file. A CSV whose final record
crosses the prefix boundary retains only complete records. Source removal after
preparation still permits decoding, proving the worker does not reopen it.
Empty/UTF-8/UTF-16/Latin-1 text, binary rejection, malformed-CSV text fallback and
cancelled jobs are covered. The focused native KDE Wayland run passed all 11
results. All 32 registered suites passed across the five focused suites (30.39 s)
and the 27 remaining regressions (53.47 s).
These are bounded beginning-of-file previews; scrolling does not fetch later
source content. Markdown retains its existing temporary prefix snapshot.
No connector changes or installation were performed.

## Independent spreadsheet window sizing (2026-10-20)

CSV and Excel grids now retain a separate size/state, initially 85% of the
available screen in each dimension. Fitting a portrait PDF cannot overwrite
their saved width. The data-driven
`tablesKeepGeometryIndependentOfReadingAndPdf` regression opens text, a portrait
PDF and a real CSV/XLSX, verifies the wide initial grid, then checks manual-size
restoration after both reading formats and hide/reopen. Both format cases passed
on native KDE Wayland. Preview, interaction, spreadsheet and document suites
passed; the document test now verifies separate PDF/table sizes instead of the
previous shared size. This change has not been installed.

## Direct-source images and GIFs (2026-10-20)

`dzurkel_images` covers a valid sparse BMP larger than 128 MiB whose pixels start
beyond the former source limit, no backing-store copy, vanished-source handoff,
16-bit PNG alpha/Display-P3 profile round trips, JPEG EXIF orientation, a font-using
SVG reader when available, GIF timing/loops and budget fallbacks. It also checks
oversized/invalid/truncated replies, a stalled helper, deadlines and concurrency.
Existing preview/interaction suites exercise first-map dimensions, zoom, fit,
GIF Pause/Play/Replay, replacement and dismissal through the new helper path.
Remote-resource tests assert direct image/PDF paths and original URL identity
through simulated SMB and opt-in SFTP KIO-FUSE responses. No new connector is used.

The all-adapters build passed all 32 registered suites (83.19 s). The image
target passed ASan/UBSan with leak detection and all 12 native KDE Wayland test
results. Native small-image fitting, zoom/scroll/fit/reset and GIF playback checks
also passed. The native window-sizing regression passed twice in isolation; one
earlier run alongside another native window test failed its manual-resize check.
Run native geometry checks serially and retain that concurrent timing limitation.
These checks use the build-tree executable; no installation was performed.

## PDF pages and helper lifecycle (2026-10-20)

`dzurkel_pdf` checks first-page publication, requested-page rendering, mixed page
sizes inside a fixed window, edge scrolling in both directions, fully visible
pages, PageUp/PageDown, Ctrl+wheel, touchpad momentum and phase-less fine-wheel
idle boundaries. It also checks internal links, malformed/oversized/truncated
pipe replies, deadlines, cancellation without a waiting destructor, staged-file
retention, a two-helper cap, and coalescing obsolete page requests.

A new regression opens a valid sparse PDF larger than 256 MiB with 10,001 pages,
checks that preparation returns the original path with no backing store, and
renders both its first and last page. Resource tests check uncopied PDF paths,
original URL identity and successful rendering through simulated SMB/SFTP
KIO-FUSE resolution; other formats retain their existing snapshot behavior.

The navigation/lifecycle checks pass offscreen and on native KDE Wayland with synthetic
input. The offscreen suite also passed three consecutive runs. Mouse tests use
an explicit device identity, since Wayland's primary device can be a touchpad.
The PDF target passed ASan/UBSan with leak detection; this run needs no new
sanitizer suppression. Existing preview tests retain encrypted/damaged
PDF fallbacks, dismissal, repeated replacement, window-state restoration and
session release. The interaction suite checks zoom, visible raster content and
file-navigation shortcuts.

```bash
ctest --test-dir build --output-on-failure -R 'dzurkel_(pdf|previews|interactions)'
QT_QPA_PLATFORM=wayland dbus-run-session -- ./build/dzurkel_pdf_tests
```

Physical mouse/trackpad feel, real remote shares and other desktop/distribution
combinations still need acceptance. Direct PDF sources have no whole-file snapshot
or application byte/page-count cap. These checks do not establish decoder sandboxing, lower total
memory, or faster first paint. Historical retained-PDFium measurements below
precede this implementation.

## Document and e-book checks

`dzurkel_documents` covers DOCX structure and text fallback, EPUB spine order and
sanitization, old XHTML document types, font obfuscation versus encryption,
RTF Unicode/code pages and skipped destinations, bounded image loading,
FB2 structure/metadata and XML rejection, UTF-8 SFB with unknown-marker fallback,
MOBI PalmDOC literals/backreferences/trailers and unsupported/encrypted variants,
malformed input, resource limits, cancellation, missing DOC extraction support,
read-only rendering and resource-loading denial. Controller checks cover local
and simulated SMB preparation, zoom, Space dismissal and view release.

Private samples are never committed. To run the optional sample sweep locally:

```bash
QT_QPA_PLATFORM=offscreen DZURKEL_DOCUMENT_SAMPLES=/path/to/private/samples \
  dbus-run-session -- ./build/dzurkel_document_tests optionalPrivateSamples
```

Multiple directories use `:` separators. Logs report aggregate counts/timing,
without document text or metadata. This checks extraction and view construction;
it does not establish exact Word layout or full EPUB reading-system compliance.
Set `DZURKEL_DOCUMENT_SCREENSHOTS=1` to save FB2/SFB/MOBI view captures in the
test executable directory for local visual inspection. Captures may contain
private sample text and must not be committed.

Validation on 2026-10-19: all 18 configured suites passed (43.44 s). The
document suite passed ASan/UBSan with leak detection and the existing Qt PDF
suppression, including 78 private samples: 74 EPUB, two DOC and two DOCX files.
All produced readable content; 57 intentionally stopped at preview limits.
The sample sweep under sanitizers took 14.2 s including view construction;
this is not a per-file latency or process-memory benchmark. A native KDE
Wayland smoke run passed read-only rendering, local/simulated SMB loading,
zoom, dismissal and widget-release checks. Synthetic light/dark screenshots
were visually inspected. Broader focus, scaling and desktop acceptance across
other environments remains separate.

Follow-up coverage adds rendered title/heading hierarchy, hanging list nesting,
source-color contrast and palette changes, Markdown structure/code/tables and
resource denial, conservative table-size fallback, compact DOC footer warnings,
and content zoom without geometry changes. Reading/PDF/table sizes are checked
independently through audio, small/large images and hide/reopen. Existing
table/image and maximized PDF restoration checks remain in the preview suite;
their initial provider now matches the geometry group under test.

The optimized follow-up build passed all 18 suites (49.69 s). ASan/UBSan with
leak detection passed the complete document suite and all 78 private samples
(20 Qt Test results passed; 57 samples intentionally partial). Native KDE
Wayland checks passed the refined views, profile transitions and existing
maximized PDF/table restoration checks. The local installation was updated;
installed DOCX/DOC/EPUB/RTF/Markdown smoke checks passed. First-map tests were
rerun without concurrent compiler load: the ordinary preview suite passed
(12.53 s), after timing-sensitive loading-window assertions failed while both
sanitizer and optimized builds were compiling. Light/dark theme behavior and
synthetic typography/Markdown screenshots were checked.

E-book follow-up: FB2/SFB/MOBI samples were added to the private sweep (81 files,
60 intentionally partial). The full document suite and sample sweep passed
ASan/UBSan with leak detection: 24 results, no failures or skips. FB2 multi-line
titles, inline spaces, SFB block markers and adjacent heading lines, and MOBI
nested quote indentation/repeated line breaks were checked visually. Native
ebook extraction and controller checks passed; the native geometry test passed
when rerun after compiler load stopped, following a compositor timing failure
during concurrent builds. No geometry code was changed for these readers.
The final optimized build passed all 18 registered suites (42.44 s). The local
installation was refreshed and all eight document readers passed installed
preview/replacement/hide/quit checks, including the new FB2/SFB/MOBI samples.
The versioned 0.7.0 release build also passed all 18 suites (43.01 s), with CMake,
RPM, Debian and Arch version fields checked together.

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
text snapshots, source mutation after copying, bounded text and uncopied large PDF
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
session with the screen unlocked (check `org.freedesktop.ScreenSaver.GetActive`
before interpreting exposure failures). It creates generated images, text and an unsupported resource, runs its
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


## Debian and Arch package checks (2026-10-18)

Native packages built from `5bc64ac` passed all ten CTest suites as an
unprivileged user in separate rootless Podman build environments: Debian 13
amd64 (27.72s) and current Arch Linux x86_64 (25.84s). A fresh runtime container
for each distribution, separate from its compiler/development environment,
verified:

- SHA-256 manifests and native package contents.
- Installation through APT or pacman, resolving declared runtime dependencies.
- Installed package integrity (`dpkg --verify` / `pacman -Qkk`).
- Exactly the four application/menu files plus package documentation/licenses.
- Desktop entry validation and absolute `/usr/bin/dzurkel` commands.
- Both application and Settings launcher commands starting on isolated buses,
  with offscreen Qt, Unicode paths, Settings/Show/Hide/Quit forwarding and shutdown.
- Removal/purge, package absence and removal of all four runtime paths.

The [first packaging CI run](https://github.com/EdinUser/dzurkel/actions/runs/39096842452)
passed both new package builds and all suites, Debian installation/removal, and
all existing Fedora jobs. Its Arch integrity check exposed the minimal image's
`NoExtract` documentation policy: `/usr/share/doc` was missing. The clean-install
job now enables documentation extraction to verify the full payload; the corrected
sequence passed locally. Only the main Arch package is installed for runtime
checks; debug symbols are optional artifacts. Subsequent CI runs are available
from the [workflow](https://github.com/EdinUser/dzurkel/actions/workflows/ci.yml).

These are automated, headless package checks. **Real KDE Wayland desktop
acceptance remains pending on Debian and Arch**, including launcher/menu discovery,
Dolphin interaction, focus, shortcuts, scaling/themes, upgrades and long sessions.
Debian 13's stock Dolphin 25.04 is below the 26.04+ Space setup target; CLI and
context-menu previews are the intended entry points there. Existing Fedora user
acceptance and the confirmed focus fix do not establish desktop acceptance for
these distributions. [Issue #4](https://github.com/EdinUser/dzurkel/issues/4) remains
open. The host's user-local development installation was not changed.


## Initial Ubuntu GNOME and Nautilus workflow

On Ubuntu 26.04.1 amd64, GNOME Shell 50.1, Nautilus 50.2.2 and Wayland, the user
installed the Debian 13 DEB after enabling Ubuntu universe. Package status,
version and `dpkg --verify` passed. The user confirmed the Settings launcher,
text preview, focus, Escape dismissal and reopening worked as expected.
The DEB was subsequently removed and its four system runtime paths were verified
absent before switching to a user-local native development build.

The native Ubuntu Debug build passed all eleven registered suites after adding
`dzurkel_nautilus` (24.12s). The final script regression set also passed separately
on Fedora and the VM. A separate private-bus check of the VM's installed Nautilus
script exercised real text previews with Unicode/spaces/special-character URIs,
resident-process forwarding, Hide and Quit. This establishes selected-file
handling through the installed script and IPC, not actual GNOME menu delivery
or compositor focus.

The opt-in **Scripts → Preview with Dzurkel** action is installed in the VM.
Real Nautilus menu visibility, selection delivery, focus and dismissal remain
pending user acceptance. Space invocation and remote GNOME/GVfs integration are
not implemented by this test script. Follow the
[GNOME testing workflow](gnome-testing.md) and record results separately.

The user observed a Qt portal app-ID registration warning from the development
Settings launch and confirmed Settings still opened normally. Its cause remains
unconfirmed; no portal suppression or behavior change was introduced.


## Non-KWin Wayland preview resizing

The user confirmed Nautilus script previews displayed the correct content,
neighboring-file navigation worked, and manual resizing was retained on reopen.
During diagnosis the Ubuntu GNOME VM reported 1280×800 at 100% scale with
Mutter's automatic maximization enabled. That setting can maximize an initially
large reading window; it is distinct from remembered manual sizing.

The application previously sent every mapped Wayland auto-resize through KWin
scripting, leaving no effective resize path on Mutter. The existing native image
sizing regression failed on the VM before the change. Window placement now probes
for KWin's service once per preview window. KWin retains its existing geometry
transaction; other Wayland compositors use coalesced Qt resize requests after the
current layout event, cancelled by newer resources or dismissal.

The new `mappedImageAndTableTransitionsRestoreReadingSize` regression checks two
small-image/table cycles, preservation of an explicitly chosen reading size,
stable native window identity, and no dismissal during automatic sizing. It
passes on native GNOME and KDE Wayland, alongside the existing image-sizing
regression. The native KDE visible-preview focus regression also passes.
The Ubuntu VM's eleven registered offscreen suites passed (28.27s).

### Resident-window reopening follow-up

The user subsequently reproduced a separate failure after dismissal: opening a
small image after a spreadsheet retained a nearly screen-sized background.
Extending the regression to hide/reopen cycles reproduced 1207×735 instead of
400×400 on native GNOME. Diagnostics showed QWidget in its normal state while
its QWindow still held a maximized state. Qt reused the stale native state when
creating the next Wayland surface.

Before showing a hidden Wayland preview, Dzurkel now synchronizes QWindow's state
with QWidget's state. This preserves an intentionally maximized preview; it does
not force every image to unmaximize. Diagnostic mode reports resize, show and
state changes without logging filenames or activation tokens in these messages.

The extended regression and four related native GNOME cases passed (4.998s),
including the compact-information rapid reopen case that had previously failed
intermittently and intentional maximization across reopening. Native KDE checks
for image/table transitions, image sizing, maximized information restoration and
visible-preview activation passed (5.447s). All eleven registered suites passed
on Ubuntu (28.33s) and Fedora (34.91s). Registered suites use offscreen rendering;
the native checks above are separate desktop runs.

The updated user-local build was installed only in the Ubuntu VM. The user
confirmed small images now resize correctly after the requested spreadsheet/image
reopening and navigation checks. The image-to-document positioning problem
remains separate; see the [GNOME placement investigation](gnome-window-placement.md).
VM display changes, other GNOME versions and mixed-DPI configurations are not
established by this check.

### Maximized-document navigation follow-up

The user then reported that CSV → Alt+Right → small images still retained a
maximized window. The installed VM binary matched the tested build: this was
distinct from the stale native state on reopening. Image sizing deliberately
returned early when the preceding document was maximized, including Mutter's
automatic maximization. The earlier manual confirmation did not establish this
navigation path.

`maximizedTableNavigationCompactsImagesAndRestoresTable` reproduces the failure
with a real CSV and 40×40/400×400 images, using Alt+Right/Left through the real
folder navigator. It failed against the preceding implementation on native
GNOME. Images now temporarily restore a maximized document to normal state,
fit after its native unmaximize configure, and restore document maximization
on return. Maximizing an image directly remains effective for subsequent images
and reopening. The test also covers dismissal/reopening after these transitions
and checks navigation does not dismiss or replace the Qt window.

The new regression, mapped/reopened image-table sizing, image sizing and
maximized-information restoration passed on native GNOME. Native KDE checks
also passed (5.351s). The separate compact-information rapid reopen stress case
passed in an earlier grouped run but failed on the final grouped GNOME run at
its reopened-width assertion; its known intermittent failure remains unresolved.
All eleven offscreen suites passed on Ubuntu (26.66s) and Fedora (34.85s).
The VM user-local binary was updated after quitting its resident instance.
Manual acceptance of the exact CSV → Alt+Right sequence on this update is pending;
positioning remains a separate issue.


## GNOME Wayland internal placement follow-up (2026-10-18)

The GNOME-specific presentation path retains the Qt window/native surface and
prepares adaptive dimensions before internally remapping a top-level role. It
keeps equal-size updates mapped and distinguishes internal hiding from dismissal.
The existing KWin placement adapter and resize restoration fixes remain in use.
See [GNOME placement](gnome-window-placement.md) for the focus gate and limits.

Full offscreen suites passed on Fedora (11/11, 34.11 s) and Ubuntu (11/11,
29.26 s). Native KDE checks using the actual KWin adapter passed remembered
reading-size transitions, maximized CSV/image navigation, image sizing and
information/maximized restoration (6.626 s including setup/cleanup). An earlier
KDE run without the test bus override failed mapped resizing/navigation; it did
not exercise the normal KWin adapter.

The native GNOME regression covers delayed replacement, equal-size updates
without remapping, retained QWidget/QWindow identity, released initial size
limits, Settings holding a completed result, newest-request replacement and
cancellation both during loading and after completion. It passed after the final
held-result assertions (3.824 s including setup/cleanup). Grouped native GNOME
image/reading and maximized CSV navigation checks also passed; information/state
restoration waited for focus in that group and passed separately. This does not
establish that every native GNOME test passes; the earlier rapid information
fallback/reopen stress case remains outside the resolved placement claim.

An isolated real-controller harness measured six previews on the GNOME VM using
framebuffer captures. Real VM Alt+Right input selected all six expected files;
each settled preview was active and centered with the existing Mutter centering
preference. Real Escape dismissed it. A 250 ms preparation delay produced the
same final placement after the initial loading fallback. A real Alt+Tab check
confirmed a completed background result stayed pending without hiding the old
window or taking focus, then committed when returning. Protocol lifecycle checks
confirmed one main wl_surface throughout, with six top-level roles (seven when
the slow first load mapped a loading view). Native Qt global coordinates were
not used as proof of positioning.

These checks cover GNOME 50.1/Qt 6.10.2 on one 1280×800 display at 100% scale.
They do not establish flicker-free transitions, user acceptance of mapping
animations, other GNOME versions, multiple monitors, tiling or fractional scaling.
No global settings changed and no performance benchmark was run.

## Nautilus Space adapter (2026-10-18)

The opt-in Settings integration adds `dzurkel_nautilus_adapter`, bringing CTest
to 12 suites. All 12 passed on Fedora and on the Ubuntu VM. The final bounded
registration-backup check also passed in the targeted Fedora adapter suite;
the VM helper then reran all 12 successfully (26.40 s) before user-local install.
Three existing native KDE sizing/navigation regressions passed (5.536 s,
including setup/cleanup) using the actual KWin test bus override.

Production Settings was exercised on the GNOME VM using a private bus and
temporary XDG directories. It detected Nautilus, enabled the preview service
with Files already open, and restored it through the native buttons. Restore
released the service name, removed the temporary registration/backup, and left
Settings and the current preview open. Actual Space selected the correct local
file, and Alt+Right followed Files selection through images, CSV and text,
including Unicode, spaces, `#` and `%`. Maximized CSV to a small image retained
the stable resize behavior. Actual Escape dismissed the preview. After stopping
the resident and confirming no provider owner, Space activated the final
installed binary and opened the selected text file; Space from Files on that
same visible URI toggled it off.

An initial check exposed selection updates incorrectly consuming activation
tokens as explicit activation requests. The final adapter restricts mapped
reactivation to Space requests. Actual Alt+Tab to Files followed by a new
selection then left Files focused and the preview pending (`Loading=true`);
returning to Dzurkel committed the result (`Loading=false`).

Competing ownership, original-user-registration restoration and preservation of
subsequent edits were tested on isolated buses/files. Sushi itself is absent in
this VM, so real Sushi exit/fallback activation remains untested. These checks
cover GNOME 50/Nautilus 50.2.2 local files; GVfs, other versions, multiple
monitors and fractional scaling remain outside this validation. No new runtime
dependency, extension, compositor script or global shortcut was introduced.

## Required native GNOME regression gate

The dedicated `GNOME Wayland, focus and Nautilus Space` job now runs the
`dzurkel_gnome_native` CTest on a separate headless GNOME Shell/Mutter session.
It is a prerequisite of draft-release creation. Private session/system buses, home, runtime,
settings and software-rendered virtual monitor isolate the user's desktop and
installation. The test-only observer is not installed or linked into the app.
Run instructions and exact coverage are in the [native gate guide](gnome-testing.md#native-gnome-gate).

The ordinary suite now has 14 tests: two additional guards verify that a required
native regression fails instead of skipping offscreen, and that frame assertions
reject displaced, offscreen and invalid bounds. All 14 passed locally (39.09 s).
The native gate also runs the existing delayed-result/Settings/cancellation Qt
regression with zero skips, then sends real Mutter-delivered keys to Files and
the production application. It checks provider kind, cold startup/restart,
selection synchronization, dismissal/toggle, background focus/result holding,
equal-size mapping, changed-size remapping, retained Qt-window identity,
maximized CSV/image restoration and registration cleanup.

With normal animations forced on, three consecutive native VM runs passed in
20.84, 20.93 and 20.95 seconds. The measured small/same-size client rectangle
was `(440,235)-(840,635)` and the wide rectangle `(232,182)-(1048,688)`;
both centers are `(640,435)`. Returning from maximized CSV restored the same
small rectangle. Measurements are framebuffer pixels, not Qt positions.
Reports record GNOME Shell 50.1, Nautilus 50.2.2 and Qt 6.10.2.

Two temporary mutant test probes verified failure detection without changing
production sources or installations. Reintroducing token-based reactivation on
background selection failed with an active preview/new content where Files
should retain focus. Disabling internal remapping failed the rendered-center
assertion: `(848,488)` versus `(640,435)`. Neither failure was counted as a pass;
the negative-control driver required the specific assertion and nonzero exit.

The gate produces synthetic-only JSON, process logs and framebuffer captures,
uploaded as `gnome-native-results`. Missing native backend/input/capture, skipped
native assertions or absent tests fail the job. This closes the previous
offscreen-only CI gap for this desktop configuration; hardware GPUs, physical
multi-monitor/fractional-scale layouts, real Sushi fallback, other GNOME versions
and subjective animation continuity remain separate acceptance work. The older
rapid information/reopen stress caveat is not claimed resolved by this test work.

## Thunar setup and Arch XFCE validation

Thunar integration is opt-in through Settings and uses the shipped Custom Actions
and accelerator system. The `dzurkel_thunar` suite covers XFCE detection while
preserving KDE/GNOME choices, single-file action generation and executable quoting,
exact original-file/permission restore, system-action defaults, conflicting Space
bindings, unrelated edits, Thunar's XML/accelerator reformatting, malformed files,
symlinks, incomplete and large backups, process races and cancelled restart.
The Settings enable/restore flow is exercised without touching live Thunar state.

The Arch XFCE/X11 VM with Thunar 4.20.10, Qt 6.12.0, GCC 16.2.1 and CMake 4.4.4
passed all 15 registered suites in 25.28 seconds. Native validation used a private
session bus, temporary Thunar profile and synthetic files. Production setup code
generated the configuration; actual Thunar keyboard events exercised cold Space
activation, the context-menu action, Escape/Space dismissal and multi-selection
exclusion from Dzurkel. Both an executable path and a filename containing spaces,
apostrophes, hash and percent characters worked, including a Unicode filename.
Restore succeeded after Thunar generated default accelerator entries, preserving
those defaults while removing Dzurkel's binding and setup backup.

The native driver is a development experiment, not a required CI compositor job.
CI includes the new configuration/Settings suite through ordinary CTest. The
Fedora run of the existing preview suite hit one cold-information first-map width
assertion during concurrent building; that suite passed on rerun without preview
code changes. No new GNOME remapping, resizing, controller or KWin behavior was
introduced by Thunar integration. Real XFCE Wayland and other Thunar versions
remain outside the native validation. See [Thunar integration](thunar-integration.md).

Thunar restart focus is also protected by automatic cases: process startup alone
must not reactivate Settings, the request runs once after window readiness, and
closing/reopening, minimizing/restoring or destroying Settings cancels it. The
opt-in `realThunarRestartReturnsFocusToSettings` case passed on the native Arch
XFCE/X11 desktop (3 passed, 0 failed, 0 skipped; 1.381s). It launches a separate
Thunar on a private bus and profile, accepts the real Settings restart flow, and
checks both a single focus request and that Settings is actually active afterward.
The user's normal Thunar/configuration are outside this test. The ordinary CI
run skips this native-only case while retaining the deterministic cancellation
checks. Window readiness uses existing public KF WindowSystem X11 support to
identify the active process; no window-control dependency or compositor script
is introduced, and Dolphin's focus path is unchanged.

## Audio integration — Fedora 44, Qt 6.11.2

The normal build passed all 17 registered suites after audio integration. The
opt-in build, which adds the standalone playback probe suite, also passed its
18-suite regression run. The final integrated audio, source and standalone
playback suites passed AddressSanitizer/UndefinedBehaviorSanitizer checks;
leak detection was disabled for external Qt runtime caches.

Read-only, muted checks against local music passed for MP3 and M4A, including
Dolby Digital Plus audio in M4A, embedded cover art and Cyrillic metadata/paths.
The same music check passed on native KDE Wayland with an isolated session bus;
the captured preview was visually inspected. Source sizes and modification
times remained unchanged. The tested backend was Qt's FFmpeg backend using
FFmpeg 8.1.3; other Qt/backend combinations remain separate compatibility work.

The integrated widget also passed generated fixtures for MP3, FLAC, Vorbis in
OGG/OGA, Opus, PCM WAV, AAC/ALAC M4A, standalone AAC and audio-only MP4. A file
with moving video showed an information error instead of playing only its audio.
Controls, forward/backward seeking, immediate stop on replacement/dismissal,
obsolete-result rejection and truncation during playback were checked. Source
tests verify bounded range reads, stalled-read cancellation, helper limits and
cleanup; the staged-install test serves byte ranges through the installed
executable's internal mode without the experiment helper.

To repeat optional local coverage, provide the read-only music root and the
directory produced by `tests/experiments/run_media_matrix.py`:

```bash
DZURKEL_TEST_MUSIC_ROOT="$HOME/Music" \
DZURKEL_TEST_AUDIO_FIXTURES="$PWD/build-media/media-results" \
ctest --test-dir build -R '^dzurkel_audio$' --output-on-failure
```

Without those variables, personal-library and generated-format cases are skipped.
Playback cases requiring an output device are skipped on headless systems;
source, selection and invalid-media tests still run. Real network-disconnection
recovery testing is deferred; failed or timed-out reads stop and show an error.
No file-manager implementation changed for audio integration.

### Slider clicks and keyboard focus

The slider follow-up makes left-clicking a track select that position directly,
instead of taking a small page step. Both seek and volume sliders receive
keyboard focus on click. Handle dragging keeps Qt's normal behavior and styling.

`AudioTest::playbackControlsAndDismissalReleaseSource` exercises actual mouse
clicks and key events against the integrated preview. It verifies that:

- Clicking three quarters along the seek track moves playback to the expected
  region of a synthetic 90-second WAV and gives the seek slider focus.
- Clicking the volume track selects the expected level and transfers focus to
  the volume slider; Right then increments its value.
- Plain arrows on either slider do not request file navigation, while Right
  outside a slider still browses files.
- Space on a playback control dismisses the preview and releases the player.

After this change, the audio regression suite passed (2.21 seconds), and the
control case passed separately on native KDE Wayland (0.825 seconds). Playback
was muted. The earlier full-suite and sanitizer results above belong to the
initial audio integration; the slider follow-up used these targeted checks.
Repeat them from a desktop audio session with:

```bash
ctest --test-dir build -R '^dzurkel_audio$' --output-on-failure
QT_QPA_PLATFORM=wayland dbus-run-session -- \
  ./build/dzurkel_audio_tests playbackControlsAndDismissalReleaseSource
```

## Cold information previews and offscreen diagnostics

The 150 ms loading grace period allows two correct first-map paths: a ready
information result maps compactly, while slower preparation maps a normal loading
window and compacts when the result arrives. The preview tests verify both first
map and final dimensions according to the observed content, rather than requiring
helper startup/classification to beat a wall-clock deadline. A precomputed-result
widget check verifies compact first mapping deterministically; fake SMB/SFTP
checks hold the backend reply until loading is visible, then verify compaction.
The production grace period is unchanged.

Preview/resource test harnesses filter only the exact Qt warning `This plugin
does not support propagateSizeHints()` when running on the offscreen backend.
Other warnings, higher severities and native-platform messages remain visible.
A passthrough check ensures unrelated warnings and critical diagnostics still
reach Qt Test. The damaged-PNG rejection test can emit `libpng error: Read Error`
from the decoder; this is expected for its deliberately truncated fixture and is
not evidence of a valid-image decoding failure.

Validation on 2026-10-19: the optimized Fedora build passed all 20 CTest suites
(46.93 s). Preview results were 40 passed / two native-only skips; resource
results were 20 passed / one live-server skip. Both logs contained zero
propagateSizeHints warnings and zero other Qt warnings. Three first-map runs
under single-core CPU contention passed; native KDE Wayland first-map and
warning-passthrough checks passed seven results without skips (1.161 s).
No live installation was replaced by this validation.

## Useful minimum and build-time compatibility

Validation on 2026-10-19: the normal build passed all 23 CTest suites (52.10 s),
and the `baseline` preset passed all 20 suites (44.38 s). Both were built locally
against Qt 6.11.2; this is not a Qt 6.4 or Debian 12 validation result. The new
Debian 12 CI job supplies that separate release gate and must complete remotely.

Coverage includes optional audio adapter absence, invalid/unloadable modules,
adding an adapter and refreshing without restarting, baseline previews, missing
antiword recovery, command copying, compatibility calculation and staged install.
The executable's ELF requirements contain neither Qt Multimedia nor the optional
KF6 backends. Python syntax, CI YAML, shell syntax, RPM spec parsing and diff
whitespace checks passed. Full native distribution package builds remain CI work.

Native KDE Wayland checks passed four results for Libraries status/guidance and
four for the minimum build's missing-audio Settings action and basic previews.
The Libraries screenshot was inspected. The isolated native session emitted
portal/accessibility service diagnostics, including a test application ID
registration warning; these were not suppressed. Native clipboard contents were
preserved. No installed application or system packages were changed.

See [runtime baseline and compatibility](runtime-baseline.md) for candidate OS
floors, validation requirements and the distinction between a baseline build and
a binary produced against newer libraries.

## Arch package testing without optional libraries

On 2026-10-19, a snapshot of the active source was packaged in a disposable Arch
Podman builder with all three adapters enabled. All 23 CTest suites passed;
audio-device, external fixture and native-desktop cases retain their documented
skips. Package contents include audio, highlighting and desktop adapters, while
metadata requires only the baseline runtime packages. A Qt 6.12 metadata enum
deprecation warning was observed; it did not fail compilation.

A fresh Arch runtime container installed the package through pacman. Qt
Multimedia, KSyntaxHighlighting, KF6 WindowSystem and antiword remained absent.
The installed audio adapter's dependency inspection confirmed missing
`libQt6Multimedia.so.6`. Baseline previews, catalog stability, missing-audio
Settings recovery and missing-antiword recovery passed six Qt Test results with
no skips. One offscreen `raise()` warning was retained. These container checks
do not establish native desktop/focus acceptance or audible playback support.

The private Arch helper now builds on the Fedora host and stages the native
package in the runtime VM, where a user-run installer requests only required
dependencies. The old user-local binary/launchers are backed up during migration.
The tested package was staged in `~/Dzurkel-packages/package.A491v27z` on the
Arch VM; its system installation and interactive library workflow are pending
the user's sudo transaction and desktop checks. No optional libraries were
installed in that VM by this validation.

## Image maximization between preview sessions

The user clarified that normal resizing works, but maximizing an image carried
that state into a later dismissed/reopened preview. The old implementation and
tests explicitly preserved image maximization. The new requirement resets an
image's maximized/fullscreen state on reopening, while preserving maximization
within the active browsing session and the saved document geometry.

`gnomeReopenedImagesFitAfterNativeMaximize` failed against the old behavior on
native Ubuntu GNOME Wayland. After the focused `preparePresentation` change,
three maximize/dismiss/reopen cycles passed, including a Settings transition,
400x400 small-image fitting, matching QWidget/QWindow states and manual resizing.
The targeted GNOME/table checks passed four results without skips (3.545 s).
An earlier combined native run had an intermittent failure restoring a table
from 1213x730 to 740x600; the same check then passed in isolation. No general
resize or compositor-state rewrite was made to address that separate observation.
Native portal/text-input diagnostics remain visible.

After the change, Fedora's full build passed 23/23 suites (61.81 s), its baseline
build passed 20/20 (42.26 s), and the Ubuntu build passed 20/20 offscreen suites
(42.54 s). The tested Ubuntu executable
was installed into the existing `~/.local` development prefix after backing up
the previous executable under
`~/.local/share/dzurkel-dev-backups/image-reopen-20261019T121140Z`. Installed and
built executable hashes match. System packages were unchanged. The staged Arch
library-workflow package above predates this subsequent window-behavior change;
rerunning the Arch helper builds a fresh snapshot with it included.

## Pre-commit baseline verification

An isolated Debian 12 Debug build with Qt 6.4.2 and libzip 1.7.3 passed all 20
CTest suites (34.43 s), including staged installation, with optional SDKs absent.
The check found a missing explicit QGuiApplication include in the Settings
clipboard handler, which was added. The allocation-budget fixture now uses a BMP
header with 8192x8192 dimensions, within the reader's header limits but above the
64 MiB decode budget. This avoids relying on PNG native-scaling support, which
differs across Qt versions; damaged-PNG rejection remains covered separately.

The reusable Arch VM scripts are tracked under `packaging/arch/`; private helper
wrappers retain local destination defaults. Script syntax/argument handling,
Python/YAML syntax, RPM optional dependency filtering and documentation links
were checked. Release publication remains the workflow's only trigger; no tag,
release package build or release publication is part of this commit preparation.
The final Fedora Qt 6.11.2 build passed all 23 CTest suites (51.99 s), including
the revised image-budget fixture and Settings clipboard checks.

## 0.7.1 release preparation

On 2026-10-19, the versioned 0.7.1 Debug build passed all 23 CTest suites
(48.13 s), including the staged installation with all eight color PNG sizes and
both scalable SVG icons. The desktop entries validate; the Settings entry retains
the existing multiple-main-category hint. PNG dimensions, SVG XML, RPM spec
parsing, shell syntax and diff whitespace checks passed.

CMake, RPM, Debian and Arch versions agree on 0.7.1. Release-specific audio and
desktop adapter IDs were advanced together; the highlighting interface retains
its independent ABI version. The regenerated compatibility report identifies
0.7.1 and confirms the core executable has no direct dependency on the optional
audio, highlighting or KDE desktop libraries. Native desktop checks for the
earlier changes are recorded above; the icon change has not received a fresh
native desktop acceptance pass. Distribution package builds, clean installation
and removal, the Debian 12 baseline and native GNOME gate run in release CI.

## Video integration — Fedora 44, Qt 6.11.2

The normal Debug build passed all 24 registered CTest suites after video
integration (58.62 s). Video, audio, streaming-source and standalone playback
suites also passed AddressSanitizer/UndefinedBehaviorSanitizer checks (23.06 s),
with leak detection disabled for external Qt runtime caches. Optional-runtime
fallback now exercises both Multimedia and Multimedia Widgets dependencies.

Generated fixtures exercise silent MP4 with indexes at the beginning/end, MKV,
VP8 WebM, AAC audio-only MP4, actual-frame seeking, pause/end behavior, dismissal,
replacement, stale failures, invalid media and streaming selection without a
whole-file snapshot. Fixture tools/codecs and personal-file checks are explicitly
skipped when unavailable; their absence is not counted as native validation.

Read-only local HEVC/AC-3 MKV and mounted CIFS samples passed integrated native
KDE Wayland controls, frame delivery, seeking and dismissal checks. An actual
active-window compositor capture was visually inspected for rendering and aspect
fit; QWidget snapshots omit the RHI video surface. Source sizes/mtimes remained
unchanged. These tests deliver Qt events rather than physical compositor input.

Ten standalone local/CIFS playback, repeat, delayed-read and cancellation cases
passed with the helper's fixed 512 KiB read-ahead window and <=64 KiB IPC
transfers. Adding 250 ms per filesystem read produced approximately 1.1/0.5 s
forward/backward seeks, versus 9.3/5.8 s without read-ahead. Cancellation during
an initial five-second delayed read did not wait for that read to finish; helper
cleanup completed. These are latency-injection observations with uncontrolled
filesystem/server caches, not SMB wire-traffic or bandwidth guarantees.

See [video previews](video-preview.md) for behavior, policies and opt-in test
variables. Real disconnection/recovery, direct SMB/SFTP authentication, actual
missing-output-device playback, other backends/codecs, decoded-memory bounds
and full GNOME/XFCE desktop acceptance remain separate checks.

Audio/video now use a shared session mute/volume choice, with audible autoplay
by default. `VideoTest::sharesMuteAndVolumeAcrossAudioAndVideo` checks the
default using a silent video, carries mute/volume into audio-only MP4, then
checks unmute/zero-volume inheritance back into video after dismissal. Unmute
checks use silent content or zero volume so the test does not emit sound.
Personal-file playback tests explicitly mute before queued media startup.

## 0.8.0 release preparation

On 2026-10-19, the versioned 0.8.0 Debug build passed all 24 CTest suites
(66.11 s). The baseline preset, with media, highlighting and KDE activation
adapters disabled, passed all 20 registered suites (44.19 s). Both builds used
Fedora's Qt 6.11.2; this baseline run does not establish Qt 6.4 or Debian 12
compatibility. Earlier native media and sanitizer checks are recorded above.

CMake, RPM, Debian and Arch versions agree on 0.8.0. The executable reports
0.8.0, and both regenerated compatibility reports identify that version. The
full build contains all three optional adapters; the baseline contains none.
The core executable has no direct Multimedia or KF6 dependency. The desktop
adapter's release-specific ID was advanced; media and highlighting retain
their independent interface versions.

RPM spec parsing, local documentation links, staged-path checks and diff
whitespace checks passed. Debian now recommends Multimedia and Multimedia
Widgets runtime libraries; their SDKs remain build dependencies. Release
package builds, clean installation/removal and the native GNOME gate still
require the release-publication workflow. No tag or GitHub release is created
by this commit preparation.

## KDE file-dialog adapter implementation

Recorded 2026-10-20 for the current source implementation. Automated/native
checks used isolated test processes and session buses. The user subsequently
reported a successful Fedora smoke test and selected this implementation as a
stable checkpoint. Individual applications and login/logout coverage were not
specified in that report. The adapter is not in the published 0.8.0 installers.

The opt-in Qt generic plugin is loaded automatically in chooser tests rather
than explicitly installed by a custom host. The complete Fedora Debug suite
passed all 26 registered tests (81.15 s), including startup from no resident,
caller/session ownership, disconnect cleanup, preserved text/multi-selection,
reversible session environment and staged installation. Runtime fault cases
cover missing/corrupt module, missing KIO and mismatched plugin interface while
an ordinary Qt application retains startup and text entry. Native-only cases
are explicitly skipped in offscreen runs.

The stock Fedora KDE portal backend and stock kdialog each passed an isolated
Wayland sequence: file list active, Space opens Dzurkel, preview becomes active,
Escape hides it, focus returns to the chooser, and chooser Escape cancels
without acceptance. Generated text fixtures and Qt-delivered keys were used;
this does not claim physical input or all-browser coverage. A native QFileDialog
component check also passed three QtTest cases including init/cleanup, no skips.
The test input driver is not installed. See [reproduction and limits](file-dialog-previews.md).

The baseline preset with all optional adapters disabled passed all 20 suites
(44.62 s). Final focused chooser/fallback/D-Bus/compatibility/install checks
also passed after stricter session-ID validation and adapter report updates
(5.04 s). A real session sign-out/in and individual browser acceptance remain
separate checks.

Before the stable checkpoint commit, the seven focused chooser, fallback,
D-Bus, Settings, CLI, compatibility and install suites passed again (12.94 s).
They ran outside the restricted filesystem sandbox so isolated D-Bus sessions
could create their local sockets.

## Desktop-backed folder contents size

Recorded 2026-10-20 on Fedora 44, Qt 6.11.2 and KIO 6.31.0. The full Debug build
with the optional KIO calculator passed all 28 CTest suites (69.62 s). After final
folder layout, navigation and lifecycle refinements, the seven focused folder,
fallback, resource, preview, interaction, compatibility and install suites passed
(28.64 s). The baseline with all optional adapters disabled passed all 21 suites
(50.57 s); real-KIO cases explicitly skip there.

Generated fixtures check nested and empty folders, hidden files, sparse logical
sizes, hard-link deduplication, exclusion of symlink targets, special objects and
backend-reported errors. Controlled callbacks check progress, incomplete results,
Stop, dismissal/replacement/destruction, stale notifications and information-popup
updates. Runtime-fault cases cover missing/corrupt modules, incompatible interface
and missing KIO dependency; folder metadata remains usable with size unavailable.
SMB/SFTP tests use a fake existing resolver followed by a real KIO calculation on
a generated local folder; they do not establish network/server compatibility.

A native KDE Wayland check displayed the expected generated-fixture contents
size and counts through the normal preparation/controller path. The window was
visually inspected after folder navigation and plural wording corrections.
This was an isolated test process/session bus, not installation into the user's
normal session or a physical-keyboard/file-manager acceptance test. See
[folder behavior and build requirements](folder-previews.md).

Real-share performance, disconnection/cancellation, authentication and other
desktop/backend versions remain targeted acceptance checks. No personal folders
were recursively scanned. RPM parsing, shell syntax, local documentation links,
staged installation and whitespace checks passed; release packages were not built.
The final folder/fallback rerun, including cancellation immediately after job
startup, passed both suites (1.35 s). The normal folder suite reported 11 passes
and one intentional unavailable-adapter skip; the separate fallback suite
exercised that case under four runtime faults.

## Folder-size VM investigation

On 2026-10-20, the Ubuntu GNOME development helper rebuilt and installed the
current app after all 25 registered suites passed (53.03 s). The installed binary
matched the tested build by SHA-256. No KIO runtime/SDK was installed system-wide,
so real-KIO cases were skipped there and folder size correctly remained
unavailable. Native Files Space opened the exact generated folder URL and Escape
dismissed it; a generated text-file Space/Escape regression also passed.

A separate isolated Ubuntu KIO 6.24 build exposed a counting incompatibility:
the 19-byte nested/hidden-file fixture reported 179 bytes because directory
metadata was included. Native Wayland rendering confirmed the same incorrect
contents-only label. Folder exact-total and resolved-remote-path checks failed
for that reason; this is a known compatibility gap, not passed GNOME sizing.
The already-present GIO API measured the same fixture as 19 bytes.

Arch XFCE, Qt 6.12.0 and an isolated KIO 6.31 runtime passed folder sizing,
runtime-fault fallback and native X11 rendering. Its initial 25-suite run passed
24 suites and exposed a separate media-source fixture defect: SFTP opt-in was
implicitly inherited from the developer's settings. The fixture now uses a
temporary config and explicitly sets the opt-in; its Arch rerun passed (6.45 s),
and the local Fedora rerun passed (7.14 s). No playback implementation changed.

After unlocking Arch's desktop, isolated native Thunar tests passed file Space,
context menu, dismissal and multi-selection rejection. Folder selection was
confirmed excluded by the existing custom-action selector. The first locked-
desktop attempt is not counted as an integration failure. See
[folder compatibility gaps](folder-previews.md#cross-desktop-validation-and-known-gaps).

The final Arch package build passed all 28 suites (48.06 s). A fresh runtime
container also passed baseline/reader recovery and missing-KIO folder fallback
checks without installing the optional desktop libraries. After the user
authenticated installation in the VM, pacman reported version 0.8.0-1 with all
55 files present; the installed executable matched the tested package by SHA-256.
Native X11 folder/text previews and dismissal passed against `/usr/bin/dzurkel`.
The folder screenshot correctly shows contents size unavailable because KIO is
absent from this standard XFCE installation.

The media fixture correction also passed on isolated Ubuntu (6.22 s). GIO's
existing contents-measurement API returned the expected 19 bytes on both standard
VMs; this validates a candidate desktop API, not an implemented Dzurkel backend.
Firefox upload dialogs were not retested in this run: host VM-control access
failed before browser launch. No real network-share counting was validated.

## GIO folder-size implementation

The follow-up implementation adds an optional GIO >= 2.78 calculator, preferred
outside KDE, and restricts the KIO calculator to the contents-only 6.30 ABI.
The interface version rejects older Dzurkel modules. Configuration against the
isolated Ubuntu KIO 6.24 SDK selects only the GIO folder adapter.

Ubuntu's development helper passed all 27 suites and installed the update.
The executable and GIO module each match their tested build by SHA-256. A native
Wayland probe using the installed module displayed 19 bytes, 3 files and
1 subfolder for the generated fixture. The corresponding native Arch X11 probe
also displayed that exact result without a KIO runtime.

The local all-adapters run passed 29 of 30 suites; its image cold-start geometry
assertion failed during concurrent builds, then the entire preview suite passed
unchanged on rerun (19.31 s). Both calculator suites and runtime-fault suites,
resources, settings, install and Thunar migration passed. The optional-adapters-off
baseline passed all 21 suites (60.02 s).

Generated-fixture checks include empty/hidden/nested contents, a selected folder
symlink, hard links, symlink entries, sparse files, errors, rejected unresolved
remote URLs and silent cancellation. GIO's distinct link/count semantics are
asserted explicitly. Thunar's upgrade test verifies idempotence, the original
shortcut restore and preservation of unrelated actions added after setup.

After the user unlocked Arch, real Thunar file Space/context-menu dispatch,
folder Space, Escape/Space dismissal and multi-selection rejection passed with
temporary configuration. Visual inspection confirmed the selected generated
folder displayed 9 bytes. Its temporary quoted launcher was given access to
the matching test plugins; the earlier missing-plugin and locked-desktop attempts
are not counted as successful sizing checks.

The follow-up Arch package passed all 30 suites (62.30 s). A fresh runtime
container without KIO/CoreAddons passed real GIO nested-folder and error/cancel
checks, baseline reader recovery, and folder-unavailable fallback with the GIO
module temporarily hidden. No KDE packages were added to the runtime VM.
An isolated Ubuntu probe also verified that a stale original KIO module does
not override the new GIO contents total under KDE desktop preference.

After user authentication, the installed Arch executable and GIO module matched
the tested package by SHA-256; pacman reported 56 files with none missing.
Installed `/usr/bin/dzurkel` passed native folder/text show/hide checks and the
19-byte folder total was visually confirmed. The existing managed Thunar action
was upgraded after normally stopping its idle background daemon, then that daemon
was restored. The original backup fields and shortcut bytes remained unchanged.
Finally, actual Space/Escape through the normal upgraded Thunar setup and
installed application passed; the generated folder visibly showed 9 bytes.
After final whitespace cleanup, the five local calculator/fallback/Thunar suites
passed again (6.52 s).

## 0.9.0 source preparation

After synchronizing the CMake/RPM/Debian/Arch version fields and the release-specific
desktop adapter ID to 0.9.0, the local all-adapters build passed all 30 suites
(75.66 s); the optional-adapters-off baseline passed all 21 (47.98 s). Both
executables report 0.9.0. Version consistency, workflow YAML, RPM spec parsing,
shell syntax, documentation links and whitespace checks passed. The release
workflow now installs the folder/dialog SDK dependencies in its builders.
This is source validation; no 0.9.0 GitHub release or release installers have
been published by this preparation step.

The release audit compared v0.8.0 with the prepared 0.9.0 source and corrected
the clean-install checker to recognize folder calculators and the KDE chooser
module, including their removal. A fresh Arch runtime container passed that
updated install/launcher/removal checker using the previously tested development
package containing all new modules. This preflight did not rebuild a 0.9.0
release package; the published-release workflow validates those artifacts.

## 0.9.0 published release

The [release workflow](https://github.com/EdinUser/dzurkel/actions/runs/39417990306)
completed successfully for tag v0.9.0 (commit 55ac10c). Fedora GCC/RPM and Clang,
Debian and Arch package builds, all three clean install/launch/removal gates,
the Debian 12 baseline and native GNOME Wayland/Nautilus input/focus/rendered
placement passed. The final job attached the three runtime installers and a
combined SHA256SUMS manifest. All installers were downloaded and checksum-verified
before promoting that same prerelease to latest stable. The release API confirms
v0.9.0 is neither draft nor prerelease. Package validation retains the real-share,
KDE chooser application matrix and hardware/backend limits recorded above.
