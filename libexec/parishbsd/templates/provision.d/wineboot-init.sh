#!/bin/sh
# provision.d/wineboot-init.sh <staging-dataset-path>
#
# Initializes the wine prefix at build time so the first jail launch does not
# pay the wineboot cost.
set -eu

root="${1:?usage: wineboot-init.sh <staging-dataset-path>}"
prefix="$root/root/.wine"

# Already initialized? Skip.
if [ -d "$prefix/drive_c" ]; then
    exit 0
fi

# Initialize. wineboot spawns a background process and returns before the
# prefix is complete; poll for the drive_c directory with a 60s bound.
WINEPREFIX="$prefix" wineboot --init >/dev/null 2>&1 || true

i=0
while [ "$i" -lt 120 ]; do
    if [ -d "$prefix/drive_c" ]; then
        exit 0
    fi
    sleep 0.5
    i=$((i + 1))
done

printf 'parish: wineboot --init did not complete within 60s; the wine\n' >&2
printf '        prefix may be incomplete. First launch will retry.\n' >&2
exit 1