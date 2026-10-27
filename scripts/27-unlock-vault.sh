#!/bin/sh
# 27-unlock-vault.sh - unlock a vault and start its jail.
#
# Usage: sh 27-unlock-vault.sh <vault-name>
#
# Loads the ZFS key (prompting if needed), mounts the encrypted
# dataset, and starts the jail. If the vault is already unlocked and
# its jail is already running, this is a no-op.
#
# Safe to run multiple times.
set -eu

name="${1:?usage: 27-unlock-vault.sh <vault-name>}"

log() { printf '    %s\n' "$*"; }
row() { printf '    %-22s %s\n' "$1" "$2"; }
err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || err "must be run as root"

ds="zroot/vault-data/$name"
mnt="/zroot/vault-data/$name"

zfs list "$ds" >/dev/null 2>&1 || err "no encrypted dataset $ds"

# --- Load the key if it isn't already loaded ---
keystatus="$(zfs get -H -o value keystatus "$ds" 2>/dev/null || echo 'unknown')"
case "$keystatus" in
	available)
		row "key" "already loaded"
		;;
	unavailable)
		log "Loading key for $ds (enter passphrase when prompted)"
		zfs load-key "$ds" || err "failed to load key (wrong passphrase?)"
		row "key" "loaded"
		;;
	*)
		err "unexpected keystatus for $ds: $keystatus"
		;;
esac

# --- Mount if not already mounted ---
# zfs get mounted instead of `mount | grep`.
mounted_state="$(zfs get -H -o value mounted "$ds" 2>/dev/null || echo unknown)"
case "$mounted_state" in
	yes)
		row "mount" "already mounted"
		;;
	no)
		zfs mount "$ds" || err "cannot mount $ds"
		row "mount" "$mnt"
		;;
	*)
		err "cannot determine mount state of $ds: $mounted_state"
		;;
esac

# --- Start the jail if not already running ---
if jls name 2>/dev/null | grep -qx "$name"; then
	row "jail" "already running"
else
	log "Starting jail $name"

	# spec + extension:
	#   Step 1: capture exit code and stderr from service jail start.
	#   Step 2: non-zero exit → print captured stderr verbatim, exit 1.
	#   Step 3 (extension, ruled keep by reviewer round 2): zero exit
	#           but stderr contains "cannot start" or "error" →
	#           print captured stderr, exit 1. Catches the observed
	#           case (exit 0, error on stderr).
	#   Step 4: zero exit, no error signal → state check as
	#           post-condition; fail on state mismatch.
	#
	# temp file under the tool's own runtime
	# directory, not /tmp. The directory is root:wheel 0700,
	# created by respond_main or xpra-sup at runtime; mkdir -p
	# handles the first-use case.
	#
	# trap-based cleanup. The explicit rm -f
	# calls below cover the known paths; the trap covers any future
	# err call that skips explicit cleanup.
	_rundir=/var/run/parishbsd
	mkdir -p "$_rundir" 2>/dev/null || true
	[ -d "$_rundir" ] || err "cannot create runtime directory: $_rundir"
	# enforce 0700 to match the mode Guardian's
	# respond_main and xpra-sup create. mkdir -p at default umask
	# under pkexec (022) would create at 0755. The temp file inside
	# is 0600 (mktemp default), so this is defense in depth for the
	# directory itself. chmod 2>/dev/null is safe if the directory
	# was already created 0700 by respond_main or xpra-sup this boot.
	chmod 0700 "$_rundir" 2>/dev/null || true
	_service_err=$(mktemp "$_rundir/parish-unlock-err.XXXXXX") \
		|| err "cannot create temp file for service output"

	_cleanup_service_err() {
		[ -n "${_service_err:-}" ] && rm -f "$_service_err"
	}
	# EXIT trap only. The previous
	# `EXIT INT TERM` form traps SIGINT/SIGTERM into running the
	# cleanup and returning normally which replaces the default
	# abort action. The shell continues after Ctrl-C instead of
	# terminating. Fix: EXIT only, letting the shell's default
	# signal handling abort with cleanup on the way out.
	#
	# Cross-reference: parish-apps's _exchange_cleanup also uses
	# `trap _exchange_cleanup EXIT INT TERM`, but each handler there
	# removes the trap (`trap - EXIT INT TERM`) before returning.
	# The trap lifetime is bounded to the specific operation. Here
	# the trap must persist through err calls that exit, so EXIT-only
	# is the correct shape.
	trap _cleanup_service_err EXIT

	_service_rc=0
	service jail start "$name" 2>"$_service_err" || _service_rc=$?

	if [ "$_service_rc" -ne 0 ]; then
		if [ -s "$_service_err" ]; then
			echo "The container service reported an error while starting:" >&2
			sed 's/^/  /' "$_service_err" >&2
			echo "" >&2
		fi
		rm -f "$_service_err"
		err "failed to start jail $name (exit $_service_rc)"
	fi

	if [ -s "$_service_err" ] && grep -qiE 'cannot start|error' "$_service_err"; then
		echo "The container service reported an error while starting:" >&2
		sed 's/^/  /' "$_service_err" >&2
		echo "" >&2
		rm -f "$_service_err"
		echo "The container's state is not trustworthy." >&2
		echo "Do not use it until the cause is understood." >&2
		echo "" >&2
		echo "To investigate:" >&2
		echo "  service jail status $name" >&2
		echo "  tail /var/log/messages" >&2
		exit 1
	fi
	rm -f "$_service_err"

	if ! jls name 2>/dev/null | grep -qx "$name"; then
		err "jail '$name' did not start (state check failed) - check: service jail status $name"
	fi

	row "jail" "started"
fi

log "Vault '$name' is unlocked and running."