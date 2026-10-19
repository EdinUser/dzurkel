# Dzurkel application icons

Promoted from the custom Dzurkel icon family in `helpers/icons/Dzurkel-icons/`.
These tracked files are the application and packaging assets; builds do not
require the ignored `helpers/` directory. They follow the project's
GPL-3.0-or-later license.

- `hicolor/scalable/apps/dzurkel.svg`: primary color icon, also used in the root README.
- `hicolor/scalable/apps/dzurkel-symbolic.svg`: single-color symbolic variant.
- 16, 22, 24 and 32 px PNGs: compact design with stronger details at small sizes.
- 48, 64, 128 and 256 px PNGs: primary color design.

CMake installs the hicolor tree for launchers and file-manager menus. The PNGs
are also embedded via `icons.qrc` for application windows before installation,
without requiring a Qt SVG plugin. Preview and Settings windows inherit the
application icon; file information continues to use the file's MIME-type icon.

The alternate flat, white symbolic and EPS artwork remains in `helpers/` for
design work and is not installed.
