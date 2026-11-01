#!/bin/sh
# 30-create-jail.sh - create a jail instance from a template.
#
# Usage: sh 30-create-jail.sh <name> <template>
#
# Reads <template>/template.conf (state at /var/db/parishbsd/templates;
# .tmpl files may be at /var/db (user template) or /usr/local/share
# (shipped template) - the shipped path is used when the state path
# lacks a jail.conf.tmpl). Verifies state=ready and tier=app|vault.
# Clones zroot/jails/templates/<template>@<current_base> to
# zroot/jails/containers/<name>; sets mountpoint to
# /usr/local/jails/containers/<name>; renders jail.conf.d/<name>.conf
# and fstab.<name> from the template's .tmpl files; writes metadata
# with SOURCE_TEMPLATE=, TIER=, KIND=, CREATED=.
#
# Idempotent: existing dataset or existing populated jailroot causes
# refusal (nothing is destroyed; caller decides).
set -eu

name="${1:?usage: 30-create-jail.sh <name> <template>}"
template="${2:?usage: 30-create-jail.sh <name> <template>}"

log()  { printf '    %s\n' "$*"; }
row()  { printf '    %-22s %s\n' "$1" "$2"; }
err()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || err "must be run as root"

LIBDIR=/usr/local/libexec/parishbsd/lib
TEMPLATES_STATE=/var/db/parishbsd/templates
TEMPLATES_SHIPPED=/usr/local/share/parishbsd/templates
POOL="zroot"

. "$LIBDIR/validate.sh"
validate_jail_name "$name"        || exit 2
validate_template_name "$template" || exit 2

conf="$TEMPLATES_STATE/$template/template.conf"
[ -f "$conf" ]   || err "template '$template' not found at $conf"
[ ! -L "$conf" ] || err "template config is a symlink; refusing"

tmpl_dir="$TEMPLATES_STATE/$template"
if [ ! -f "$tmpl_dir/jail.conf.tmpl" ] && \
   [ -f "$TEMPLATES_SHIPPED/$template/jail.conf.tmpl" ]; then
    tmpl_dir="$TEMPLATES_SHIPPED/$template"
fi

read_field() {
    _v="$(grep -F "^$1=" "$conf" 2>/dev/null | head -1 | cut -d= -f2-)"
    printf '%s' "$_v"
}

state="$(read_field state)"
[ "$state" = "ready" ] || err "template '$template' is not ready (state='${state:-missing}')"

current_base="$(read_field current_base)"
case "$current_base" in
    @base-*) : ;;
    *) err "template '$template' has invalid current_base '$current_base'" ;;
esac

tier="$(read_field tier)"
case "$tier" in
    app|vault) : ;;
    '') err "template '$template' has no tier=; run: pkexec parishctl template reset $template" ;;
    *)  err "template '$template' has invalid tier '$tier'" ;;
esac

kind="$(read_field kind)"
case "$kind" in
    application|file-vault|database-vault) : ;;
    '') if [ "$tier" = "vault" ]; then kind="file-vault"; else kind="application"; fi ;;
    *)  err "template '$template' has invalid kind '$kind'" ;;
esac

data_mount="$(read_field data_mount)"
data_source="$(read_field data_source)"
if [ "$tier" = "vault" ]; then
    [ -n "$data_mount" ]  || err "template '$template' tier=vault but data_mount= is empty"
    [ -n "$data_source" ] || err "template '$template' tier=vault but data_source= is empty"
fi

clusters="$(read_field clusters)"

tmpl_ds="$POOL/jails/templates/$template"
tmpl_snap="${tmpl_ds}${current_base}"
zfs list "$tmpl_snap" >/dev/null 2>&1 \
    || err "template snapshot '$tmpl_snap' does not exist"

container_ds="$POOL/jails/containers/$name"
jailroot="/usr/local/jails/containers/$name"

if zfs list "$container_ds" >/dev/null 2>&1; then
    err "container dataset '$container_ds' already exists"
fi
if [ -d "$jailroot" ] && [ -e "$jailroot/bin/sh" ]; then
    err "jailroot '$jailroot' already exists"
fi

log "Cloning $tmpl_snap -> $container_ds"
zfs clone "$tmpl_snap" "$container_ds" || err "zfs clone failed"
if ! zfs set "mountpoint=$jailroot" "$container_ds"; then
    zfs destroy -r "$container_ds" 2>/dev/null || true
    err "cannot set mountpoint"
fi
row "jail root" "$jailroot (cloned)"

mkdir -p "$jailroot" || err "cannot ensure $jailroot"

if [ -f "$tmpl_dir/jail.conf.tmpl" ]; then
    epair_num=$(( $(echo "$name" | cksum | awk '{print $1}') % 1000 ))
    epair="epair${epair_num}"
    ip="10.0.0.$(( epair_num % 200 + 2 ))"

    install -d /etc/jail.conf.d
    sed -e "s/\${name}/$name/g" \
        -e "s/\${tier}/$tier/g" \
        -e "s/\${epair}/$epair/g" \
        -e "s/\${ip}/$ip/g" \
        -e "s|\${data_mount}|$data_mount|g" \
        -e "s|\${data_source}|$data_source|g" \
        "$tmpl_dir/jail.conf.tmpl" > "/etc/jail.conf.d/$name.conf.tmp.$$" \
        || err "sed jail.conf.tmpl failed"
    chmod 0644 "/etc/jail.conf.d/$name.conf.tmp.$$"
    chown root:wheel "/etc/jail.conf.d/$name.conf.tmp.$$"
    mv "/etc/jail.conf.d/$name.conf.tmp.$$" "/etc/jail.conf.d/$name.conf" \
        || err "cannot install /etc/jail.conf.d/$name.conf"
    row "jail.conf" "$name.conf (epair=$epair ip=$ip)"
fi

if [ -f "$tmpl_dir/fstab.tmpl" ]; then
    sed -e "s/\${name}/$name/g" \
        -e "s/\${tier}/$tier/g" \
        -e "s|\${data_mount}|$data_mount|g" \
        -e "s|\${data_source}|$data_source|g" \
        "$tmpl_dir/fstab.tmpl" > "/etc/fstab.$name.tmp.$$" \
        || err "sed fstab.tmpl failed"
    chmod 0644 "/etc/fstab.$name.tmp.$$"
    chown root:wheel "/etc/fstab.$name.tmp.$$"
    mv "/etc/fstab.$name.tmp.$$" "/etc/fstab.$name" \
        || err "cannot install /etc/fstab.$name"
    row "fstab" "$name"
fi

if [ -f "$tmpl_dir/provision.sh" ]; then
    install -d "$jailroot/usr/local/etc/parishbsd"
    install -m 0755 "$tmpl_dir/provision.sh" \
        "$jailroot/usr/local/etc/parishbsd/provision.sh"
    row "provision.sh" "installed"

    rclocal="$jailroot/etc/rc.local"
    if [ ! -f "$rclocal" ] || ! grep -qF 'parishbsd/provision.sh' "$rclocal" 2>/dev/null; then
        cat > "$rclocal" <<'EOF'
#!/bin/sh
if [ -x /usr/local/etc/parishbsd/provision.sh ]; then
    /usr/local/etc/parishbsd/provision.sh
fi
exit 0
EOF
        chmod 0755 "$rclocal"
        row "rc.local" "provisioning hook installed"
    fi
fi

install -d "$jailroot/usr/local/etc/parishbsd"
meta_tmp="$jailroot/usr/local/etc/parishbsd/metadata.tmp.$$"
{
    printf 'SOURCE_TEMPLATE=%s\n' "$template"
    printf 'TIER=%s\n' "$tier"
    printf 'KIND=%s\n' "$kind"
    printf 'CREATED=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$meta_tmp" || err "cannot write metadata"
chmod 0644 "$meta_tmp"
chown root:wheel "$meta_tmp"
sync
mv "$meta_tmp" "$jailroot/usr/local/etc/parishbsd/metadata" \
    || err "cannot install metadata"
row "metadata" "source_template=$template tier=$tier kind=$kind"

exchange_subdir="/var/parishbsd/exchange/$name"
install -d "$exchange_subdir"
chmod 0770 "$exchange_subdir"
install -d "$jailroot/exchange"

case ",$clusters," in
    *,T-xpra,*)
        mkdir -p "/var/run/xpra/$name"
        chmod 0700 "/var/run/xpra/$name"
        mkdir -p "$jailroot/xpra"
        chmod 0700 "$jailroot/xpra"
        row "xpra dirs" "/var/run/xpra/$name"
        ;;
esac

if [ "$tier" = "vault" ]; then
    mkdir -p "$jailroot$data_mount"
    row "data dir" "$jailroot$data_mount"
fi

log "Jail '$name' is ready."
log "Start it with: service jail start $name"