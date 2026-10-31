#!/bin/sh
# provision.d/app-user.sh <staging-dataset-path>
#
# Creates the `app` user in the template.
set -eu

root="${1:?usage: app-user.sh <staging-dataset-path>}"

# F51: uid 1000 must be free.
if pw -R "$root" usershow -u 1000 >/dev/null 2>&1; then
    printf 'parish: uid 1000 is already in use in the template.\n' >&2
    printf '        Cannot create the "app" user. Check base.txz for a\n' >&2
    printf '        conflicting account.\n' >&2
    exit 1
fi

# Create the user with a home directory inside the template dataset.
pw -R "$root" useradd app -u 1000 -d /home/app -m -s /bin/sh \
    -c "Compartment application user"

# Lock interactive login (only `jexec -U app` reaches it).
pw -R "$root" lock app

# Verify.
pw -R "$root" usershow app >/dev/null
test -d "$root/home/app"
test "$(stat -f '%Su' "$root/home/app")" = "app"