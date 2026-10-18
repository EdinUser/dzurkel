#!/usr/bin/env bash
# Run inside an isolated package-test container, never on the live desktop.
set -euo pipefail
case "${1:-}" in
    installed)
        test "$(/usr/bin/dzurkel --version)" = 'dzurkel 0.6.0'
        test -x /usr/bin/dzurkel
        test -f /usr/share/kio/servicemenus/dzurkel.desktop
        desktop-file-validate /usr/share/applications/dzurkel{,-settings}.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --show -- %u' /usr/share/applications/dzurkel.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --settings' /usr/share/applications/dzurkel-settings.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --show -- %u' /usr/share/kio/servicemenus/dzurkel.desktop
        runuser -u preview-user -- dbus-run-session -- python3 tests/integration/InstallTest.py --smoke-launchers /usr/bin/dzurkel
        ;;
    removed)
        for path in /usr/bin/dzurkel /usr/share/applications/dzurkel{,-settings}.desktop /usr/share/kio/servicemenus/dzurkel.desktop; do
            test ! -e "$path"
        done
        ;;
    *) exit 2 ;;
esac
