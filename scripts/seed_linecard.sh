#!/usr/bin/env bash
if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/<脚本名> 或 bash scripts/<脚本名>(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi
# seed_linecard.sh — VM 冷啟後手工播種線卡物件(AGENTS.md 規則 5 步驟②③的自動化)
#
# 背景: redis 冷啟(db1/db6 清空)→ 線卡記帳蒸發 → syncd 永等 LINECARD key →
#        告警管線全死。修復 = 兩筆 DB 操作:① hset LINECARD key ② 推一次
#        doc 43 §手工 create 配方。本腳本把兩步打包 + 冪等守衛 + 指紋驗收。
#
# 前提: VM 在跑、syncd 容器 Up、syncd 程序 RUNNING
# 冪等: 記帳(db1)已有 LINECARD 條目 → 不重播直接退出
#        (重播 = 重複 create = syncd 退出,2026-11-06 實證)
# 用法: ./scripts/seed_linecard.sh
# 可調: VM_SSH 環境變數(同其他驗證腳本)
set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 admin@localhost"}
LEDGER_PAT='ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*'
OP_QUEUE='ASIC_STATE_KEY_VALUE_OP_QUEUE'
OP_CHANNEL='ASIC_STATE_CHANNEL@1'
OP_FIELDS='["OTAI_LINECARD_ATTR_LINECARD_STATE_CHANGE_NOTIFY","0xdeadbeef","OTAI_LINECARD_ATTR_LINECARD_ALARM_NOTIFY","0xdeadbeef","OTAI_LINECARD_ATTR_LINECARD_OCM_SPECTRUM_POWER_NOTIFY","0xdeadbeef","OTAI_LINECARD_ATTR_LINECARD_OTDR_RESULT_NOTIFY","0xdeadbeef"]'

log() { echo "[seed] $*"; }
die() { echo "[seed][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

# 互斥锁:同一台 VM 的旋钮/账本/容器是共享状态,并发验证会互相踩踏
# (2026-11-11 实证:两个 opencode 同时跑 D1,边沿互吞、账本互冲)
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || die "另一个验证脚本正在运行(排队 60s 未取到锁)——稍后再试,或停掉另一个 opencode 的验证循环"


# ── 0) 預檢 ──────────────────────────────────────────────
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}} {{.Status}}' | grep -q '^syncd Up'" || die "syncd 容器沒在跑"
vm "sudo docker exec syncd supervisorctl status syncd" | grep -q RUNNING || die "syncd 程序非 RUNNING(supervisorctl status syncd 查看原因)"

# ── 1) 冪等守衛:記帳已有線卡 → 退出 ─────────────────────
if vm "redis-cli -n 1 --raw --scan --pattern '${LEDGER_PAT}' | head -1" | grep -q .; then
    vm "redis-cli -n 1 --raw --scan --pattern '${LEDGER_PAT}' | head -2" | sed 's/^/    已存在: /'
    log "線卡記帳已存在,不重播;若 syncd 剛重啟,hardReinit 會自動重放,無需任何操作"
    exit 0
fi

# ── 2) 線卡 key(syncd 等待循環的解鎖條件)───────────────
vm "redis-cli -n 6 hset 'LINECARD|linecard1' oper-status ACTIVE slot-status Ready" >/dev/null \
    || die "hset LINECARD|linecard1 失敗"
log "LINECARD|linecard1 key 已確保(db6)"

# ── 3) llen 守衛 + 推一次 doc 43 配方 ────────────────────
Q=$(vm "redis-cli -n 1 --raw llen ${OP_QUEUE}")
[ "$Q" = "0" ] || die "op 佇列非空(=${Q}),先 DEL ASIC_STATE_KEY_VALUE_OP_QUEUE 再播種"
vm "redis-cli -n 1 LPUSH '${OP_QUEUE}' \
    'OTAI_OBJECT_TYPE_LINECARD:oid:0x1000000000000' \
    '${OP_FIELDS}' \
    'Screate'" >/dev/null || die "LPUSH 失敗"
vm "redis-cli -n 1 PUBLISH '${OP_CHANNEL}' G" >/dev/null || die "PUBLISH 失敗"
log "create op 已推(vid=oid:0x1000000000000,4 佔位指針齊)"

# ── 4) 指紋驗收:記帳落地 → +10s 基線警報 ─────────────────
for _ in $(seq 1 10); do
    vm "redis-cli -n 1 --raw --scan --pattern '${LEDGER_PAT}' | head -1" | grep -q . && break
    sleep 1
done
LEDGER=$(vm "redis-cli -n 1 --raw --scan --pattern '${LEDGER_PAT}' | head -1")
[ -n "$LEDGER" ] || die "10s 內記帳未落地(查 syncd 日誌: creating linecard)"
log "記帳落地: ${LEDGER}"

for _ in $(seq 1 20); do
    vm "redis-cli -n 6 --raw keys 'CURALARM*'" | grep -q HIGH_TEMPERATURE && break
    sleep 1
done
CUR=$(vm "redis-cli -n 6 --raw keys 'CURALARM*'" | tr '\n' ' ')
echo "$CUR" | grep -q HIGH_TEMPERATURE \
    || die "20s 內基線警報未落地(模擬器 create 後 +10s 定時;查 syncd 日誌 sendAlarmNotification/SEGV)"
log "基線警報落地: ${CUR}"
log "播種完成——線卡物件就緒,可跑三通道驗證腳本"
