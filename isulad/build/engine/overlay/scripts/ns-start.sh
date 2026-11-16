#!/bin/sh
set -eu
BASE=$1
BB="$BASE/bin/busybox"
R="$BASE/rootfs"
# Vendor fix: all mount changes stay in control.sh's NEW mount namespace.
# In particular never unmount/rearrange the firmware's cpu,cpuacct hierarchy.
"$BB" mount --make-rprivate /
"$BB" ip link set lo up
"$BB" mount --bind "$R" "$R"
"$BB" mount --bind /proc "$R/proc"
"$BB" mount --rbind /dev "$R/dev"
"$BB" mount --rbind /sys "$R/sys"
"$BB" mount --bind "$BASE/data" "$R/var/lib/isulad"
"$BB" mount --bind "$BASE/inbox" "$R/inbox"
"$BB" mount --bind "$BASE/evidence" "$R/evidence"
"$BB" mount -t tmpfs -o mode=755,nosuid,nodev tmpfs "$R/run"
if "$BB" awk '$2=="/sys/fs/cgroup" && $3=="cgroup2" {found=1} END {exit !found}' /proc/mounts; then
 echo 'Using existing cgroup v2 hierarchy.'
else
 # Vendor fix: cgroups may be below /ofs1, not /sys/fs/cgroup. Mount a
 # private directory tree; sysfs is shared but this covering mount is not.
 "$BB" mount -t tmpfs -o mode=755,nosuid,nodev tmpfs "$R/sys/fs"
 "$BB" mkdir -p "$R/sys/fs/cgroup"
 ENABLED=$("$BB" awk '!/^#/ && $4==1 {print $1}' /proc/cgroups)
 for NAME in $ENABLED; do
  [ ! -e "$R/sys/fs/cgroup/$NAME" ] || continue
  SRC=
  OPTS=
  while read -r DEV MP TYPE MOPTS REST; do
   [ "$TYPE" = cgroup ] || continue
   case ",$MOPTS," in *",$NAME,"*) SRC=$MP; OPTS=$MOPTS; break;; esac
  done < /proc/mounts
  if [ -n "$SRC" ]; then
   # Vendor fix: preserve co-mounted controllers as ONE mount. runc
   # derives mount data from the mountpoint basename. Binding cpu,cpuacct
   # separately as 'cpu' and 'cpuacct' caused EPERM in the cgroup namespace.
   GROUP=
   for C in $ENABLED; do
    case ",$OPTS," in *",$C,"*) GROUP=${GROUP:+$GROUP,}$C;; esac
   done
   DEST="$R/sys/fs/cgroup/$GROUP"
   "$BB" mkdir -p "$DEST"
   echo "Mirror $GROUP from $SRC"
   "$BB" mount --bind "$SRC" "$DEST"
   for C in $ENABLED; do
    case ",$GROUP," in
     *",$C,"*) [ "$C" = "$GROUP" ] || "$BB" ln -s "$GROUP" "$R/sys/fs/cgroup/$C";;
    esac
   done
  else
   # Vendor fix: CONFIG_CGROUP_DEVICE was enabled, but devices had no
   # mount. Expose enabled/unmounted controllers privately, not on host.
   DEST="$R/sys/fs/cgroup/$NAME"
   "$BB" mkdir -p "$DEST"
   if ! "$BB" mount -t cgroup -o "$NAME" "isulad-$NAME" "$DEST"; then
    # /proc/cgroups enabled=1 does not prove v1 support (newer kernels
    # may compile memory/cpuset for v2 only). devices is mandatory here;
    # optional controllers must not prevent this standalone test engine.
    "$BB" rmdir "$DEST"
    if [ "$NAME" = devices ]; then
     echo "ERROR: cannot mount required v1 devices controller" >&2
     exit 1
    fi
    echo "WARNING: enabled controller $NAME is not mountable as v1; skipped" >&2
   fi
  fi
 done
 # This package's v1 runc configuration requires devices. Fail before a
 # misleading container-create error if firmware disabled the controller.
 [ -f "$R/sys/fs/cgroup/devices/devices.list" ] || {
  echo 'ERROR: usable cgroup v1 devices controller is missing' >&2; exit 1;
 }
fi
"$BB" cat /proc/self/mountinfo > "$BASE/logs/private-mountinfo.txt"
"$BB" cat /proc/cgroups > "$BASE/logs/private-cgroups.txt"
echo $$ > "$BASE/run/daemon.pid"
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export LANG=en_US.UTF-8
# Vendor fix: initial rootfs rejected pivot_root(EINVAL). The runc adapter
# inserts --no-pivot for create/run; exec-root restoration is in runc-real.
unset LD_LIBRARY_PATH LD_PRELOAD HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy
exec "$BB" chroot "$R" /usr/bin/isulad
