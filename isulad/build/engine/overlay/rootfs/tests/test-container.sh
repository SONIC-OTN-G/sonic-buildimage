#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE=${ENGINE:-isula}
IMAGE=${IMAGE:-sonic-database-standalone:arm64-20261022}
RUN_ID=${RUN_ID:-$(date +%Y%m%d%H%M%S)}
NAME=sonic-arm64-db-$RUN_ID
RESULTS="/evidence/results-$RUN_ID"
mkdir -p "$RESULTS/redis/sonic-db"
cp database_config.json "$RESULTS/redis/sonic-db/database_config.json"
# Vendor minimal /dev may have no /dev/fd. Bash process substitution
# (> >(tee ...)) then fails before any container test. Direct file logging
# has no /dev/fd dependency; control/test.sh reports the evidence location.
exec > "$RESULTS/test.log" 2>&1
echo "engine=$ENGINE image=$IMAGE container=$NAME"
uname -a
cat /etc/os-release
if [ "${ALLOW_EMULATION:-0}" != 1 ]; then
    case "$(uname -m)" in aarch64|arm64) ;; *) echo 'Requires ARM64 device; set ALLOW_EMULATION=1 only for explicitly simulated tests'; exit 1;; esac
fi
ls /proc/sys/fs/binfmt_misc 2>/dev/null || true
"$ENGINE" version
"$ENGINE" info > "$RESULTS/engine-info.txt"
opts=(--runtime runc)
[ "$ENGINE" != docker ] || opts=(--platform linux/arm64)
redis_args=()
if [ "${ALLOW_EMULATION:-0}" = 1 ]; then
    echo 'EMULATION ONLY: bypass ARM64-COW-BUG guard; save and appendonly remain disabled'
    redis_args=(/etc/redis/sonic-standalone.conf --ignore-warnings ARM64-COW-BUG)
fi
"$ENGINE" run -d --name "$NAME" "${opts[@]}" --network none -v "$RESULTS/redis:/var/run/redis:rw" "$IMAGE" "${redis_args[@]}"
ready() {
    for i in $(seq 1 30); do
        if "$ENGINE" exec "$NAME" redis-cli -s /var/run/redis/redis.sock PING | grep -qx PONG; then return 0; fi
        sleep 1
    done
    return 1
}
diagnostics() {
    "$ENGINE" inspect "$NAME" > "$RESULTS/inspect.json" 2>&1 || true
    "$ENGINE" logs "$NAME" > "$RESULTS/container.log" 2>&1 || true
}
trap diagnostics EXIT
ready
"$ENGINE" exec "$NAME" /bin/sh -c 'dpkg --print-architecture; test "$(dpkg --print-architecture)" = arm64'
"$ENGINE" exec "$NAME" python3 -c 'from swsscommon import swsscommon; print("swsscommon-import-ok")'
for pass in first restart; do
    "$ENGINE" exec "$NAME" redis-cli -s /var/run/redis/redis.sock SET arm64_probe "$pass"
    "$ENGINE" exec "$NAME" redis-cli -s /var/run/redis/redis.sock GET arm64_probe | grep -qx "$pass"
    "$ENGINE" exec "$NAME" python3 -c 'from swsscommon import swsscommon as s; db=s.DBConnector(0,"/var/run/redis/redis.sock",0); t=s.Table(db,"ARM64_PROBE"); t.set("key",s.FieldValuePairs([("status","ok")])); ok,values=t.get("key"); assert ok and dict(values)["status"]=="ok"; t._del("key"); print("swsscommon-redis-read-write-ok")'
    test -S "$RESULTS/redis/redis.sock"
    if [ "$pass" = first ]; then
        "$ENGINE" stop "$NAME"
        "$ENGINE" start "$NAME"
        ready
    fi
done
diagnostics
"$ENGINE" stop "$NAME"
echo 'PASS: ARM64 packages, exec, swsscommon import/Redis I/O, bind mount, stop/start.'
echo "Container retained stopped: $NAME"
