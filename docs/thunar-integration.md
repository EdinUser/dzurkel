# Thunar integration

On XFCE with Thunar installed, Dzurkel Settings shows **Thunar shortcut**.
Choose **Enable Space shortcut…** to add both Space previews and **Preview with
Dzurkel** to Thunar's file context menu. Finish all file operations before
accepting a Thunar restart. Windows and tabs may not be restored when it reopens.
On the validated XFCE/X11 session, Settings regains focus once the reopened
Thunar window has taken focus. This is a single request; closing or minimizing
Settings cancels it, and it expires after ten seconds if no ready window appears.

Select one local file and press Space. Escape or Space closes the preview;
navigation inside Dzurkel uses its existing folder navigation. This does not
synchronize Thunar's selection as Nautilus's preview service does. Multiple
selection, folder-background actions and direct remote URLs are not enabled by
this integration; mounted local paths can be used.

Setup uses Thunar's shipped Custom Actions and accelerator support. It requires
no additional Thunar plugin, global shortcut or window-control utility. Thunar's
GUI shortcut picker rejects bare Space, but its action dispatcher accepts a
configured Space accelerator. The context action has a Z mnemonic.

New setups use the installed Dzurkel application icon for the context action.
If setup predates the custom icon, use **Restore previous shortcuts…**, then
**Enable Space shortcut…** after installing the updated app to refresh the action.

Only these per-user files are updated:

- `$XDG_CONFIG_HOME/Thunar/uca.xml` (normally `~/.config/Thunar/uca.xml`).
- `$XDG_CONFIG_HOME/Thunar/accels.scm`.

The backup is `$XDG_STATE_HOME/dzurkel/thunar-preview-backup.json`, normally under
`~/.local/state`. System action files remain unchanged. If no user action file
exists, its system defaults are included in the new user file. File permissions
are preserved. Conflicting bare Space assignments are disabled and backed up;
modified key combinations are retained.

**Restore previous shortcuts…** removes Dzurkel's action and restores the
affected bindings. If the files are otherwise unchanged, their original bytes
and absence are restored. Unrelated action/shortcut edits are retained, including
Thunar's normal XML formatting and commented empty accelerator entries. If a
managed action or binding was edited, restore refuses to overwrite it and keeps
the backup for review. Malformed files, duplicate entries, symbolic links and
incomplete backups also prevent changes. Setup rechecks that Thunar has exited
before writing, and rolls back if either configuration write fails.

Installation alone does not reassign Space or edit Thunar's configuration. Open
Settings from the application menu, from the preview, or with `dzurkel --settings`.
This setup is included in the executable; package installation needs no Thunar
post-install script. Package removal should be preceded by restoring the user
integration so that Thunar does not retain an action pointing to a removed app.

`dzurkel_thunar` covers desktop detection, executable quoting, setup/restore,
conflicting bindings, permissions, unrelated edits, malformed configuration,
process races, restart cancellation and the Settings flow. It uses temporary
configuration and an injected process-state check, leaving real Thunar sessions
alone. Actual key and menu delivery still require a native desktop check.

Reference: [Thunar Custom Actions](https://docs.xfce.org/xfce/thunar/custom-actions).
