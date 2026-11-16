#!/bin/sh
# Explicit host-side operation only. A matching module must come from the
# vendor/kernel build; shipping a random overlay.ko would not fix missing ABI.
set -eu
case "$0" in */*) D=${0%/*};; *) D=.;; esac
BASE=$(CDPATH= cd -- "$D/.." && pwd -P)
BB="$BASE/bin/busybox"
[ "$("$BB" id -u)" = 0 ] || { echo 'Run as root'; exit 1; }
if "$BB" grep -qw overlay /proc/filesystems; then echo 'OverlayFS already available'; exit 0; fi
[ "$#" = 1 ] && [ -f "$1" ] || { echo 'Usage: sh scripts/load-overlay.sh /absolute/path/to/vendor/overlay.ko'; exit 2; }
"$BB" uname -r
if ! "$BB" insmod "$1"; then
 echo 'Module rejected: inspect dmesg for vermagic, symbols, signature or dependencies.' >&2
 "$BB" dmesg | "$BB" tail -n 30
 exit 1
fi
"$BB" grep -w overlay /proc/filesystems || { echo 'Module loaded but OverlayFS not registered'; exit 1; }
echo 'OverlayFS registered; continue with sh control.sh start'
