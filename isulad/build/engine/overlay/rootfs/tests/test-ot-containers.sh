#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE=${ENGINE:-isula}
RUN_ID=${RUN_ID:-$(date +%Y%m%d%H%M%S)}
QEMU_HOST=${QEMU_HOST:-}
if [ -z "$QEMU_HOST" ]; then
    case "$(uname -m)" in aarch64|arm64) ;; *) echo 'Requires ARM64 device, or explicit QEMU_HOST for emulated validation'; exit 1;; esac
fi
RESULTS="/evidence/results-ot-$RUN_ID"
mkdir -p "$RESULTS"
# Vendor minimal /dev may have no /dev/fd. Bash process substitution
# (> >(tee ...)) then fails before any container test. Direct file logging
# has no /dev/fd dependency; control/test.sh reports the evidence location.
exec > "$RESULTS/test.log" 2>&1
uname -a
"$ENGINE" version
opts=(--runtime runc)
[ "$ENGINE" != docker ] || opts=(--platform linux/arm64)
qmount=()
qentry=()
prefix=()
if [ -n "$QEMU_HOST" ]; then
    test -x "$QEMU_HOST"
    echo 'SIMULATED ARM64; explicit QEMU; private Redis COW check overridden for simulation only'
    qmount=(-v "$QEMU_HOST:/arm64-test-qemu:ro")
    qentry=(--entrypoint /arm64-test-qemu)
    prefix=(/arm64-test-qemu -E ARM64_TEST_QEMU=/arm64-test-qemu)
fi
read -r -a components <<< "${COMPONENTS:-otss syncd}"
for component in "${components[@]}"; do
    case "$component" in otss|syncd) ;; *) echo 'Unknown component'; exit 1;; esac
    IMAGE="sonic-$component-standalone:arm64-20261023"
    NAME="sonic-arm64-$component-$RUN_ID"
    mkdir -p "$RESULTS/$component"
    args=()
    [ -z "$QEMU_HOST" ] || args=(/usr/bin/python3 /usr/local/bin/standalone-probe.py "$component")
    "$ENGINE" run -d --name "$NAME" "${opts[@]}" --network none "${qmount[@]}" \
        -v "$RESULTS/$component:/probe-output:rw" "${qentry[@]}" "$IMAGE" "${args[@]}"
    diagnostics() {
        "$ENGINE" inspect "$NAME" > "$RESULTS/$component/inspect.json" 2>&1 || true
        "$ENGINE" logs "$NAME" > "$RESULTS/$component/container.log" 2>&1 || true
    }
    trap diagnostics EXIT
    runpy() { "$ENGINE" exec "$NAME" "${prefix[@]}" /usr/bin/python3 "$@"; }
    ready() {
        for i in $(seq 1 30); do
            if runpy -c 'from pathlib import Path; import sys; sys.exit(0 if Path("/tmp/standalone-probe-ready").exists() else 1)'; then return 0; fi
            sleep 1
        done
        return 1
    }
    ready
    runpy /usr/local/bin/standalone-probe.py "$component" --check
    runpy /usr/local/bin/check-business-binary.py "$component"
    runpy -c 'from pathlib import Path; Path("/probe-output/mount-probe.txt").write_text("mount-ok\n")'
    grep -qx mount-ok "$RESULTS/$component/mount-probe.txt"
    "$ENGINE" stop "$NAME"
    "$ENGINE" start "$NAME"
    ready
    runpy /usr/local/bin/check-business-binary.py "$component"
    diagnostics
    "$ENGINE" stop "$NAME"
    echo "PASS: $component inspection entrypoint, libraries, actual binary help, exec, mount and restart; business control loop NOT TESTED"
    trap - EXIT
done
