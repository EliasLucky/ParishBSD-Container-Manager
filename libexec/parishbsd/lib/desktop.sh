#!/bin/sh
# libexec/parishbsd/lib/desktop.sh
#
# Locate the .desktop file a package installed inside a jail.
#
# REQUIRES (sourced by caller, in this order):
#   validate.sh     validate_package_name (used indirectly by callers)
#   desktop.sh      this file
#
# This is the single implementation used by all callers that need to
# find a package's desktop file:
#   - parish-apps _locate_desktop_argv_and_version
#   - parish-app-create-launcher
#
# Two-stage search:
#   1. Fixed-path: <pkg>.desktop under /usr/local/share/applications
#      or /usr/share/applications.
#   2. Owner-scan: iterate all .desktop files, extract the Exec=
#      binary, ask pkg which which package owns it. First match wins.
#
# Stage 2 handles packages whose .desktop is not named <pkg>.desktop
# (e.g., firefox-esr.desktop for pkg firefox). It is slower (one
# jexec sh -c per call, one pkg which per .desktop file) but it
# runs at most once per (package, install).

# _find_pkg_desktop <jail> <pkg_name>
# Echo the path to the package's .desktop inside the jail, or empty.
# Never fails; empty output means "not found".
_find_pkg_desktop() {
    _j="$1"
    _p="$2"

    # Stage 1: fixed-path.
    for _d in /usr/local/share/applications /usr/share/applications; do
        _c="$_d/$_p.desktop"
        if jexec "$_j" test -f "$_c" 2>/dev/null; then
            printf '%s' "$_c"
            return 0
        fi
    done

    # Stage 2: owner-scan. Extracted from parish-app-create-launcher
    jexec "$_j" sh -c '
        pkg="$1"
        for d in /usr/local/share/applications /usr/share/applications; do
            for df in "$d"/*.desktop; do
                [ -f "$df" ] || continue
                bin=$(grep "^Exec=" "$df" | head -1 | cut -d= -f2 | awk "{print \$1}")
                [ -n "$bin" ] || continue
                owner=$(pkg which -q "$bin" 2>/dev/null | awk "{print \$1}")
                [ "$owner" = "$pkg" ] && echo "$df" && exit 0
            done
        done
    ' _ "$_p" 2>/dev/null | head -1
}