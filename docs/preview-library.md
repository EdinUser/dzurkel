# Preview library and reader boundaries

The first library increment supplies a compiled-in reader catalog and a shared
read-only spreadsheet view. It does not download readers, install packages, load
third-party plugins, or enable DOCX/video support. XLSX now supplies a separate
bounded reader for the same table view.

## Current pipeline

1. The resource helper inspects the source and asks `PreviewLibrary` to select
   a reader. PDF signatures and actual Qt image readers retain priority over a
   misleading CSV/text extension.
2. The selected capability supplies a complete-snapshot or bounded-prefix source
   policy and byte limit. Image/PDF snapshots remain capped at 64 MiB; CSV/text
   prefixes at 2 MiB plus one truncation-detection byte.
3. The existing single decoder worker creates `PreviewData`. Each result names
   its reader. Cancellation and request-generation checks remain in the controller.
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

CSV uses a dedicated bounded parser and the shared table view. TSV explicitly
uses tabs. CSV considers comma, semicolon and tab separators using consistent
field counts across up to 32 complete sample records from at most 64 KiB.
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
gets file information. CSV structure errors fall back to the bounded text view
with an explanation. Single-column and empty CSV files remain valid tables.

Limits: 2 MiB input, 10,000 records, 256 columns, 100,000 field positions (including
empty ones), 16,384 characters per field, and five seconds of cooperative parsing
work. Only complete records within the limits are retained. A byte-truncated last
record is discarded even if unquoted. Limit notices distinguish a partial table
from a complete workbook. If no complete record fits, the bounded text fallback
is used. These are application limits, not an OS process-memory sandbox.

Table arrows navigate cells/scroll; Alt+arrows and Previous/Next browse files.
Space/Shift+Space/Escape dismiss as for other previews. Existing mounted, SMB and
opt-in SFTP access routes use the same bounded preparation path.

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
decompression reads and XML tokens. The complete compressed snapshot is capped
at 16 MiB; the ZIP directory at 2,048 entries; all declared expanded entries
(even ignored content) at 64 MiB; each consumed XML part at 16 MiB. Actual reads
also enforce expanded-byte limits. No worksheet/media/style objects are loaded
through an external office library.

Display limits are 32 worksheets, 10,000 rows, 256 columns and 100,000 cell
records across the workbook. Exceeding these gives a labelled partial preview.
Shared strings are capped at 100,000, each field at 16,384 characters, and total
retained string/cell/format characters at 8 Mi. Number format and cell style
tables are each capped at 100,000 entries. XML nesting is capped at 64; DTDs are
rejected. A five-second cooperative loading budget applies. Package, XML,
string-budget and decompression failures show file information, not a misleading
partial table. These are application bounds, not an OS process-memory sandbox;
cancellation cannot interrupt an individual synchronous library call.

Builds require libzip 1.7+ development files and pkg-config. Legacy XLS, XLSB,
XLSM, DOCX and video require separate decisions/readers. No office suite, formula
engine, broker service or reader-package installer is introduced.
