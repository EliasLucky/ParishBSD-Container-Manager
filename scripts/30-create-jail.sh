#!/bin/sh
# 30-create-jail.sh - create a jail instance from a template or legacy personality.
#
# Usage: sh 30-create-jail.sh <name> --template=<template>
#        sh 30-create-jail.sh <name> <personality>          (legacy compat)
#
# Templates live in $repo/templates/<name>/ and may contain:
#   template.conf    (required, T1 C.2 schema)
#   jail.conf.tmpl   (optional; built-in default used when absent)
#   fstab.tmpl       (optional; built-in default used when absent)
#   provision.sh     (optional)
set -eu

PARISHBSD_CACHE="${PARISHBSD_CACHE:-/var/cache/parishbsd}"
PARISHBSD_FORCE="${PARISHBSD_FORCE:-0}"

log() { printf '    %s\n' "$*"; }
row() { printf '    %-22s %s\n' "$1" "$2"; }
err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# --- Arguments ---
name="${1:?usage: 30-create-jail.sh <name> --template=<t>|<personality>}"
_template_mode=0
template=""
personality=""
case "${2:-}" in
    --template=*) _template_mode=1; template="${2#--template=}" ;;
    "")           err "usage: 30-create-jail.sh <name> --template=<t>|<personality>" ;;
    *)            personality="$2" ;;
esac

# --- Paths ---
repo="${PARISHBSD_REPO:-/usr/local/share/parishbsd}"
arch="$(uname -m)"
version="$(freebsd-version -u | awk -F'-p' '{print $1}')"
cached_txz="$PARISHBSD_CACHE/base-$version-$arch.txz"
url="https://download.freebsd.org/releases/$arch/$version/base.txz"

# --- Sanity ---
[ "$(id -u)" -eq 0 ] || err "must be run as root"
command -v fetch >/dev/null 2>&1 || command -v curl >/dev/null 2>&1 \
    || err "fetch/curl not found"
command -v tar >/dev/null 2>&1 || err "tar not found"

# --- Load source: template or legacy personality ---
template_dir=""
personality_dir=""

if [ "$_template_mode" -eq 1 ]; then
    if [ -d "/var/db/parishbsd/templates/$template" ]; then
        template_dir="/var/db/parishbsd/templates/$template"
    elif [ -d "/usr/local/share/parishbsd/templates/$template" ]; then
        template_dir="/usr/local/share/parishbsd/templates/$template"
    else
        err "template '$template' not found"
    fi
    [ -f "$template_dir/template.conf" ] || err "template '$template' missing template.conf"

    current_base=$(grep '^current_base=' "$template_dir/template.conf" | cut -d= -f2 | sed 's/^@//')
    [ -n "$current_base" ] || err "template '$template' has no current_base (is it built?)"
    TEMPLATE_SNAPSHOT="zroot/jails/templates/$template@$current_base"

    case "$template" in
        vault-*) TIER="vault" ;;
        *)       TIER="app"   ;;
    esac

    # Fix 2: Anchor the T-xpra check to the clusters= line
    if grep -qE '^clusters=.*T-xpra(,|$)' "$template_dir/template.conf" 2>/dev/null; then
        NEEDS_XPRA_DIR="yes"
    else
        NEEDS_XPRA_DIR="no"
    fi

    DATA_MOUNT="/var/db/data"
    DATA_SOURCE="/zroot/vault-data"
    PROVISION="provision.sh"
else
    # Legacy personality mode (one-way compat; translates to internal names)
    personality_dir="$repo/personalities/$personality"
    [ -d "$personality_dir" ] || err "personality '$personality' not found at $personality_dir"
    # shellcheck disable=SC1091
    . "$personality_dir/personality.conf" || err "failed to load personality.conf"

    TIER="${PERSONALITY_TIER:-app}"
    TEMPLATE_SNAPSHOT="${PERSONALITY_TEMPLATE:-}"
    NEEDS_XPRA_DIR="${PERSONALITY_NEEDS_XPRA_DIR:-no}"
    DATA_MOUNT="${PERSONALITY_DATA_MOUNT:-/var/db/data}"
    DATA_SOURCE="${PERSONALITY_DATA_SOURCE:-/zroot/vault-data}"
    PROVISION="${PERSONALITY_PROVISION:-}"
fi

jailroot="/usr/local/jails/$TIER/$name"

# --- Atomic write ---
write_atomic() {
    _wa_target="$1"; _wa_mode="$2"; _wa_owner="$3"
    _wa_tmp="$_wa_target.tmp.$$"
    cat > "$_wa_tmp" || { rm -f "$_wa_tmp"; err "cannot write $_wa_tmp"; }
    chmod "$_wa_mode" "$_wa_tmp" || { rm -f "$_wa_tmp"; err "cannot chmod $_wa_tmp"; }
    chown "$_wa_owner" "$_wa_tmp" || { rm -f "$_wa_tmp"; err "cannot chown $_wa_tmp"; }
    sync
    mv "$_wa_tmp" "$_wa_target" || { rm -f "$_wa_tmp"; err "cannot rename $_wa_tmp"; }
    trap - EXIT
}

# --- Template rendering with variable substitution ---
# Handles both retired and current variable names for transition safety.
render_tmpl() {
    _rt_src="$1"
    _rt_dst="$2"
    _rt_mode="$3"
    _rt_owner="$4"
    sed -e "s/\${name}/$name/g" \
        -e "s/\${tier}/$TIER/g" \
        -e "s/\${epair}/$epair/g" \
        -e "s/\${ip}/$ip/g" \
        -e "s|\${DATA_MOUNT}|$DATA_MOUNT|g" \
        -e "s|\${DATA_SOURCE}|$DATA_SOURCE|g" \
        -e "s|\${PERSONALITY_DATA_MOUNT}|$DATA_MOUNT|g" \
        -e "s|\${PERSONALITY_DATA_SOURCE}|$DATA_SOURCE|g" \
        "$_rt_src" | write_atomic "$_rt_dst" "$_rt_mode" "$_rt_owner"
}

ensure_cache() {
	mkdir -p "$PARISHBSD_CACHE"
	if [ -f "$cached_txz" ] && [ "$PARISHBSD_FORCE" != "1" ]; then
		row "base.txz" "using cache"
		return 0
	fi
	log "Downloading base.txz for $version/$arch"
	tmp="$cached_txz.part"
	rm -f "$tmp"
	if command -v fetch >/dev/null 2>&1; then
		fetch -o "$tmp" "$url" || err "download failed"
	else
		curl -L -o "$tmp" "$url" || err "download failed"
	fi
	mv "$tmp" "$cached_txz"
	row "base.txz" "cached ($(du -h "$cached_txz" | awk '{print $1}'))"
}

create_jailroot() {
	if [ -d "$jailroot" ] && [ -e "$jailroot/bin/sh" ]; then
		row "jail root" "already exists - leaving alone"
		return 0
	fi

	if [ -n "$TEMPLATE_SNAPSHOT" ] && zfs list "$TEMPLATE_SNAPSHOT" >/dev/null 2>&1; then
		pool="${TEMPLATE_SNAPSHOT%%/*}"
		container_ds="${pool}/jails/containers/$name"
		containers_parent="${container_ds%/*}"

		# Ensure parent dataset exists (T4b-B8)
		zfs list "$containers_parent" >/dev/null 2>&1 || \
			zfs create -p "$containers_parent" || err "cannot create $containers_parent"

		if zfs list "$container_ds" >/dev/null 2>&1; then
			err "ZFS dataset $container_ds already exists - destroy it first"
		fi

		log "Cloning template"
		zfs clone "$TEMPLATE_SNAPSHOT" "$container_ds" || err "zfs clone failed"
		zfs set mountpoint="$jailroot" "$container_ds" || err "cannot set mountpoint"
		row "jail root" "$jailroot (cloned)"
	else
		log "No ZFS template - extracting base.txz"
		ensure_cache
		mkdir -p "$jailroot"
		tar -xpf "$cached_txz" -C "$jailroot" || err "extraction failed"
		rm -f "$jailroot/boot/kernel/kernel" 2>/dev/null || true

		if [ ! -s "$jailroot/etc/resolv.conf" ]; then
			cat > "$jailroot/etc/resolv.conf" <<'EOT'
nameserver 1.1.1.1
nameserver 9.9.9.9
EOT
		fi
		row "jail root" "$jailroot (extracted)"
	fi

	exchange_subdir="/var/parishbsd/exchange/$name"
	install -d "$exchange_subdir"
	chmod 0770 "$exchange_subdir"
	install -d "$jailroot/exchange"
}

install_config() {
	epair_num=$(( $(echo "$name" | cksum | awk '{print $1}') % 1000 ))
	epair="epair${epair_num}"
	ip="10.0.0.$(( epair_num % 200 + 2 ))"

	# --- jail.conf ---
	_jc_src=""
	if [ -n "$template_dir" ] && [ -f "$template_dir/jail.conf.tmpl" ]; then
		_jc_src="$template_dir/jail.conf.tmpl"
	elif [ -n "$personality_dir" ] && [ -f "$personality_dir/jail.conf.tmpl" ]; then
		_jc_src="$personality_dir/jail.conf.tmpl"
	fi

	install -d /etc/jail.conf.d
	if [ -n "$_jc_src" ]; then
		render_tmpl "$_jc_src" "/etc/jail.conf.d/$name.conf" 0644 root:wheel
		row "jail.conf" "$name.conf (from template, epair=$epair ip=$ip)"
	else
		_jc_tmp=$(mktemp)
		cat > "$_jc_tmp" <<'EOT'
${name} {
	host.hostname = "${name}.parishbsd.local";
	path = "/usr/local/jails/${tier}/${name}";

	allow.raw_sockets = 0;
	allow.chflags     = 0;
	allow.mount       = 0;
	allow.quotas      = 0;
	allow.socket_af   = 0;
	allow.sysvipc     = 0;
	devfs_ruleset     = 4;
	enforce_statfs    = 2;
	exec.clean        = 1;
	securelevel       = 1;

	vnet;
	vnet.interface = "${epair}b";
	exec.prestart = "/sbin/ifconfig ${epair} create up; /sbin/ifconfig bridge0 addm ${epair}a up";
	exec.start = "/sbin/ifconfig ${epair}b ${ip}/24 up; /sbin/route add default 10.0.0.1; /bin/sh /etc/rc";
	exec.poststart = "/bin/chmod 0600 /var/log/jail_${name}_console.log 2>/dev/null || true";
	exec.poststop = "/sbin/ifconfig bridge0 deletem ${epair}a; /sbin/ifconfig ${epair}a destroy";

	mount.fstab = "/etc/fstab.${name}";

	exec.consolelog = "/var/log/jail_${name}_console.log";
	persist;
}
EOT
		render_tmpl "$_jc_tmp" "/etc/jail.conf.d/$name.conf" 0644 root:wheel
		rm -f "$_jc_tmp"
		row "jail.conf" "$name.conf (built-in default, epair=$epair ip=$ip)"
	fi

	# --- fstab ---
	_fs_src=""
	if [ -n "$template_dir" ] && [ -f "$template_dir/fstab.tmpl" ]; then
		_fs_src="$template_dir/fstab.tmpl"
	elif [ -n "$personality_dir" ] && [ -f "$personality_dir/fstab.tmpl" ]; then
		_fs_src="$personality_dir/fstab.tmpl"
	fi

	if [ -n "$_fs_src" ]; then
		render_tmpl "$_fs_src" "/etc/fstab.$name" 0644 root:wheel
		row "fstab" "$name (from template)"
	else
		_fs_tmp=$(mktemp)
		case "$TIER" in
			app)
				if [ "$NEEDS_XPRA_DIR" = "yes" ]; then
					cat > "$_fs_tmp" <<'EOT'
/var/run/xpra/${name}              /usr/local/jails/${tier}/${name}/xpra      nullfs  rw,nosuid,noexec  0 0
/var/parishbsd/exchange/${name}    /usr/local/jails/${tier}/${name}/exchange  nullfs  rw,nosuid,noexec  0 0
EOT
				else
					cat > "$_fs_tmp" <<'EOT'
/var/parishbsd/exchange/${name}    /usr/local/jails/${tier}/${name}/exchange  nullfs  rw,nosuid,noexec  0 0
EOT
				fi
				;;
			vault)
				cat > "$_fs_tmp" <<'EOT'
/var/parishbsd/exchange/${name}    /usr/local/jails/${tier}/${name}/exchange  nullfs  rw,nosuid,noexec  0 0
${DATA_SOURCE}/${name}    /usr/local/jails/${tier}/${name}${DATA_MOUNT}    nullfs    rw,noexec,nosuid    0    0
EOT
				;;
		esac
		render_tmpl "$_fs_tmp" "/etc/fstab.$name" 0644 root:wheel
		rm -f "$_fs_tmp"
		row "fstab" "$name (built-in default, tier=$TIER)"
	fi

	# --- provision.sh ---
	if [ -n "$PROVISION" ]; then
		_prov_base=""
		if [ -n "$template_dir" ] && [ -f "$template_dir/$PROVISION" ]; then
			_prov_base="$template_dir/$PROVISION"
		elif [ -n "$personality_dir" ] && [ -f "$personality_dir/$PROVISION" ]; then
			_prov_base="$personality_dir/$PROVISION"
		fi

		if [ -n "$_prov_base" ]; then
			install -d "$jailroot/usr/local/etc/parishbsd"
			install -m 0755 "$_prov_base" "$jailroot/usr/local/etc/parishbsd/provision.sh"
			row "provision.sh" "installed"

			rclocal="$jailroot/etc/rc.local"
			if [ ! -f "$rclocal" ] || ! grep -q 'parishbsd/provision.sh' "$rclocal" 2>/dev/null; then
				cat > "$rclocal" <<'RCEOF'
#!/bin/sh
if [ -x /usr/local/etc/parishbsd/provision.sh ]; then
    /usr/local/etc/parishbsd/provision.sh
fi
exit 0
RCEOF
				chmod 0755 "$rclocal"
				row "rc.local" "provisioning hook installed"
			fi
		fi
	fi

	# --- In-jail metadata (T1 A.3 layout, read by ContainerBackend) ---
	install -d "$jailroot/usr/local/etc/parishbsd"
	# Fix 1: Write SOURCE_TEMPLATE= in template mode, PERSONALITY= in legacy mode
	if [ "$_template_mode" -eq 1 ]; then
		write_atomic "$jailroot/usr/local/etc/parishbsd/metadata" 0644 root:wheel <<EOT
SOURCE_TEMPLATE=$template
TIER=$TIER
CREATED=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOT
	else
		write_atomic "$jailroot/usr/local/etc/parishbsd/metadata" 0644 root:wheel <<EOT
PERSONALITY=$personality
TIER=$TIER
CREATED=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOT
	fi
	chmod 0644 "$jailroot/usr/local/etc/parishbsd/metadata"
	row "metadata" "in-jail ($TIER)"
}

create_mountpoints() {
	if [ "$NEEDS_XPRA_DIR" = "yes" ]; then
		mkdir -p "/var/run/xpra/$name" || err "cannot create /var/run/xpra/$name"
		chmod 0700 "/var/run/xpra/$name" || err "cannot chmod 0700 /var/run/xpra/$name"
		mkdir -p "$jailroot/xpra" || err "cannot create $jailroot/xpra"
		chmod 0700 "$jailroot/xpra" || err "cannot chmod 0700 $jailroot/xpra"
		row "xpra dirs" "/var/run/xpra/$name"
	fi

	if [ "$TIER" = "vault" ]; then
		mkdir -p "$jailroot$DATA_MOUNT"
		row "data dir" "$jailroot$DATA_MOUNT"
	fi
}

clean_orphan_epairs() {
	mkdir -p /var/run/parishbsd 2>/dev/null || true
	chmod 0700 /var/run/parishbsd 2>/dev/null || true
	if ! { exec 9> /var/run/parishbsd/service.lock; } 2>/dev/null; then
		row "orphan epairs" "skipped (cannot open service lock)"
		return 0
	fi
	if ! lockf -w 30 9; then
		exec 9>&-
		row "orphan epairs" "skipped (lock busy)"
		return 0
	fi

	cleaned=0
	for ea in $(ifconfig -l | tr ' ' '\n' | grep '^epair[0-9]*a$' 2>/dev/null); do
		if ! ifconfig bridge0 2>/dev/null | grep -q "member: $ea"; then
			ifconfig "$ea" destroy 2>/dev/null && cleaned=$((cleaned + 1))
		fi
	done

	exec 9>&-
	[ "$cleaned" -gt 0 ] && row "orphan epairs" "destroyed $cleaned"
	return 0
}

install_packages() {
	# Template mode: packages are baked into @base
	[ "$_template_mode" -eq 1 ] && return 0
	[ -n "${PERSONALITY_PACKAGES:-}" ] || return 0

	if ! jls name 2>/dev/null | grep -qxF "$name"; then
		row "packages" "jail not running - skipped"
		return 0
	fi

	log "Installing packages: $PERSONALITY_PACKAGES"
	jexec "$name" env ASSUME_ALWAYS_YES=YES pkg bootstrap 2>/dev/null || true
	jexec "$name" env ASSUME_ALWAYS_YES=YES pkg install -y $PERSONALITY_PACKAGES \
		|| row "packages" "install failed - run manually later"
}

create_jailroot
install_config
create_mountpoints
clean_orphan_epairs
install_packages

if [ "$_template_mode" -eq 1 ]; then
	row "template" "$template"
else
	row "personality" "$personality"
fi
row "tier" "$TIER"
log "Jail '$name' is ready."
log "Start it with: service jail start $name"