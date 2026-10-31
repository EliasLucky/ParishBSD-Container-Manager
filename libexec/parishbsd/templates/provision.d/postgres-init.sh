#!/bin/sh
# provision.d/postgres-init.sh <staging-dataset-path>
#
# Initializes the PostgreSQL data directory at build time. initdb refuses
# to run as root; run as postgres via su -m, with ownership set BEFORE initdb.
set -eu

root="${1:?usage: postgres-init.sh <staging-dataset-path>}"

# Locate the installed postgres binary. pkg search output determines the NN;
# the wrapper cluster installed postgresqlNN-server. Find the newest.
pg_bin=""
for d in "$root"/usr/local/bin/postgres; do
    if [ -x "$d" ]; then pg_bin="$d"; break; fi
done
[ -n "$pg_bin" ] || { printf 'parish: postgres binary not found in %s\n' "$root" >&2; exit 1; }

data="$root/var/db/postgres/data"

# Create data dir with postgres ownership BEFORE initdb.
mkdir -p "$data"
# Translate uid/gid into the template's /etc/passwd (postgres user created by
# pkg postgresqlNN-server install).
if ! pw -R "$root" usershow postgres >/dev/null 2>&1; then
    printf 'parish: postgres user does not exist in template; pkg install\n' >&2
    printf '        may have failed. See log.\n' >&2
    exit 1
fi
uid=$(pw -R "$root" usershow postgres | cut -d: -f3)
gid=$(pw -R "$root" usershow postgres | cut -d: -f4)
chown "$uid:$gid" "$data"
chmod 0700 "$data"

# initdb refuses to run as root; use su -m to preserve env.
su -m postgres -c "initdb -D '$data'" >/dev/null
su -m postgres -c "pg_ctl -D '$data' start" >/dev/null
sleep 1
su -m postgres -c "pg_ctl -D '$data' stop" >/dev/null

test -f "$data/PG_VERSION"