#!/bin/sh
set -eu
NAME=offline-smoke-$(date +%Y%m%d%H%M%S)
OUT=/evidence/$NAME
mkdir -p "$OUT"
exec > "$OUT/test.log" 2>&1
trap 'isula inspect "$NAME" > "$OUT/inspect.json" 2>&1 || true; isula logs "$NAME" > "$OUT/container.log" 2>&1 || true; isula stop "$NAME" >/dev/null 2>&1 || true' EXIT
isula version
isula info
isula load -i /inbox/arm64-smoke.tar
isula run -d --name "$NAME" --runtime runc --network none offline-arm64-smoke:20261029
# Regression for vendor outer-chroot bug: matching namespace alone is not
# enough. exec must have the SAME root inode/device as container init.
check_exec_root() {
 PID=$(isula inspect "$NAME" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["State"]["Pid"])')
 MAIN_ROOT=$(stat -Lc '%d:%i' "/proc/$PID/root")
 EXEC_ROOT=$(isula exec "$NAME" /bin/busybox stat -Lc '%d:%i' /)
 echo "init-root=$MAIN_ROOT exec-root=$EXEC_ROOT"
 [ "$MAIN_ROOT" = "$EXEC_ROOT" ]
 MAIN_NS=$(readlink "/proc/$PID/ns/mnt")
 EXEC_NS=$(isula exec "$NAME" /bin/busybox readlink /proc/self/ns/mnt)
 [ "$MAIN_NS" = "$EXEC_NS" ]
 isula exec "$NAME" /bin/busybox sh -c 'test -d "/proc/$$"'
 echo 'PASS: exec mount namespace, root and proc PID view'
}
check_exec_root
isula exec "$NAME" /bin/busybox uname -m
isula exec "$NAME" /bin/busybox sh -c 'echo offline-ok >/tmp/probe; test "$(cat /tmp/probe)" = offline-ok'
isula stop "$NAME"
isula start "$NAME"
check_exec_root
isula exec "$NAME" /bin/busybox sh -c 'test "$(cat /tmp/probe)" = offline-ok'
isula stop "$NAME"
echo 'PASS: image import, run, exec, file write and stop/start.'
