#!/bin/sh
case "$0" in */*) D=${0%/*};; *) D=.;; esac
BASE=$(CDPATH= cd -- "$D" && pwd -P) || exit 1
BB="$BASE/bin/busybox"
case "${1:-smoke}" in
 smoke) SCRIPT=/tests/smoke.sh;;
 load-sonic) SCRIPT=/tests/load-sonic.sh;;
 database) SCRIPT=/tests/test-container.sh;;
 ot) SCRIPT=/tests/test-ot-containers.sh;;
 *) echo 'Usage: sh test.sh smoke|load-sonic|database|ot'; exit 2;;
esac
if [ "${1:-smoke}" = smoke ]; then
 "$BB" gzip -dc "$BASE/inbox/arm64-smoke.tar.gz" > "$BASE/inbox/arm64-smoke.tar" || exit 1
fi
"$BB" sh "$BASE/control.sh" shell "$SCRIPT"
RC=$?
echo "Test exit=$RC. Reports are in $BASE/evidence/ (smoke log is inside offline-smoke-*/test.log)."
exit "$RC"
