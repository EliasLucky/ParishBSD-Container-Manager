#!/bin/sh
# provision.d/app-user.sh <staging-dataset-path>
set -eu

root="${1:?usage: app-user.sh <staging-dataset-path>}"

# Ensure /home exists in the template (FreeBSD base.txz doesn't have it)
mkdir -p "$root/home"

# F51: uid 1000 must be free.
if pw -R "$root" usershow -u 1000 >/dev/null 2>&1; then
    printf 'parish: uid 1000 is already in use in the template.\n' >&2
    printf '        Cannot create the "app" user. Check base.txz for a\n' >&2
    printf '        conflicting account.\n' >&2
    exit 1
fi

# Create the user with a home directory inside the template dataset.
if ! pw -R "$root" useradd app -u 1000 -d /home/app -m -s /bin/sh \
    -c "Compartment application user"; then
    printf 'parish: pw useradd failed.\n' >&2
    exit 1
fi

# Lock interactive login (only `jexec -U app` reaches it).
pw -R "$root" lock app

# Verify.
pw -R "$root" usershow app >/dev/null
test -d "$root/home/app"
# Use numerical UID (%u) because %Su resolves against the HOST's passwd
test "$(stat -f '%u' "$root/home/app")" = "1000"