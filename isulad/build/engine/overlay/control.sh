#!/bin/sh
case "$0" in */*) D=${0%/*};; *) D=.;; esac
BASE=$(CDPATH= cd -- "$D" && pwd -P) || exit 1
BB="$BASE/bin/busybox"
ROOT="$BASE/rootfs"
export PATH="$ROOT/usr/bin:$ROOT/usr/sbin:/usr/bin:/bin:/usr/sbin:/sbin"
# Do not expose the guest dynamic binaries to host shell PATH.
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
[ "$("$BB" id -u)" = 0 ] || { echo 'Run as root (sudo is not required if already root).'; exit 1; }
"$BB" mkdir -p "$BASE/logs" "$BASE/run" "$BASE/data" "$BASE/inbox" "$BASE/evidence"
umask 077
alive() {
 PID=$("$BB" cat "$BASE/run/daemon.pid" 2>/dev/null) || return 1
 case "$PID" in ''|*[!0-9]*) return 1;; esac
 "$BB" kill -0 "$PID" 2>/dev/null || return 1
 [ "$("$BB" readlink /proc/"$PID"/root)" = "$ROOT" ]
}
client() {
 alive || { echo 'Daemon is not running. Read logs/start.log and run: sh control.sh start' >&2; return 1; }
 "$BB" nsenter -t "$PID" -m -n "$BB" chroot "$ROOT" /usr/bin/isula "$@"
}
case "${1:-help}" in
 start)
  if alive; then echo "Already running pid=$PID"; exit 0; fi
  # Vendor fix: overlay2 requires a registered kernel filesystem. A .ko
  # is kernel-specific; never auto-load an arbitrary module from this bundle.
  if ! "$BB" grep -qw overlay /proc/filesystems; then
   echo 'ERROR: OverlayFS unavailable. Load the matching vendor overlay.ko on HOST, then retry.' >&2
   echo 'Check: bin/busybox grep -w overlay /proc/filesystems' >&2
   exit 1
  fi
  # Vendor fix: iSulad lstat/security-check of this directory failed on a
  # clean device. Its location is data/ because data is bound to graph root.
  "$BB" mkdir -p "$BASE/data/isulad_tmpdir"
  "$BB" chown 0:0 "$BASE/data/isulad_tmpdir"
  "$BB" chmod 700 "$BASE/data/isulad_tmpdir"
  "$BB" chroot "$ROOT" /usr/bin/isula --version || exit 1
  "$BB" sh -c 'trap "" HUP; exec "$@"' sh "$BB" unshare -m -n "$BB" sh "$BASE/scripts/ns-start.sh" "$BASE" > "$BASE/logs/start.log" 2>&1 < /dev/null &
  SUP=$!
  echo "Starting; startup log: $BASE/logs/start.log"
  N=0
  while [ "$N" -lt 45 ]; do
   if alive && client version > "$BASE/logs/version.txt" 2>&1; then
    echo "READY pid=$PID"; "$BB" cat "$BASE/logs/version.txt"; exit 0
   fi
   if ! "$BB" kill -0 "$SUP" 2>/dev/null; then break; fi
   "$BB" sleep 1; N=$((N+1))
  done
  echo 'Startup not confirmed. Last log lines:'; "$BB" tail -n 30 "$BASE/logs/start.log"; "$BB" tail -n 40 "$BASE/evidence/isulad.log" 2>/dev/null; exit 1
  ;;
 stop)
  alive || { echo 'Not running'; exit 0; }
  IDS=$(client ps -q) || exit 1
  for ID in $IDS; do client stop "$ID" || exit 1; done
  "$BB" kill -TERM "$PID" || exit 1
  N=0; while alive && [ "$N" -lt 30 ]; do "$BB" sleep 1; N=$((N+1)); done
  if alive; then echo 'Daemon still running; inspect logs. No forced kill performed.'; exit 1; fi
  echo 'Stopped. Images and logs retained.'
  ;;
 status) client version && client info;;
 cli) shift; client "$@";;
 shell)
  alive || exit 1
  shift
  "$BB" nsenter -t "$PID" -m -n "$BB" chroot "$ROOT" /bin/bash "$@"
  ;;
 check)
  "$BB" uname -a
  "$BB" df -k "$BASE"
  "$BB" grep -E 'cgroup|overlay' /proc/filesystems
  "$BB" grep cgroup /proc/mounts
  "$BB" chroot "$ROOT" /usr/bin/isula --version
  "$BB" chroot "$ROOT" /usr/bin/runc --version
  ;;
 collect)
  "$BB" uname -a > "$BASE/logs/host-uname.txt"
  "$BB" cat /proc/cgroups > "$BASE/logs/host-cgroups.txt"
  "$BB" cat /proc/mounts > "$BASE/logs/host-mounts.txt"
  "$BB" df -k "$BASE" > "$BASE/logs/host-disk.txt"
  if alive; then client info > "$BASE/logs/engine-info.txt" 2>&1; client ps -a > "$BASE/logs/containers.txt" 2>&1; fi
  # Minimal vendor PATH may not contain gzip. BusyBox tar creation with
  # -z can exec an external gzip, so invoke the bundled applets explicitly.
  # Check every stage; never print success for a missing/truncated archive.
  "$BB" tar -cf "$BASE/diagnostics.tar" -C "$BASE" logs evidence || exit 1
  "$BB" gzip -c "$BASE/diagnostics.tar" > "$BASE/diagnostics.tar.gz" || exit 1
  "$BB" gzip -t "$BASE/diagnostics.tar.gz" || exit 1
  "$BB" rm -f "$BASE/diagnostics.tar"
  echo "$BASE/diagnostics.tar.gz"
  ;;
 *) echo 'Usage: sh control.sh check|start|status|stop|collect|cli ARGS|shell [SCRIPT]';;
esac
