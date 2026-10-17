# Contributing to Dzurkel

Dzurkel is a C++20 / Qt 6 previewer targeting Fedora KDE Wayland and Dolphin
26.04+. Keep preview rendering, resource access, IPC and desktop integration
separate. Prefer public Qt/KDE APIs and four-space indentation.

## Build and test

Follow the [Fedora dependency instructions](docs/installation.md#dependencies),
then run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add behavior-based Qt tests alongside functional changes. The ordinary suites
use offscreen rendering and isolated session buses where needed. Real KWin
placement and SFTP checks are optional and require additional setup; see
[testing](docs/testing.md). Never commit personal documents, credentials,
screenshots containing private paths, or the ignored `helpers/` directory.

## Report a problem

Include your distribution, Plasma, Qt and Dolphin versions, installation method,
reproduction steps, expected result and actual result. A small synthetic sample
is helpful for preview problems. Remove private paths, network credentials and
document contents from logs and screenshots before attaching them.

## Pull requests

Explain the user-visible behavior, include relevant test results, and provide
screenshots for UI changes. Report desktop checks separately from offscreen
tests. Discuss new formats, distributions and expanded scope before implementing
them. Contributions are provided under the project's GPL-3.0-or-later license.
