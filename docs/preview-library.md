# Preview library and reader boundaries

The compiled-in reader catalog supplies shared read-only table, text and rich
document views. It does not download readers, install packages or load arbitrary
third-party readers. Audio and video use the shared optional media adapter.
XLSX supplies a progressive worksheet
reader for the same table view. DOCX/RTF/EPUB/FB2/SFB/MOBI/Markdown and text-only legacy DOC share a
formatted document view and progressively loaded, bounded portions.

## Current pipeline

1. The resource helper inspects the source and asks `PreviewLibrary` to select
   a reader. PDF signatures and actual Qt image readers retain priority over a
   misleading CSV/text extension.
2. The selected capability supplies the direct seekable-source or streaming
   policy for the implemented file readers. The resolved source path and original
   URL remain separate, with no whole-file copy or source-size admission cap.
   Decoder helpers open and read/seek within their content/parsing budgets.
   Plain text, CSV/TSV, XLSX and all supported rich documents/books use persistent
   lazy sessions. Images decode bounded pixels and PDF renders requested pages.
   Audio/video use
   helper-backed range reads without a media snapshot.
3. The existing single decoder worker creates `PreviewData`. Direct text/image/PDF/table/document results are
   candidates; the controller waits for a valid first frame from the isolated
   decoder session before committing the view.
   Each result names its reader. Cancellation and request-generation checks remain in the controller.
   Plain text, CSV/TSV, XLSX and all supported rich documents/books open a persistent lazy session and commit its first
   small portion. Later portions use the same coordinator/helper session.
4. The GUI chooses a view from the result kind. A table payload goes to
   `SpreadsheetView`, independently of the reader that produced it.
5. Settings gets the implemented support list/descriptions from the same catalog.
   It reports actual included capabilities, not hypothetical installable packages.

`TabularDocument` contains immutable sheets with dimensions and sparse populated
cell values. `SpreadsheetModel` is a `QAbstractTableModel`; `QTableView` requests
visible values without allocating a widget/item per cell. Initial section sizes
are fixed and user-resizable, avoiding whole-sheet autosizing scans. Worksheet
switching and read-only behavior are shared view responsibilities. XLSX
readers produce this payload without converting CSV to XLSX or using QXlsx
to display tables.

## CSV / TSV

CSV reuses its existing parser through `CsvRecordParser`, the shared lazy
coordinator/window policy, and the existing Qt table model/view. TSV explicitly
uses tabs. CSV considers comma, semicolon and tab separators using consistent
field counts across up to 32 complete sample records from a 64 KiB source sample plus Unicode boundary alignment.
Ties prefer comma, then semicolon, then tab. Ambiguous data cannot always be
identified correctly; the selected separator is shown in the preview status.
A delimiter override control is not implemented in this increment.

Quoted separators, doubled quotes, quoted multiline fields, CR/LF/CRLF records,
empty fields and ragged records are supported. CR-based line endings normalize
to LF. The first row remains data; column headers use A/B/... and row headers use
1/2/... rather than guessing a header row. Values remain literal strings:
leading zeros, long numeric identifiers and strings beginning with '=' are not
converted, evaluated, or opened as links. Empty/ragged cells display blank.
Cell tooltips escape markup. Embedded multiline values are available in tooltips;
the initial grid uses single-line cells.

UTF-8 and BOM-identified UTF-16/UTF-32 follow the existing text decoding policy;
invalid UTF-8 can use a labelled Latin-1 fallback. Binary/control-heavy input
gets file information. Initial CSV structure errors or records that do not fit
field/column budgets fall back to progressive plain text with an explanation.
Errors discovered later preserve the useful table and show an error. Single-column
and empty CSV files remain valid tables.

Batches target 64 complete records or 16,384 retained UTF-16 characters. One
complete record may exceed the character target, within 256 columns and 16,384
characters per field. Parsing retains quote/CRLF state across source chunks and
has a five-second work deadline per batch. There is no lifetime 2 MiB or
10,000-row preview cutoff in the production CSV/TSV path. The synchronous
in-memory/fallback entry point retains those earlier bounds for existing callers.

Text views expose Beginning/End and Ctrl+Home/Ctrl+End through the same coordinator.
TextPortionReader resolves EOF to its final useful Unicode-safe interval, including
files without a final newline and empty nominal intervals caused by boundary
lookahead. The reader seeks directly, without scanning/copying intervening content.
The editor and selected language remain attached. Multiline highlighting context
outside the resident window is accepted as best effort; exhaustive reader/lexer
checkpointing is outside the agreed preview scope.

Both text and tables use `LazyWindow`/`LazyBoundaryWatcher` for bounded adjacent
portions and input handling. `SpreadsheetModel` retains at most three batches
(192 rows maximum), using Qt insert/remove notifications and persistent model
indexes to preserve retained selection, row anchors, column sizes and horizontal
scrolling. Row numbers and the range summary use original source-row positions.
Backward requests scan bounded preceding bytes from a known record boundary;
escaped quotes and embedded newlines are respected. No source-length-dependent
index or source copy is created. Reverse scanning is limited to 64 MiB and five
seconds per request, not a whole-file admission limit.

The shared record writer splits large logical portions into bounded JSON-lines
records; no second transport is added. CSV/TSV permit at most 16 MiB per logical
reply and a five-entry/64 MiB conservatively charged reply cache. Each wire
record stays within 128 KiB, with one active request and one lookahead. These
are content/cache/work budgets, not an OS process-memory sandbox.

Table arrows navigate cells/scroll; Alt+arrows and Previous/Next browse files.
Space/Shift+Space/Escape dismiss as for other previews. Existing mounted, SMB and
opt-in SFTP access routes hand off the resolved source to the same helper session.

## XLSX

The XLSX reader shows a simple read-only grid and worksheet tabs. It reads stored
text, numeric lexemes, booleans, errors and ISO date strings. Unformatted numeric
values remain as stored to avoid rounding long identifiers. Formula expressions
are ignored; only saved results are displayed. Missing results display
`[Result unavailable]`, with a count in the preview status. A cached zero stays
zero, and an explicitly cached empty string stays blank. Saved results may be
stale; Dzurkel never recalculates.

Cell number formats identify common numeric dates and times. Built-in formats
14–22 and 45–47, and simple custom date/time formats (including locale tags and
elapsed hours/minutes/seconds) display normalized `yyyy-MM-dd`, `HH:mm:ss`, or
combined values; durations can exceed 24 hours. Milliseconds are retained when
present in the serial. Both 1900 and 1904 workbook calendars are supported;
serial 60 in the 1900 calendar is labelled `1900-02-29 (Excel)` to identify its
fictional leap day. This interprets content, not the workbook's exact display
format. Conditional/multisection formats, fractional custom format codes,
unrecognized/localized built-in formats, inherited style-only formats and
out-of-range dates keep raw values. Number formats are read from explicit
cellXfs entries; unsupported style references also keep raw values.
Number-format metadata follows the
[OOXML number-format definitions](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.numberingformat).

Other number formatting, colors, merged-cell presentation, charts, images,
pivots and macros are ignored.
Rich text contributes plain text. Column widths and row heights use the same
defaults as CSV. Hidden worksheets are included as tabs. Non-worksheet parts
(such as chart sheets) do not receive tabs. An empty sheet remains an empty grid.

The reader follows internal package relationships, including Strict OOXML
namespaces, and never follows external relationships or extracts archive files
to disk. Actual retained cells determine grid dimensions, not the declared
worksheet range or formatting-only rows. Missing formula result markers do
expand the grid; genuinely empty cells do not.

The narrowed values-only scope uses [libzip](https://libzip.org/) public APIs
and Qt's XML stream reader rather than a workbook editing/layout engine. This
avoids QXlsx's private Qt ZIP dependency and permits cancellation between 64 KiB
decompression reads and XML tokens. An isolated helper opens the validated source
descriptor and exposes read/seek/stat operations through libzip's public source
callback API; libzip still parses the ZIP container. No complete archive is copied
to disk or memory, and there is no source-file size admission cap. Archive opening
has a 4 MiB metadata-read budget, followed by a 128 MiB actual source-read budget.
The ZIP directory is capped at 2,048 entries. Unused media and unselected sheets
are indexed without decompression. No worksheet/media/style objects are loaded
through an external office library. Source mutation can fail loading; this is not
a snapshot. An already-open descriptor remains usable after source unlink.

Production worksheets stream through libzip into QXmlStreamReader. Initial and
subsequent batches contain at most 64 rows, targeting 16,384 characters at row
boundaries. The shared grid retains three adjacent batches (at most 192 rows),
and the coordinator retains five replies within a charged 64 MiB cache. Logical
replies are capped at 16 MiB and transported in validated 128 KiB wire records.
Only the active sheet retains its row window. Switching releases those rows and
remembers one resume token per sheet; return loads that region, then supports
backward/forward scrolling. Pixel-perfect restoration across sheet changes is
not implemented. Ordinary row insertion/eviction preserves the live scroll anchor,
selection, column widths and window geometry through native Qt model operations.

The old 10,000-row, 100,000-cell and worksheet-XML part-size cutoffs do not apply
over the lifetime of progressive browsing. There are still 32 sheets, 256 columns
and 16,384 characters per field. Metadata/styles/shared strings remain eager and
bounded: 16 MiB per required metadata part, 64 MiB aggregate expansion, 100,000
shared strings, 100,000 format/style entries, and 8 Mi retained metadata characters.
These limits can prevent opening a workbook with a very large shared-string table.
Expanding those limits would need separate lookup/continuation work; it is
outside the agreed preview completion scope.

Each worksheet request, including replay, has a five-second cooperative budget,
128 MiB source-read and 64 MiB expanded-read budgets; XML depth is limited to 64
and DTDs are rejected. A cached or immediately following batch is inexpensive.
libzip's public `zip_fseek` supports stored, unencrypted entries only; ordinary
compressed XLSX entries require replay for an evicted older range or sheet return.
A distant replay may exceed the work budget even when forward browsing worked.
There is no growing row index or extracted worksheet copy. Very sparse sheets
retain absolute row labels and bounded batches; jumping directly over huge blank
ranges is not implemented. Initial failures show file information; later failures
retain useful rows and show an error. The shared helper's 30-second request deadline,
non-waiting process cancellation and two live/reaping-helper cap still apply.

Synchronous/compatibility callers retain the bounded whole-preview path (10,000
rows, 100,000 cells and 16 MiB per worksheet XML), sharing the same cell parser
and formatting code. Application budgets are not an OS process-memory sandbox.

Builds require libzip 1.7+ development files and pkg-config. Legacy XLS, XLSB,
XLSM require separate decisions/readers. No office suite, formula
engine, broker service or reader-package installer is introduced.

## Formatted documents and e-books

Production EPUB loads portions of the ordered spine through the shared lazy
coordinator and rich-document record protocol. It targets 8,192 text units or 64
semantic units per load. Inline formatting, lists and tables remain complete
units; transparent div/section/article wrappers can span portions while their
anchors are retained. Three adjacent native Qt text frames and the existing
five-entry/64 MiB charged lazy cache bound retained content. Four reusable image
resource slots prevent a growing QTextDocument image cache. Interned Qt format
history is also refreshed through public `clear()` above 4,096 formats; only
resident portions are reimported with the viewport anchor/selection preserved.
The threshold triggers refresh; live portions can themselves require more formats. Decoded raster data
uses the same document transfer validation, with a 2 Mi-pixel budget per portion;
unsupported/over-budget images keep the existing placeholders.

Contents includes unloaded spine chapters, and internal chapter/fragment links
can request their containing block. Evicted older content reloads through the
same renderer. Bad navigation targets keep the current document usable. Source
mutation and later content errors retain already-visible content. Forward reads
retain one chapter tree and a block cursor; backward misses may replay blocks
within request deadlines/read budgets. The existing Qt XML tree parser remains:
8 MiB per chapter/part, 200,000 nodes, depth 64, 64 MiB consumed ZIP data and five
seconds per portion (initial metadata has a separate bounded pass). A single block still has the 200,000-character/5,000-block/
2 MiB HTML bounds; excessive blocks produce a visible error, not a successful
truncated continuation. Spine metadata is capped at 2,048 entries. There is no
lifetime 200,000-character or 128-chapter cutoff on the progressive path.
Bounded synchronous EPUB callers retain their previous preview contract.


Stable reader IDs `docx`, `rtf`, `epub`, `fb2`, `sfb`, `mobi`, `markdown` and `doc` share `FormattedDocument` and
`DocumentView`. The worker produces escaped, application-generated HTML, plain
text, retained section anchors and already-decoded images. Source HTML/CSS is
never handed directly to Qt. Unsupported formatting is reduced to readable text
where possible. Both the QTextDocument and QTextBrowser resource loaders block
filesystem/network fallback; only retained image identifiers resolve. External
links do not navigate. Internal links scroll to retained anchors.

Production DOCX/DOC/RTF/FB2/SFB/MOBI and Markdown now share the progressive
rich-document path, three resident portions and bounded replay. DOCX streams
complete XML blocks while retaining list/style state; RTF retains group/format
state; FB2/SFB retain section/line state; MOBI retains decoder/render state across
PalmDOC records; DOC drains antiword through a bounded stdout pipe. Older cache
misses replay the same parser/extractor within five-second/64 MiB work budgets.
Eager metadata and excessive indivisible blocks still have explicit limits.

Markdown uses Qt's HTML-disabled importer on conservative structural segments in
the helper, with the existing table preflight/normalization and denied external
resources. Reference-bearing portions trigger bounded reference discovery through
that same importer (2,048 definitions/64 KiB). This may delay initial loading or
fail the work budget. Large indivisible lists/fences/paragraphs fail explicitly;
table-limit fallback uses progressive plain text. Compatibility callers retain
the earlier bounded initial preview. See [architecture](architecture.md) for the
reader-specific limits and [validation](testing.md) for actual checks. Shared
block typography distinguishes title anchors, heading levels and ordinary/list
paragraph spacing. DOCX emits semantic list items; inherited explicit RGB run
colors are retained in source-color properties so contrast can be reconsidered
on palette changes. Font zoom scales all reading typography without requesting
window geometry changes. Plain text uses an independent monospace zoom wrapper.

DOCX and EPUB use libzip public APIs and Qt XML parsing. Package relationships
and EPUB spine order determine content paths. Paths stay inside the archive;
entries are never extracted to disk. RTF uses a bounded group/control parser
and libc iconv for legacy character decoding. DOC calls optional `antiword`
with UTF-8 text output, passing the already validated descriptor through
`/proc/self/fd/0`. No source snapshot is required. Production output drains under backpressure and
per-request budgets; the synchronous compatibility path retains its 1 MiB cap.
Initial failures and missing dependencies return information-only results.

FB2 streams section boundaries and renders bounded XML subtrees in order, including
additional note bodies after the main body. Metadata is kept separate from body
text and base64 binaries are not rendered. Unknown wrappers retain their text;
tables reduce to text separators and images use placeholders. SFB accepts UTF-8
Chitanka text, interprets common title/chapter and emphasis markers, and retains
unknown commands as escaped text. It does not interpret HTML or resolve links.

MOBI uses a small native reader for unencrypted MOBI 6 with uncompressed or
PalmDOC-compressed text (UTF-8 or Windows-1252). It validates monotonically
increasing record offsets (at most 8,192 records), reads the header/table and
requested text records directly, skips unused image/font/index records, strips bounded trailing data,
limits each expanded text record to 4,096 bytes, and retains bounded decoder/tag
carry and the current portion. The existing tokenizer generates only approved
tags, with at most 200,000 tokens per portion and 64 open elements. Attributes and resources
are discarded, tables become text, and images become placeholders. No source
HTML is passed to Qt. Encrypted, HUFF/CDIC and standalone KF8 variants return
information with an explanation. No additional dependency or converter is used.

There is no whole-source file-size admission cap or complete archive copy.
Production document readers request further source content as described above.
DOCX/EPUB retain 4 MiB archive-open/128 MiB later source-read/64 MiB expanded-read
budgets per request, a 2,048-entry ZIP directory and bounded eager metadata.
DOCX main XML streams; EPUB still retains one bounded 8 MiB chapter tree. DTDs
and entity declarations are rejected, except for EPUB XHTML's ordinary HTML
document-type declaration without an internal subset. No external DTD is fetched;
a fixed small map handles common HTML entities.

Synchronous compatibility callers retain the earlier bounded initial preview:
16 MiB RTF/FB2/SFB source prefixes, 2 MiB Markdown prefixes, 8 MiB package parts,
200,000 text units, 5,000 blocks, 2 Mi HTML units and at most 128 EPUB spine items.
Those are not lifetime cutoffs for production progressive browsing. Complete
malformed XML remains an error; only a known bounded prefix tolerates premature
XML end. Limits or unsupported formatting have explicit notices/placeholders.

Per rich portion, up to 16 raster images use the existing validated decoder.
Each image is limited to 4 MiB encoded bytes and 2,097,152 pixels; progressive
portions share a 2 Mi-pixel aggregate budget (8 Mi pixels for bounded callers).
Displayed images fit within 640 × 640 logical pixels. Tables beyond four nesting
levels reduce to text. Unsupported, absent or excess images become placeholders.
SVG/fonts/scripts and remote assets are not decoded or fetched.

A five-second cooperative loading budget applies. These are application bounds,
not an OS memory sandbox; synchronous Qt/libzip calls cannot be interrupted
mid-call. All production readers use the shared lazy helper, with a 30-second request
deadline, a two-helper cap and non-waiting process-group cancellation, including
extractor descendants. Compatibility readers retain StructuredDecodeJob. The host validates document
fields, sections and raster descriptors before publication. Records are capped at
128 KiB and a document result at 128 MiB; native pixel buffers total at most
64 MiB, with profiles at most 1 MiB each. GUI layout is bounded by retained content, but is not asynchronous.
The source identity, request-generation checks, mounted-share and optional
SMB/SFTP preparation routes remain shared with other readers. See
[user-facing scope](previews.md#formatted-documents-and-e-books).

## Audio and video playback policy

Audio and video previews use the decoding support available through the
installed Qt Multimedia backend. Dzurkel is a quick viewer: it does not bundle
additional decoders, transcode files, try a second playback engine, or install
codec packages. Playback in another application is not proof of support in
Dzurkel, because that application may use different decoder libraries.

The initial audio candidates are MP3, FLAC, Ogg/Vorbis (`.ogg` and `.oga`), Opus,
PCM WAV, M4A with AAC or ALAC, standalone AAC and audio-only MP4. These identify
the intended preview scope, not an unconditional promise that every installed
backend can decode every file. Video supports MP4/M4V, MKV, WebM, MOV, AVI,
MPG/MPEG, OGV and WMV under the same policy. Actual tracks choose audio or video
presentation without reopening the source, including audio-only MP4. Both views
share a playback session, controls and session mute/volume choice.

Use Qt's runtime decoding-capability queries to describe available support.
Actual opening and playback determine whether a particular file can be
previewed; neither its suffix nor a capability entry guarantees valid content,
a supported codec profile or successful decoding. Capability checks must not
turn an uncertain diagnosis into a claim that a codec package is missing.

On failure, stop playback, cancel source access, and show file information with a
clear error. Source read failures, timeouts, truncation and invalid media use this
same terminal error path. Automatic reconnection, retries and complete-file
download fallbacks are outside the agreed scope. A network-disconnection
recovery experiment is not a prerequisite for integrating the audio preview.

Use a specific explanation when the cause is established, such as an unreadable
source or a known missing decoder. For an ambiguous format/decode failure, use
wording such as "This audio file couldn't be previewed. Its format may be
unsupported, or the file may be damaged." Keep backend diagnostics in optional
details rather than displaying raw technical output as the main message.

Installation advice is optional and must name a verified remedy for the user's
distribution and active backend. If no reliable package recommendation is known,
give the general explanation without guessing an install command or promising
that adding codecs will repair damaged media. Dzurkel never installs packages
as part of previewing a file.

Compatibility validation checks our source adapter's threading, seeking,
cancellation and cleanup on supported Qt/backend combinations. It does not
require every codec to be present on every system: missing decoding support is
an expected, gracefully reported outcome. The
[streaming experiment](media-streaming-experiment.md) supplies the current
source-access evidence. Audio and video are integrated into the reader catalog;
[video previews](video-preview.md) describes frame readiness, bounded read-ahead,
slow-source feedback and cancellation. Network-disconnection recovery, automatic
resume, advanced tracks/subtitles and hard decoder-memory bounds are not claimed.

## Progressive plain text

The production text path uses `TextPortionReader`, `LazyCoordinator` and
`LazyTextWindow`. Each nominal 4 KiB source interval ends at a nearby physical
line boundary (at most 1 KiB lookahead), or a safe Unicode boundary for long
lines. UTF-8 and BOM-declared UTF-16/UTF-32 preserve code points, CRLF and
whitespace across portions. Initial invalid UTF-8 selects a labelled Latin-1
fallback for the session; later encoding damage produces an error while the
current text stays visible. Source size/mtime changes require reopening.

The reader retains one validated open descriptor and no growing token/index
collection. Evicted portions are seekable in either direction, including after
unlinking an already-open file. The coordinator has one active request, one
replaceable foreground target and one optional lookahead; IDs and generations
prevent obsolete publication. It retains at most five replies and 384 KiB of
conservatively charged payload storage, with 128 KiB wire records. The view
retains three adjacent portions and no undo history. Source length has no
lifetime transfer or initial-prefix cutoff. Scroll position is anchored to a
live text cursor; loading/status feedback reserves fixed space. At most two
lazy helpers may be live/reaping, with a 30-second deadline per request.

The lower-level synchronous text provider remains a bounded decoder for existing
in-memory/fallback callers; it is not the production progressive text path. Selection/copy/search operates on the resident text
window. Syntax highlighting uses the existing optional adapter; context whose
opening construct has been evicted is best effort while the language stays fixed.
CSV/TSV, XLSX and all supported rich documents/books share this lifecycle and
adjacent-window policy with their reader-owned continuation implementations
described above. Images need no text-style continuation; PDF and media retain
their page/range-based lifecycles.
