#!/bin/sh
# libexec/parishbsd/lib/policy.sh
#
# Host-side trust/risk policy, audio permissions, and template capability
# resolution for ParishBSD Compartments.
#
# All policy files live under host-only /var/db/parishbsd/ (invisible inside jails)
# so a compromised compartment cannot spoof its own risk border, capabilities,
# or audio privileges.
#
# REQUIRES (sourced by caller before this file):
#   validate.sh
#   state.sh

PARISH_RISK_DIR="/var/db/parishbsd/risk"
PARISH_TRUST_DIR="/var/db/parishbsd/trust"
PARISH_AUDIO_DB_DIR="/var/db/parishbsd/audio"
PARISH_TEMPLATES_STATE="/var/db/parishbsd/templates"
PARISH_TEMPLATES_SHIPPED="/usr/local/share/parishbsd/templates"

# parish_normalize_risk <value>
# Prints low|medium|high|critical or returns 1 if invalid.
parish_normalize_risk() {
    case "${1-}" in
        low|trusted)      printf 'low' ;;
        medium|work)      printf 'medium' ;;
        high|untrusted)   printf 'high' ;;
        critical|vault)   printf 'critical' ;;
        *) return 1 ;;
    esac
}

# parish_container_risk <jail> <tier>
# Reads host-side /var/db/parishbsd/risk/<jail> (falls back to legacy trust/<jail>).
# Defaults: vault -> critical, app/other -> medium.
parish_container_risk() {
    _pr_jail="${1-}"
    _pr_tier="${2-app}"
    _pr_rfile="$PARISH_RISK_DIR/$_pr_jail"

    if [ -n "$_pr_jail" ] && [ -f "$_pr_rfile" ] && [ ! -L "$_pr_rfile" ]; then
        _pr_val="$(head -n 1 "$_pr_rfile" 2>/dev/null | tr -d '[:space:]')"
        if parish_normalize_risk "$_pr_val"; then
            return 0
        fi
    fi

    # Legacy trust/<jail> migration mapping (high trust = low risk, low trust = high risk)
    _pr_tfile="$PARISH_TRUST_DIR/$_pr_jail"
    if [ -n "$_pr_jail" ] && [ -f "$_pr_tfile" ] && [ ! -L "$_pr_tfile" ]; then
        _pr_tval="$(head -n 1 "$_pr_tfile" 2>/dev/null | tr -d '[:space:]')"
        case "$_pr_tval" in
            high)   printf 'low'; return 0 ;;
            medium) printf 'medium'; return 0 ;;
            low)    printf 'high'; return 0 ;;
        esac
    fi

    if [ "$_pr_tier" = "vault" ]; then
        printf 'critical'
    else
        printf 'medium'
    fi
}

# parish_set_risk_file <jail> <low|medium|high|critical>
parish_set_risk_file() {
    _sr_jail="${1-}"
    _sr_risk="$(parish_normalize_risk "${2-}")" || return 1
    validate_jail_name_existing "$_sr_jail" || return 1

    install -d -m 0755 -o root -g wheel "$PARISH_RISK_DIR" || return 1
    _sr_target="$PARISH_RISK_DIR/$_sr_jail"
    [ ! -L "$_sr_target" ] || return 1
    _sr_tmp="${_sr_target}.tmp.$$"
    printf '%s\n' "$_sr_risk" > "$_sr_tmp" || { rm -f "$_sr_tmp"; return 1; }
    chmod 0644 "$_sr_tmp"
    chown root:wheel "$_sr_tmp"
    mv -f "$_sr_tmp" "$_sr_target" || { rm -f "$_sr_tmp"; return 1; }
    return 0
}

# parish_audio_policy_get <jail> <tier>
# Prints "<output:0|1> <input:0|1>".
# Vaults ALWAYS return "0 0".
parish_audio_policy_get() {
    _pa_jail="${1-}"
    _pa_tier="${2-app}"
    if [ "$_pa_tier" = "vault" ]; then
        printf '0 0'
        return 0
    fi

    _pa_out=0
    _pa_in=0
    _pa_file="$PARISH_AUDIO_DB_DIR/$_pa_jail"
    if [ -n "$_pa_jail" ] && [ -f "$_pa_file" ] && [ ! -L "$_pa_file" ]; then
        while IFS='=' read -r _k _v; do
            case "$_k" in
                AUDIO_OUTPUT) [ "$_v" = "1" ] && _pa_out=1 ;;
                AUDIO_INPUT)  [ "$_v" = "1" ] && _pa_in=1 ;;
            esac
        done < "$_pa_file"
    fi
    printf '%s %s' "$_pa_out" "$_pa_in"
}

# parish_container_capabilities <jailroot> <tier> <source_template>
# Prints space-separated capability tokens:
#   gui, audio, compat-linux, compat-windows, vault
parish_container_capabilities() {
    _pc_root="${1-}"
    _pc_tier="${2-app}"
    _pc_tmpl="${3-}"

    if [ "$_pc_tier" = "vault" ]; then
        # Vaults never receive audio; GUI only if explicitly provisioned with /xpra
        if [ -d "$_pc_root/xpra" ]; then
            printf 'vault gui'
        else
            printf 'vault'
        fi
        return 0
    fi

    _pc_clusters=""
    if [ -n "$_pc_tmpl" ]; then
        for _tdir in "$PARISH_TEMPLATES_STATE" "$PARISH_TEMPLATES_SHIPPED"; do
            _tconf="$_tdir/$_pc_tmpl/template.conf"
            if [ -f "$_tconf" ] && [ ! -L "$_tconf" ]; then
                _pc_clusters="$(awk -F= '$1=="clusters"{sub(/^[^=]*=/,""); print; exit}' "$_tconf" 2>/dev/null)"
                [ -n "$_pc_clusters" ] && break
            fi
        done
    fi

    _pc_caps=""
    _pc_add() {
        case " $_pc_caps " in
            *" $1 "*) : ;;
            *) _pc_caps="${_pc_caps:+$_pc_caps }$1" ;;
        esac
    }

    case ",$_pc_clusters," in
        *,T-xpra,*) _pc_add "gui" ;;
    esac
    if [ -d "$_pc_root/xpra" ]; then
        _pc_add "gui"
    fi

    # Non-vault compartments support host-brokered audio toggles
    _pc_add "audio"

    case ",$_pc_clusters," in
        *,T-compat-linux,*) _pc_add "compat-linux" ;;
    esac
    case ",$_pc_clusters," in
        *,T-compat-windows,*) _pc_add "compat-windows" ;;
    esac
    case "$_pc_tmpl" in
        apps-compat*)
            _pc_add "compat-linux"
            _pc_add "compat-windows"
            ;;
    esac
    if [ -d "$_pc_root/compat/linux" ] && [ -e "$_pc_root/etc/fstab" ] || \
       grep -q '/compat/linux' "/etc/fstab.$(basename "$_pc_root")" 2>/dev/null; then
        _pc_add "compat-linux"
    fi
    if [ -x "$_pc_root/usr/local/bin/wine" ] || [ -x "$_pc_root/usr/local/bin/wine64" ]; then
        _pc_add "compat-windows"
    fi

    printf '%s' "$_pc_caps"
}
