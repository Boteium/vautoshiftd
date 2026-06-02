#!/bin/sh
set -eu

LIBEXECDIR="/usr/lib/vautoshiftd"
UNITDIR="/usr/lib/systemd/system"

if [ "$(id -u)" -ne 0 ]; then
	echo "uninstall.sh: run as root (e.g. sudo ./uninstall.sh)" >&2
	exit 1
fi

if systemctl is-enabled --quiet vautoshiftd.service 2>/dev/null; then
	systemctl disable --now vautoshiftd.service
elif systemctl is-active --quiet vautoshiftd.service 2>/dev/null; then
	systemctl stop vautoshiftd.service
fi

rm -f "$LIBEXECDIR/vautoshiftd"
rm -f "$UNITDIR/vautoshiftd.service"

systemctl daemon-reload

echo "Removed vautoshiftd binary and systemd unit."
echo "/etc/default/vautoshiftd was left in place."
