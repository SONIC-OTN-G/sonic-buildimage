#!/usr/bin/env bash
# snmp_verify.sh — OTN 告警 SNMP trap 端到端驗證(S1–S5 範圍)
#
# 链路: vslib 注入旋鈕(json)→ syncd 寫/刪 CURALARM
#        → redis keyspace 事件 → alarmsync.py(snmp 容器)
#        → snmptrap UDP → 網管端 snmptrapd(WSL udp:16162)
#
# 前置: VM 在跑;snmp 容器 Up 且 alarmsync RUNNING;WSL snmptrapd 監聽 16162
# 用法: ./scripts/snmp_verify.sh
# 可調: VM_SSH / TRAP_PORT(本地接收端口;829-831 與 <1024 有 WSL 限制,故 16162)
#
# 流程: 預檢 → 接收端就緒 → 重置旋鈕 → pre-clear → raise → clear → trap 摘要
# 注意: 結束時告警清回靜默;trap 摘要取自 snmptrapd 日誌尾部

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
TRAP_PORT=${TRAP_PORT:-16162}
TRAP_LOG=/tmp/opencode/snmptrapd.log
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json

log() { echo "[snmpverify] $*"; }
die() { echo "[snmpverify][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

# ── 0) 預檢 ──────────────────────────────────────────────
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}} {{.Status}}' | grep -q '^snmp Up'" || die "snmp 容器沒在跑"
vm "sudo docker exec snmp supervisorctl status alarmsync" | grep -q RUNNING || die "容器內 alarmsync 非 RUNNING"
ss -ulnp 2>/dev/null | grep -q ":${TRAP_PORT} " || die "接收端 udp:${TRAP_PORT} 沒監聽(先起 snmptrapd)"
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . || die "線卡物件不存在(記帳空):先跑 scripts/seed_linecard.sh"
vm "redis-cli CONFIG GET notify-keyspace-events" | tail -1 | grep -qE "K|A" || die "redis keyspace 事件未開"
log "預檢通過(snmp 容器 + alarmsync RUNNING + 接收端 udp:${TRAP_PORT})"

# ── 1) 接收端日誌不打標記(雙寫者會互相覆蓋;摘要改按注入 text 過濾)──

# ── 2) 重置注入旋鈕到靜默 ────────────────────────────────
OP=$(vm "sudo docker exec syncd cat ${SIM_JSON}" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
sleep 6
log "旋鈕已重置靜默(oper_status=${OP})"

# ── 3) 注入序列:pre-clear → raise → clear ───────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "verify pre-clear"; sleep 8; log "① pre-clear 已注入"
inject 0 22 5 "verify snmp raise $(date +%H:%M:%S)"; sleep 8; log "② raise 已注入(期待 state=1 trap)"
inject 1 22 5 "verify snmp clear"; sleep 14; log "③ clear 已注入(期待 state=2 trap)"

# ── 4) trap 摘要(只取標記之後)──────────────────────────
echo
echo "════ 網管端收到的 trap(snmptrapd 日誌,本輪注入)════"
python3 - "$TRAP_LOG" <<'PYEOF'
import re, sys
lines = open(sys.argv[1]).read().splitlines(keepends=True)
starts = [i for i, l in enumerate(lines) if re.match(r"\d{4}-\d{2}-\d{2} .*\[UDP:", l)]
blocks = ["".join(lines[s:e]) for s, e in zip(starts, starts[1:] + [len(lines)])]
otai = [b for b in blocks if "99999.1.1.0.1" in b]
# 本腳本每輪固定注入 raise+clear;取最後兩個(舊輪與其他來源的 trap 不混入)
# 注意:CLEARED 幀的 text 是「緩存補全」的上次內容,不含本次注入文字,勿按 text 過濾
for i, b in enumerate(otai[-2:], 1):
    def vb(oid):
        m = re.search(r"99999\.1\.%s = \w+: ?\"?([^\"\t\n]*)" % oid, b)
        return (m.group(1) if m else "?").strip()
    state = {"1": "RAISED", "2": "CLEARED"}.get(vb("7"), vb("7"))
    print(f"  {i}. [{state}] {vb('2')}#{vb('3')}  severity={vb('4')}  text={vb('5')!r}")
if not otai:
    print("  (未收到 OTAI trap!)")
PYEOF

# ── 5) DB 終態 ──────────────────────────────────────────
echo
echo "════ VM 側 DB 終態(應無 LOW_TEMPERATURE_ALARM = 已清回靜默)════"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'
echo
log "完成(trap 原始日誌:$TRAP_LOG)"
