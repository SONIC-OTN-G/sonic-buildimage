#!/usr/bin/env bash
if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/<脚本名> 或 bash scripts/<脚本名>(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi
# test_container_restart.sh — 重启韧性 ②:syncd 容器重启验证(hardReinit 记帐重放,doc 54)
#
# 链路: docker restart syncd → 重新等待 LINECARD key(redis 活,key 仍在,即时过闸)
#        → hardReinit 读帐本(m_asicKeys)→ created LINECARD RID → FlexCounter 两级重建
#        → starting main loop → +10s 基线警报 → 四库键集 diff 断言零丢失
#
# 前提: VM 在跑;线卡记帐在(ASIC_DB);syncd Up;旋钮静默(oper_status=0)
# 断言: ASIC_DB/COUNTERS_DB/FLEX_COUNTER_DB/STATE_DB/HISTORY_DB 键集 pre/post 一致(零丢失);
#       hardReinit 指纹;恢复期告警端到端(唯一标记注入 → NP 写库后日志 + STATE_DB text 双保险)
# 出處: docs/06-數據採集/54-重啟韌性②-容器重啟驗證計劃(hardReinit記帳重放).md §5
set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 admin@localhost"}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
EVDIR=/tmp/opencode/container_restart.$$
PASS=1

log() { echo "[d1] $*"; }
die() { echo "[d1][ERROR] $*" >&2; exit 1; }
fail() { echo "[d1][FAIL] $*"; PASS=0; }
vm() { $VM_SSH "$@" 2>/dev/null; }
mkdir -p "$EVDIR"

# 互斥锁:同一台 VM 的旋钮/账本/容器是共享状态,并发验证会互相踩踏
# (2026-11-11 实证:两个 opencode 同时跑 D1,边沿互吞、账本互冲)
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || die "另一个验证脚本正在运行(排队 60s 未取到锁)——稍后再试,或停掉另一个 opencode 的验证循环"
vm "sudo monit unmonitor container_checker" >/dev/null 2>&1 || true


knob() { # $1=oper_status $2=alarm_status $3=alarm_text
    vm "echo '{\"linecard_oper_status\": $1, \"alarm_status\": $2, \"alarm_type\": 22, \"alarm_severity\": 5, \"alarm_text\": \"$3\"}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}
cleanup() { knob 0 1 "d1 cleanup"; vm "redis-cli -n 6 del 'CURALARM|#LOW_TEMPERATURE_ALARM'" >/dev/null; vm "sudo monit monitor container_checker" >/dev/null 2>&1; }
trap cleanup EXIT

# 键集快照:五库各自 scan|sort 存档
snap() { # $1=标签
    vm "redis-cli -n 1 --raw --scan --pattern '*' | sort" > "$EVDIR/ASIC_DB.$1"
    vm "redis-cli -n 2 --raw --scan --pattern '*' | sort" > "$EVDIR/COUNTERS_DB.$1"
    vm "redis-cli -n 5 --raw --scan --pattern '*' | sort" > "$EVDIR/FLEX_COUNTER_DB.$1"
    # STATE_DB 排除 PROCESS_STATS|<pid>(进程统计按 PID 记帐,进程重启天然翻动,与数据面无关)
    vm "redis-cli -n 6 --raw --scan --pattern '*' | grep -v '^PROCESS_STATS|' | sort" > "$EVDIR/STATE_DB.$1"
    vm "redis-cli -n 10 --raw --scan --pattern '*' | sort" > "$EVDIR/HISTORY_DB.$1"
    vm "redis-cli -n 10 --raw dbsize" > "$EVDIR/HISTORY_DB_size.$1"
}

# ── 0) 预检 ──────────────────────────────────────────────
vm true || die "VM 不可达"
vm "sudo docker ps --format '{{.Names}} {{.Status}}' | grep -q '^syncd Up'" || die "syncd 容器没在跑"
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . \
    || die "线卡记帐不存在(先跑 scripts/seed_linecard.sh)"

# ── 1) pre-state ─────────────────────────────────────────
PID0=$(vm "sudo docker exec syncd supervisorctl pid syncd")
snap pre
log "pre-state: pid=$PID0 ASIC_DB=$(wc -l < "$EVDIR/ASIC_DB.pre")键 COUNTERS_DB=$(wc -l < "$EVDIR/COUNTERS_DB.pre")键 STATE_DB=$(wc -l < "$EVDIR/STATE_DB.pre")键 HISTORY_DB=$(cat "$EVDIR/HISTORY_DB_size.pre")键"

# ── 2) 动作:容器重启 ─────────────────────────────────────
CURSOR=$(vm "sudo sh -c 'wc -l < /var/log/syslog'" | tr -d ' ')
log "docker restart syncd……(日志游标 $CURSOR)"
vm "sudo docker restart syncd" >/dev/null || die "restart 失败"

# 等回来:supervisor RUNNING + starting main loop(只看游标之后的行,杜绝旧日志冒充)
BACK=0
for _ in $(seq 1 90); do
    vm "sudo docker exec syncd supervisorctl status syncd" | grep -q RUNNING || { sleep 2; continue; }
    vm "sudo awk -v n=$CURSOR 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q 'starting main loop'" && BACK=1 && break
    sleep 2
done
[ "$BACK" = 1 ] || die "90s 内 syncd 未回到 starting main loop(查日志)"
log "syncd 已回到主循环"

# ── 3) 验收指纹:hardReinit 链(仅游标之后)────────────────
FP=0
for f in "prepareAsicState: read asic state m_asicKeys" "created LINECARD RID" "FlexCounterReiniter hard reinit" "FlexCounterGroupReiniter hard reinit"; do
    vm "sudo awk -v n=$CURSOR 'NR>n' /var/log/syslog | grep 'syncd#syncd#syncd' | grep -q \"$f\"" \
        && log "指纹 ✓ $f" || { fail "缺指纹: $f"; FP=1; }
done
# 记账健在性:重启窗口内 orchagent 级联可能冲掉 ASIC_DB(AGENTS 规则 4)——指纹虚报防线
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . \
    || die "重启后线卡记帐消失(orchagent 级联冲帐)——重跑 seed_linecard.sh 后再试"

# ── 4) post-state 对账 ───────────────────────────────────
# 等 2s 让重放后的写库动作全部落定,再快照
sleep 2
snap post

for DB in ASIC_DB COUNTERS_DB FLEX_COUNTER_DB STATE_DB HISTORY_DB; do
    if diff -q "$EVDIR/$DB.pre" "$EVDIR/$DB.post" >/dev/null 2>&1; then
        log "$DB 键集一致 ✓($(wc -l < "$EVDIR/$DB.post") 键)"
    else
        fail "$DB 键集漂移:"
        diff "$EVDIR/$DB.pre" "$EVDIR/$DB.post" | head -8 | sed 's/^/    /'
    fi
done

# ── 5) 恢复期告警端到端:唯一标记注入(db6 直查为先,日志为辅)──
# 旧版等模拟器 +10s 基线警报(HIGH_TEMPERATURE 固定名)——被 CURALARM 残留架空
# (redis 不死,键跨容器重启存活)。改为唯一标记注入,并以 STATE_DB 直查为先:
# NP 线程先写 CURALARM 后打日志(NP.cpp:518-519)——直查 text 字段 = 端到端写库的铁证,
# 零 rsyslog 落盘延迟;日志指纹只作辅助。守卫(already generated)查告警键不查文本,
# 注入前先 DEL 目标键拆守卫,并把模拟器归「已清除」态保证 S=0 必出边沿。
LOW_KEY='CURALARM|#LOW_TEMPERATURE_ALARM'
MARKER="D1RUN-$$-$(date +%s)"
log "注入恢复期告警(唯一标记 $MARKER;先拆守卫+归零态)……"
knob 0 1 "d1 settle"; sleep 6
vm "redis-cli -n 6 del '${LOW_KEY}'" >/dev/null
RV=""
for S in 0 1 0; do
    knob 0 "$S" "${MARKER}-$S"
    for _ in $(seq 1 12); do
        TXT=$(vm "redis-cli -n 6 --raw hget '${LOW_KEY}' text")
        if [ "$TXT" = "${MARKER}-$S" ]; then RV="$TXT" && break; fi
        sleep 1
    done
    [ -n "$RV" ] && break
done
[ -n "$RV" ] && log "恢复期告警端到端实证:STATE_DB CURALARM text='$RV'(NP 写库完成)" \
               || fail "恢复期告警未端到端落地(直查 12s×3 发皆无)"
if [ -n "$RV" ]; then
    vm "sudo grep 'syncd#syncd#syncd' /var/log/syslog | grep 'handler_alarm_generated' | grep -q \"${RV}\"" \
        && log "日志指纹辅助实证 ✓(rsyslog 已落盘)" \
        || log "日志指纹未及落盘(rsyslog 延迟,不影响 db6 铁证)"
fi
vm "redis-cli -n 6 del '${LOW_KEY}'" >/dev/null

PID2=$(vm "sudo docker exec syncd supervisorctl pid syncd")
log "post-state: pid=$PID2(重启后新进程,属预期)"

log "证据目录: $EVDIR"
if [ "$PASS" = 1 ]; then
    log "════ D1 PASS:容器重启全链验证通过(hardReinit 记帐重放 + 零丢失)════"
else
    log "════ D1 FAIL:存在未通过断言(见上 [FAIL] 行)════"
fi
exit $((1 - PASS))
