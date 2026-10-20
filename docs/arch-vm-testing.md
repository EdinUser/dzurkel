# Arch VM library workflow

Use a runtime VM separate from the package builder. Building adapters requires
their SDKs; installing those SDKs in the runtime VM defeats missing-library tests.
The host-side helper builds the complete package in a disposable Arch Podman
container, tests it in a fresh runtime container, then copies it to the VM and
installs it through pacman with only required dependencies.

From the host checkout, as an ordinary user with rootless Podman, Git, Python 3,
SSH and repository network access:

```bash
bash packaging/arch/test-vm.sh your-user@arch-vm
```

Use an SSH config alias or `user@IP`; no libvirt machine name is needed. Establish
trusted key-based SSH access first. The VM needs Arch Linux, pacman, sudo and a
logged-in desktop session. No guest compiler, Ninja or optional backend SDKs are
needed. The helper requests the VM sudo password in the user's terminal; it does
not collect credentials. Pacman's transaction remains interactive.

- `--stage-only` builds/tests and copies the package, then prints the guest
  installer command. It does not install in the VM.
- `--build-only` builds/tests locally without contacting a VM or requiring a
  destination.
- `--install` explicitly selects the default installation mode.

The source snapshot contains the current tracked/non-ignored source, including
uncommitted changes. Private helpers and build output are excluded. This is a
development-testing workflow; the clean-HEAD release scripts remain separate.
Artifacts, metadata and logs are retained under `build-arch-vm/package.*`.
Containers are removed after use and downloaded packages are cached on the host.

The builder compiles and ships every adapter, including both KIO and GIO folder-size
modules. The fresh runtime check verifies that Qt Multimedia, KSyntaxHighlighting,
KF6 WindowSystem, KIO, CoreAddons and antiword are absent, while baseline previews,
missing-reader recovery and GIO folder measurement work. It temporarily hides the
GIO module to verify unavailable folder-size fallback too. KIO and
CoreAddons remain optional package dependencies. The test VM's existing
optional libraries are preserved, including any required by its desktop.

The package and installer are staged under `~/Dzurkel-packages/package.*`, with a
small source file and silent WAV for manual tests. Installation runs `pacman -U`,
which resolves required runtime dependencies. It does not request optional
packages, bypass dependency checks or enable repositories. If the VM needs a full
Arch upgrade, review that separately; the helper does not upgrade the VM's whole
system automatically. See the [pacman manual](https://man.archlinux.org/man/pacman.8.en).

The installer backs up an old user-local executable and application launchers
under `~/.local/share/dzurkel-package-backups` after package installation succeeds.
A user-local forwarding script preserves shortcuts that reference
`~/.local/bin/dzurkel`; it launches `/usr/bin/dzurkel`. Preferences and shortcut
configuration are preserved. The host's installed application is unchanged.

## Manual checks

1. Open `/usr/bin/dzurkel --settings` in the VM and inspect Libraries.
2. Open the staged `library-check.wav` while Qt Multimedia is absent. Verify the
   explanation and Open Settings action. A genuine legacy `.doc` file similarly
   exercises antiword; `.docx` uses the built-in ZIP/XML reader.
3. Copy the row's installation command, inspect it, and run it yourself in the
   VM terminal. Refresh status and reopen the file.
4. After installing highlighting, toggle its checkbox and reopen
   `library-check.cpp`. Plain text remains usable in either state.
5. Restart Dzurkel after removing/replacing a loaded backend. Use snapshots for
   repeatability; never force-remove libraries required by the desktop.

The baseline CMake preset omits adapters and serves a different regression
purpose. A missing adapter cannot be created by installing its backend library.
