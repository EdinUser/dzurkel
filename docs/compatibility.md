# Location compatibility and experimental support

Content rendering and resource access are separate: image/PDF/text readers do not
implement network protocols. Remote locations use the desktop's KIO-FUSE backend
and the same bounded resource-preparation helper as local files.

| Location | Default | Evidence / limitations |
| --- | --- | --- |
| Local paths and file URLs | Enabled | Automated and native Fedora KDE Wayland checks. |
| Mounted network shares | Enabled as filesystem paths | User confirmed CIFS and mounted S3 workflows; other mount types and failure cases have not all been validated. |
| Direct SMB URLs | Enabled | Automated resource lifecycle tests, real native PDF/JPEG checks and user confirmation on Fedora KDE. |
| Direct SFTP URLs | Disabled; experimental opt-in | Automated URL/policy/fake-backend tests pass; user confirmed the normal workflow on their Fedora KDE SFTP setup. Broader server/authentication/failure-case coverage remains incomplete. |
| Other direct schemes | Disabled | Candidates require separate investigation and validation. |

An implemented route is not necessarily field-confirmed. Tests using a fake
KIO-FUSE service establish request handling and lifecycle behavior, not real SSH
authentication, server interoperability or transport performance. A successful
test on one deployment does not establish universal compatibility.

## Trying experimental SFTP

Open Settings from a local preview and check **Experimental SFTP previews** under
**File locations**. It is off by default and can be turned off again there.
Opening Settings alone does not change the setting. The per-user preference is
stored in `~/.config/dzurkel/dzurkel.ini` (or the corresponding `XDG_CONFIG_HOME`).
It applies to new CLI, D-Bus and Dolphin requests without restarting Dzurkel.
Disabling it does not forcibly interrupt an already accepted preview.

First open the SFTP location in Dolphin, verify the host key and authenticate
there. Use the same username-qualified URL, for example:

```bash
dzurkel --show 'sftp://user@server:2222/absolute/path/photo.jpg'
```

SFTP accepts an optional username and port (1–65535); its path must be absolute.
Embedded passwords, queries, fragments and NUL characters are rejected. Encode
literal filename characters such as `#` and `%`. Host-key verification and
authentication remain the desktop backend's responsibility; Dzurkel does not
disable verification or read mount/SSH credential files itself.

KIO-FUSE and the SFTP worker must be installed. Fedora's `kio-fuse` and
`kio-extras` provide the optional runtime route; no new KIO build dependency is
introduced. The Dolphin menu lists file, SMB and SFTP locations, but SFTP requests
are rejected with opt-in guidance until this preference is enabled. Rejected
requests preserve the current preview. Other schemes remain disabled even when
experimental SFTP is enabled.

Image/PDF source limits, text truncation, preparation timeout, cancellation and
cleanup follow [the existing resource policy](previews.md#filesystem-policy).
Remote paths retain their identity in the window and D-Bus FileUrl property.

## Controlled real SFTP check

After building, an optional Linux test harness can exercise real OpenSSH and
KIO-FUSE without editing personal SSH files or system services:

```bash
python3 tests/integration/SftpSmoke.py --build-dir build
```

It requires bubblewrap, OpenSSH server/client tools, D-Bus utilities, KIO-FUSE,
the SFTP worker, working user namespaces and permission to create a FUSE mount.
It creates a temporary loopback-only read-only SFTP server, disposable keys and
known-host data, a private session bus and a mount namespace with the real home
hidden. Temporary fixtures/configuration are cleaned up afterward. The HOME
environment variable is not changed. It is not part of default CTest.

The intended checks cover default denial, image/PDF/Unicode text previews,
running-service opt-out, an unavailable endpoint and cleanup. Readiness timings
compare local and SFTP previews; they are not first-painted-frame measurements
or a general performance guarantee. A successful run still does not cover every
authentication method, server, desktop or interrupted-transfer scenario.

On the development environment, OpenSSH starts but creation of the private FUSE
mount returns `Operation not permitted`. The harness reports `unavailable` and
exit status 77; this is **not a real SFTP success**. A machine permitting that
mount is needed to finish this check before making transport/performance claims.

## Reporting field results

Record Pass / Fail / Not tested for small image/PDF/text previews, Unicode names,
rapid replacement, dismissal while loading, oversized/unsupported content,
reauthentication and unavailable connections where safe. Include distribution,
desktop/session, Dzurkel revision, Qt/KIO-FUSE and server versions if known.
Report delays, errors and cleanup behavior; never include passwords, private keys
or credential files. Test fixtures should contain no personal material.

New routes stay experimental until their controlled and field evidence and
remaining limitations have been reviewed. Local polishing can proceed while
broader compatibility evidence accumulates.

Field update on 2026-10-17: the user confirmed the installed experimental SFTP
workflow and previews through a mounted S3 volume. This provides real deployment
evidence alongside the automated tests. Specific formats and failure scenarios
were not reported, and mounted S3 access does not establish direct `s3://` URL
support. SFTP remains explicitly experimental and disabled by default.
