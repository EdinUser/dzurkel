# 0.7.1 draft notes

Unreleased. One further change is pending; no tag, release publication or release
package build is requested yet. Version fields still describe the current 0.7.0
development base and will be updated together during release preparation.

## Changes completed so far

- Settings is divided into Integration, Previews and Libraries. Preview formats
  are grouped by type, with supported formats in the information popups.
- Libraries lists availability and recovery guidance for required and optional
  components. Highlighting has a persistent usage checkbox; refresh reports its
  completion. Reviewed distro/release commands can be copied, but Dzurkel never
  runs installers or enables repositories.
- The useful minimum requires Qt base, Qt PDF/PDF Widgets and libzip. Audio,
  highlighting and specialised desktop activation use optional adapters; package
  build SDKs do not become mandatory application runtime dependencies.
- Known formats with a missing optional reader show file information and an
  Open Settings action. Missing highlighting keeps plain text usable.
- Builds generate offline compatibility metadata, separating baseline dependency
  candidates from the actual binary's requirements and validated OS support.
- Image previews reopen fitted after dismissal instead of inheriting the previous
  image session's maximized/fullscreen state. Reading geometry remains separate.
- Cold information-preview tests cover both fast provider completion and the
  loading grace period. Offscreen diagnostics filter one known size-hint warning
  while preserving unexpected warnings.
- A reusable Arch VM workflow builds all adapters in an isolated host container,
  verifies the package without optional runtime backends, and installs through
  pacman in the runtime VM.

## Validation and remaining release work

See [testing](testing.md) for automated and native desktop evidence. Native
desktop acceptance and package installation remain separate from library-version
arithmetic. The observed intermittent GNOME table-transition check is recorded
there; the focused maximize/dismiss/reopen check passed.

Ubuntu currently receives general missing-library guidance; Ubuntu-specific
copyable installation commands have not been reviewed. Older OS releases,
unreviewed derivatives and unreviewed architectures have no automatic support
claim. The release workflow still runs only for a published release/prerelease.
