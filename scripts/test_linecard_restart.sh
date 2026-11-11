#!/usr/bin/env bash
if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/<脚本名> 或 bash scripts/<脚本名>(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi
# test_linecard_restart.sh — 重啟韌性 D2:單卡死亡/復位驗證(SoftReinit 首測,doc 54)
#
# 鏈路: 旋鈕 linecard_oper_status DOWN(1)→ LINECARD_STATE_CHANGE(INACTIVE)
#        → syncd 清計數器/倒佇列 → 旋鈕 UP(0)→ ACTIVE
#        → SoftReinit:重用 RID、照帳本逐筆重放 set(每筆驗收,失敗即拋)
#
# 前提: VM 在跑;syncd Up 且 RUNNING;線卡記帳在(ASIC_DB);redis AKE 開
# 斷言: RID 不變;計數器鍵集 DOWN 消失/UP 回歸;SoftReinit 指紋;告警如實上報;syncd 進程全程不死
# 出處: docs/06-數據採集/53-重啟韌性①-單卡死亡復位驗證計劃(SoftReinit首測).md §5
set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 admin@localhost"}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
LEDGER_KEY='ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:oid:0x1000000000000'
EVDIR=/tmp/opencode/linecard_restart.$$
PASS=1

log() { echo "[d2] $*"; }
die() { echo "[d2][ERROR] $*" >&2; exit 1; }
fail() { echo "[d2][FAIL] $*"; PASS=0; }
vm() { $VM_SSH "$@" 2>/dev/null; }
mkdir -p "$EVDIR"

# 互斥锁:同一台 VM 的旋钮/账本/容器是共享状态,并发验证会互相踩踏
# (2026-11-11 实证:两个 opencode 同时跑 D1,边沿互吞、账本互冲)
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || die "另一个验证脚本正在运行(排队 60s 未取到锁)——稍后再试,或停掉另一个 opencode 的验证循环"


cleanup() {
    # 旋鈕還原靜默 + 告警清回(oper_status=0 + alarm_status=1)
    vm "echo '{\"linecard_oper_status\": 0, \"alarm_status\": 1, \"alarm_type\": 22, \"alarm_severity\": 5, \"alarm_text\": \"d2 cleanup\"}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}} {{.Status}}' | grep -q '^syncd Up'" || die "syncd 容器沒在跑"
vm "sudo docker exec syncd supervisorctl status syncd" | grep -q RUNNING || die "syncd 程序非 RUNNING"
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . \
    || die "線卡記帳不存在(先跑 scripts/seed_linecard.sh)"

# ── 1) pre-state ─────────────────────────────────────────
PID0=$(vm "sudo docker exec syncd supervisorctl pid syncd")
RID=$(vm "redis-cli -n 1 --raw hget VIDTORID 'oid:0x1000000000000'")
[ -n "$RID" ] || die "VIDTORID 映射裡找不到線卡 RID"
CNT0=$(vm "redis-cli -n 2 --raw --scan --pattern 'COUNTERS*' | sort")
# 對賬窗口基準:先注入一筆帶唯一標記的告警,以它在 syslog 的行號為測試窗口起點
# (免疫 rsyslog 落盤延遲;模擬器只在 alarm_status 變化時發——0/1 振盪嘗試,總有一發出邊沿)
knob_alarm() { # $1=status $2=text
    vm "echo '{\"linecard_oper_status\": 0, \"alarm_status\": $1, \"alarm_type\": 22, \"alarm_severity\": 5, \"alarm_text\": \"$2\"}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}
MARKER="D2RUN-$$-$(date +%s)"
MARKLN=""
for S in 0 1 0; do
    knob_alarm "$S" "${MARKER}"
    for _ in $(seq 1 6); do
        MARKLN=$(vm "sudo grep -n '${MARKER}' /var/log/syslog | tail -1 | cut -d: -f1")
        [ -n "$MARKLN" ] && break 2
        sleep 1
    done
done
[ -n "$MARKLN" ] || die "標記告警未落地(0/1 振盪三發皆無邊沿,rsyslog 線路異常)"

CNT0N=$(echo "$CNT0" | grep -c . || true)
CUR0=$(vm "redis-cli -n 6 --raw keys 'CURALARM*'" | sort)
echo "$CNT0" > "$EVDIR/counters.pre"; echo "$CUR0" > "$EVDIR/curalarm.pre"
log "pre-state: RID=$RID syncd-pid=$PID0 計數器鍵=${CNT0N} CURALARM=$(echo "$CUR0" | grep -c . || echo 0) 條"

# ── 2) 動作①:旋鈕 DOWN(INACTIVE)────────────────────────
log "旋鈕 DOWN(oper_status=1)……"
vm "echo '{\"linecard_oper_status\": 1}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"

DOWNOK=0
for _ in $(seq 1 15); do
    vm "sudo awk -v n="$MARKLN" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'linecard state change'" && DOWNOK=1 && break
    sleep 1
done
[ "$DOWNOK" = 1 ] || fail "15s 內未見 INACTIVE 通知指紋(linecard state change)"

# G2 校准(2026-11-06 首测):removeAllCounters 拆采集器,不清 DB 键——
# 可观测指纹改为 SLOT_COMM_FAIL(CRITICAL)自动生成/清除对
SCFGEN=0
for _ in $(seq 1 15); do
    vm "sudo awk -v n="$MARKLN" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'handler_alarm_generated: ALARM generated key:#SLOT_COMM_FAIL'" && SCFGEN=1 && break
    sleep 1
done
[ "$SCFGEN" = 1 ] && log "INACTIVE 处置实证:SLOT_COMM_FAIL(CRITICAL)自动生成" \
                 || fail "INACTIVE 后未见 SLOT_COMM_FAIL 告警生成"
PID1=$(vm "syncd_pid() { sudo docker exec syncd supervisorctl pid syncd; }; syncd_pid")
[ "$PID1" = "$PID0" ] || fail "INACTIVE 期間 syncd 進程死亡(pid $PID0 → $PID1)"

# ── 3) 動作②:旋鈕 UP(ACTIVE → SoftReinit)───────────────
log "旋鈕 UP(oper_status=0)→ 期待 SoftReinit……"
vm "echo '{\"linecard_oper_status\": 0}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"

SOFTOK=0
for _ in $(seq 1 30); do
    vm "sudo awk -v n="$MARKLN" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'set linecard attr'" && SOFTOK=1 && break
    sleep 1
done
[ "$SOFTOK" = 1 ] && log "SoftReinit 指紋實證:set linecard attr(重用 RID,無 create)" \
                  || fail "30s 內未見 SoftReinit 指紋(set linecard attr)"
vm "sudo awk -v n="$MARKLN" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'created LINECARD RID'" \
    && fail "SoftReinit 期間出現 create 指紋(物件不應重生,RID 應重用)"

SCFCLR=0
for _ in $(seq 1 30); do
    vm "sudo awk -v n="$MARKLN" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'handler_alarm_cleared: ALARM cleared key:#SLOT_COMM_FAIL'" && SCFCLR=1 && break
    sleep 1
done
[ "$SCFCLR" = 1 ] && log "ACTIVE 处置实证:SLOT_COMM_FAIL 自动清除(生成/清除对完整)" \
                 || fail "UP 后未见 SLOT_COMM_FAIL 清除"

CNT2=$(vm "redis-cli -n 2 --raw --scan --pattern 'COUNTERS*' | sort")
CNT2N=$(echo "$CNT2" | grep -c . || true)
echo "$CNT2" > "$EVDIR/counters.post"
[ "$CNT2N" -ge "$CNT0N" ] && log "計數器鍵集回歸:${CNT2N} ≥ ${CNT0N}" \
                          || fail "UP 後計數器鍵集未回歸(${CNT2N} < ${CNT0N})"
RID2=$(vm "redis-cli -n 1 --raw hget VIDTORID 'oid:0x1000000000000'")
[ "$RID2" = "$RID" ] || fail "RID 變了($RID → $RID2),SoftReinit 應重用舊 RID"

# ── 4) 如實上報:復活後注入一筆告警 ──────────────────────
log "注入告警(驗證復活後管線如實上報;振盪避開邊沿陷阱)……"
RV=""
for S in 0 1 0; do
    RTXT="d2-revive-$S-$(date +%s)"
    knob_alarm "$S" "$RTXT"
    for _ in $(seq 1 6); do
        vm "sudo awk -v n=\"$MARKLN\" 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q \"$RTXT\"" && RV="$RTXT" && break
        sleep 1
    done
    [ -n "$RV" ] && break
done
[ -n "$RV" ] && log "復活後告警如實上報實證:handler_alarm_generated '$RV'" \
               || fail "復活後告警未如實上報(振盪三發皆無日誌)"


# ── 5) 進程存活總檢 ──────────────────────────────────────
PID2=$(vm "sudo docker exec syncd supervisorctl pid syncd")
[ "$PID2" = "$PID0" ] || fail "測試結束時 syncd 進程 pid 變了($PID0 → $PID2)——all-or-nothing 觸發,如實記錄"

echo "$CNT0N $CNT2N" > "$EVDIR/summary"
log "證據目錄: $EVDIR"
if [ "$PASS" = 1 ]; then
    log "════ D2 PASS:單卡死亡/復位全鏈驗證通過(SoftReinit 首測)════"
else
    log "════ D2 FAIL:存在未通過斷言(見上 [FAIL] 行)════"
fi
exit $((1 - PASS))
