#!/bin/sh
# provision.sh  setup for app-xpra jails.
#
# This script only does runtime configuration
# that must run inside the running jail.
#
# The /xpra and /exchange directories are provided by the host as
# nullfs mounts. Do not create or chmod them here.
set -eu

log() { printf '    [app-xpra] %s\n' "$*"; }

# --- Sanity check: expected mounts are present ---
if [ ! -d /xpra ]; then
    log "WARN: /xpra is not mounted"
fi

if [ ! -d /exchange ]; then
    log "WARN: /exchange is not mounted"
fi

# Xvfb is required for headless xpra start
# Verify it is present. If not, warn so the user knows
# the headless path will not work.
#
# The install hint names the FreeBSD package. A VNC-based Xvfb
# variant (if the personality switches) would need its own check.
if ! command -v Xvfb >/dev/null 2>&1 && [ ! -x /usr/local/bin/Xvfb ]; then
    log "WARN: Xvfb is not installed - headless Xpra mode will not work"
    log "      Install it in this container: pkg install -y xorg-vfbserver"
fi

# --- Point applications at the exchange directory for downloads ---
# XDG_DOWNLOAD_DIR
cat > /usr/local/etc/profile.d/parishbsd-downloads.sh <<'EOF'
# downloads go to /exchange (a nullfs mount from the host)
export XDG_DOWNLOAD_DIR=/exchange
EOF
chmod 0644 /usr/local/etc/profile.d/parishbsd-downloads.sh

# --- Symlink ~/Downloads /exchange for apps that hardcode it ---
for home in /home/* /root; do
    [ -d "$home" ] || continue
    [ -e "$home/Downloads" ] && continue
    [ -L "$home/Downloads" ] && continue
    ln -s /exchange "$home/Downloads" 2>/dev/null || true
done

log "app-xpra provisioning complete"