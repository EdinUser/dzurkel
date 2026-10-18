# GNOME and Nautilus development testing

Settings offers opt-in, user-local **Space** previews on GNOME with Nautilus
(Files) installed. Open **Dzurkel Settings**, check the **Nautilus shortcut**
heading and choose **Enable Space shortcut**. Select a local file in Files and
press Space. Escape or Space dismisses the preview; Alt+arrows navigate using
Files’ selection, including its current sort/filter order. Ordinary arrows also
navigate images and file information; document readers retain their normal keys.
The preview stays unparented and uses Mutter’s normal placement policy.

This registers `org.gnome.NautilusPreviewer` in
`$XDG_DATA_HOME/dbus-1/services/` (normally `~/.local/share/dbus-1/services/`).
The registration starts the same Dzurkel instance on demand. A backup in
`$XDG_STATE_HOME/dzurkel/` (normally `~/.local/state/dzurkel/`) records the
previous user registration. **Restore previous previews** restores it or removes
the override, allowing a system provider such as Sushi to activate again.
Later edits to the managed registration are preserved and require review.
No system service file, package, global shortcut or GNOME extension is changed.
Use Restore before removing or moving the Dzurkel executable.
An already-running provider is never replaced or queued behind: close its
preview and quit it, then retry Enable. Sushi may take time to exit after closing
its preview. Restore releases Dzurkel’s provider name without quitting Files.

The adapter implements the Nautilus 50 `org.gnome.NautilusPreviewer2` contract.
Initial support is for local files; GVfs/network URI integration is not included.
KDE Settings retains the Dolphin workflow. Detection uses the current desktop
and installed supported file managers; an ambiguous unsupported desktop shows
unavailable controls.

The separate Scripts menu test workflow adds **Scripts → Preview with
Dzurkel** to GNOME Files (Nautilus) for one selected local file. It uses Python 3
and the installed Dzurkel executable; it does not require a Nautilus extension
package. It does not assign keys or change the default file-opening application.

## Install the test integration

Build and install Dzurkel into `~/.local` first. From the source checkout, run as
your regular GNOME user:

```bash
python3 integrations/nautilus/install.py --executable "$HOME/.local/bin/dzurkel"
```

The executable script is installed at:

```text
~/.local/share/nautilus/scripts/Preview with Dzurkel
```

Custom `XDG_DATA_HOME` is respected. The generated script contains the absolute
path to the selected executable. For a system package, pass `/usr/bin/dzurkel`
instead. Re-run the installer when changing this path or updating the integration.
An existing Dzurkel-managed script is backed up beneath
`~/.local/state/dzurkel/nautilus-backups/` (or `XDG_STATE_HOME`). An unrelated
script or symbolic link with the same filename is preserved and installation
refuses to overwrite it.

GNOME documents the [Nautilus Scripts menu](https://help.gnome.org/gnome-help/nautilus-behavior.html).
The action should appear when right-clicking a selected file. If the newly
installed action is missing, finish any file operations and close/reopen Files
normally before retesting. Do not force a Nautilus restart with open work.

## Desktop acceptance sequence

Use a test folder containing a text file, an image, a PDF, CSV/TSV and an XLSX
workbook. Include a filename with spaces and Unicode; also test `#`, `%`, quotes,
and a leading `-`. Keep personal files out of screenshots and logs.

1. Open **Dzurkel Settings** from the GNOME application overview. Check the
   Nautilus heading, enable Space previews and close Settings. Test Enable and
   Restore with an existing provider and check that cancellation/conflicts leave
   its files and ownership unchanged.
2. In Files, select exactly one local text file. Choose **Scripts → Preview with
   Dzurkel** and verify the filename/content and File information path.
3. Check that the preview receives keyboard focus; press Escape to dismiss it.
   Files should be usable immediately afterward. Check the displayed **Space**
   dismissal control too, after the preview has focus.
4. Invoke the menu on another file with Dzurkel still resident. Verify correct
   content, window reuse and focus. Switch to another application, return to
   Files and repeat with an already-visible or minimized preview.
5. Repeat with images, PDF, CSV/TSV and XLSX, then perform at least twenty
   open/dismiss cycles. Check neighboring-file navigation, zoom and sheet/page
   controls where applicable.
6. Test multiple selection: the script must reject it instead of guessing one
   file. This initial integration supports local files only; remote GNOME/GVfs
   URIs are rejected. KIO network behavior on KDE does not establish GVfs support.
7. Check light/dark themes and available display scaling. Record focus failures
   by launch route (terminal, Settings launcher, or Nautilus script).

Repeat the sequence with real Space invocation, including a fresh Dzurkel
process and an already-resident instance. Check Files list/grid views, sorting
and filters, navigation selection synchronization, same-file toggle versus a
different selection, and reopening after Escape. Restore the provider in
Settings and verify that a previous system provider can activate again if installed.
The script path does not establish Space activation behavior.

For sizing checks, compare text/CSV/XLSX reading windows before and after a small
image preview. The document should recover its reading size. Manual document
resizing is remembered across dismissal/reopening while Dzurkel remains resident.
GNOME can automatically maximize initially large document windows. Navigate
from that maximized document to small images with Alt+Right: each image should
temporarily use its natural window size. Returning to the document restores its
maximized state. Maximizing an image directly keeps subsequent images maximized;
restore that image window before checking natural dimensions. A VM's resolution
and display scaling change the available space, so record those settings.

Check both navigation while visible and Escape followed by opening another file
through Nautilus. These exercise different window-mapping paths. Resizing passed
user retesting on the Ubuntu GNOME VM. GNOME Wayland now internally remaps
adaptive size/state changes after the final result is ready, allowing Mutter to
apply its initial placement policy. Equal-size updates stay mapped. Switch to
another application during a slow load: the current view should remain in place
until you return to the preview, without taking focus. Repeat with Settings open;
close Settings and return to the preview to apply the pending result.
Mutter's `center-new-windows` setting was already enabled in that VM and controls
initial placement. Check whether repeated mapping animations are acceptable;
do not change global animation settings. See [GNOME window placement](gnome-window-placement.md)
for the lifecycle, focus handling and remaining validation.

## Diagnostics

To observe a fresh resident process, quit Dzurkel before starting diagnostics
from a terminal inside the logged-in GNOME session:

```bash
~/.local/bin/dzurkel --quit
~/.local/bin/dzurkel --diagnostics --show "$HOME/Dzurkel-test/hello.txt"
```

Keep that terminal open, dismiss the preview, then invoke the Nautilus script.
Record the Qt platform, activation events, correct selected file and visible
result. Do not publish raw activation tokens or private file paths. The resident
process remains after closing a window; `--quit` ends it before a source update.

On Ubuntu 26.04.1 / GNOME 50, the user observed:

```text
qt.qpa.services: Failed to register with host portal
Could not register app ID: Connection already associated with an application ID
```

Settings opened normally despite that warning. Record the warning and the
observed UI behavior separately; the cause is not established. Do not suppress
portal logging or disable the portal to make a test appear clean.

## Automated checks and removal

`dzurkel_nautilus` is registered with CTest. It verifies the generated script's
argument/URI delivery (including special characters), selection rejection,
preservation of unrelated scripts, and managed update/removal backups. It uses
a fake executable and does not establish real GNOME activation or menu behavior.

`dzurkel_nautilus_adapter` tests desktop detection, registration backup/restore,
preservation of edits and symbolic links, competing ownership, the D-Bus
contract, external selection navigation and cold activation/resident forwarding.
It uses isolated buses and configuration directories. Native GNOME focus,
placement and real Files input remain desktop acceptance checks.

To remove only the test integration, from the checkout run:

```bash
python3 integrations/nautilus/install.py --remove
```

This removes the managed script and retains backups. The app, preferences and
other Nautilus scripts remain installed. Native packages currently contain the
existing four runtime files; this integration is installed separately for testing.
