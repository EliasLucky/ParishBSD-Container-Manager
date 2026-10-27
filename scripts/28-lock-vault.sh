#!/bin/sh
# 28-lock-vault.sh - stop a vault's jail and lock its data.
#
# Usage: sh 28-lock-vault.sh <vault-name>
#
# Stops the jail (if running), unmounts the encrypted dataset, and
# unloads its key. After this, the vault's data is unreadable until
# 27-unlock-vault.sh is run again.
#
# Safe to run multiple times.
set -eu

name="${1:?usage: 28-lock-vault.sh <vault-name>}"

log() { printf '    %s\n' "$*"; }
row() { printf '    %-22s %s\n' "$1" "$2"; }
err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || err "must be run as root"

ds="zroot/vault-data/$name"

# --- Stop the jail if running ---
if jls name 2>/dev/null | grep -qx "$name"; then
	log "Stopping jail $name"
	service jail stop "$name" || row "jail" "stop returned non-zero"
	row "jail" "stopped"
else
	row "jail" "not running"
fi

# --- Unmount and unload the key ---
#
# use `zfs get mounted`, not `mount | grep`
# (regex on the jail name matches other mount points if the name
# contains a dot).
if zfs list "$ds" >/dev/null 2>&1; then
	mounted_state="$(zfs get -H -o value mounted "$ds" 2>/dev/null || echo unknown)"
	case "$mounted_state" in
		yes)
			zfs unmount "$ds" || {
				echo "ERROR: zfs unmount failed for $ds" >&2
				echo "The vault data may still be accessible." >&2
				echo "Refusing to proceed with locking." >&2
				echo "Manually investigate:" >&2
				echo "  zfs get mounted $ds" >&2
				echo "  mount | grep -F $ds" >&2
				exit 1
			}
			row "mount" "unmounted"
			;;
		no)
			row "mount" "already unmounted"
			;;
		*)
			echo "ERROR: cannot determine mount state for $ds" >&2
			echo "  (zfs get mounted returned: '$mounted_state')" >&2
			echo "Refusing to proceed with locking." >&2
			exit 1
			;;
	esac

	keystatus="$(zfs get -H -o value keystatus "$ds" 2>/dev/null || echo 'unknown')"
	case "$keystatus" in
		available)
			zfs unload-key "$ds" || {
				echo "ERROR: zfs unload-key failed for $ds" >&2
				echo "The encryption key is still loaded. The vault" >&2
				echo "is NOT locked." >&2
				echo "Manual recovery:" >&2
				echo "  zfs unload-key -r $ds" >&2
				exit 1
			}
			row "key" "unloaded"
			;;
		unavailable)
			row "key" "already unloaded"
			;;
		*)
			echo "ERROR: cannot determine key state for $ds" >&2
			echo "  (zfs get keystatus returned: '$keystatus')" >&2
			echo "Refusing to proceed with locking." >&2
			exit 1
			;;
	esac
else
	row "dataset" "$ds does not exist"
fi

log "Vault '$name' is locked."