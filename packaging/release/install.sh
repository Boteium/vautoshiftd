#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LIBEXECDIR="/usr/lib/vautoshiftd"
UNITDIR="/usr/lib/systemd/system"
DEFAULTDIR="/etc/default"

if [ "$(id -u)" -ne 0 ]; then
	echo "install.sh: run as root (e.g. sudo ./install.sh)" >&2
	exit 1
fi

install -d "$LIBEXECDIR"
install -m 0755 "$ROOT/vautoshiftd" "$LIBEXECDIR/vautoshiftd"

install -d "$UNITDIR"
install -m 0644 "$ROOT/vautoshiftd.service" "$UNITDIR/vautoshiftd.service"

install -d "$DEFAULTDIR"
if [ ! -f "$DEFAULTDIR/vautoshiftd" ]; then
	install -m 0644 "$ROOT/etc/default/vautoshiftd" "$DEFAULTDIR/vautoshiftd"
fi

systemctl daemon-reload

echo "Installed vautoshiftd."
echo "Enable and start with:"
echo "  sudo systemctl enable --now vautoshiftd.service"
