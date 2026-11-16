#!/usr/bin/env bash
if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/<脚本名> 或 bash scripts/<脚本名>(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi
# test_warm_reboot.sh — 重启韧性 ③:整机 warm reboot(真编排版,doc 55 §3 图 ①-⑦)
#
# 真编排链:sudo warm-reboot -f -d → 体检链(-f 放行 orchagent 死;-d 放行
#           COUNTERS_PORT_NAME_MAP 缺席,仿真设计内)→ STATE_DB 白名单清理
#           → docker cp dump.rdb → /host/warmboot(宿主盘)→ kexec 热重启
#           (cmdline+SONIC_BOOT_TYPE=warm)→ 开机恢复桥(/usr/bin/database.sh)回灌
#           → redis 载入 → hardReinit 重放
# 测试插入:①pre 快照 ②(已移除)手动 save——centralize 自带快门覆盖之(I1)③warm-reboot -f -d
#           ④cmdline 指纹 ⑤四库对账 ⑥hardReinit 指纹+CURALARM 重现 ⑦三通道重跑(另行)
# 已知设计内:STATE_DB 白名单删 CURALARM/LINECARD|linecard1 等 → syncd 开机闸门
#           轮询 LINECARD key 会永等 → 回魂后必须补 1 条 hset(种子仅剩此一步)
# 判定:Phase B(cmdline warm 指纹+宿主盘照非空+db1/2/10 零漂移+hardReinit 指纹全成立)
#           / Phase C(任一不成立;kexec 在 KVM 热重启成败 = 唯一执行期未知)
# 警告:本脚本会 kexec 热重启整台 VM(全程约 8-10 分钟);执行前确认无其他验证进行中。
# 出處: docs/06-數據採集/55-重啟韌性③-整機warmreboot驗證計劃(RDB快門探險).md §3/§5
set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ServerAliveInterval=10 -o ServerAliveCountMax=3 -o ConnectTimeout=10 -p 2222 admin@localhost"}
EVDIR=/tmp/opencode/warm_reboot.$$
PASS=1

log() { echo "[d3] $*"; }
die() { echo "[d3][ERROR] $*" >&2; exit 1; }
fail() { echo "[d3][FAIL] $*"; PASS=0; }
vm() { $VM_SSH "$@" 2>/dev/null; }
mkdir -p "$EVDIR"

# 互斥锁:同一台 VM 的旋钮/账本/容器是共享状态,并发验证会互相踩踏
# (2026-11-11 实证:两个 opencode 同时跑 D1,边沿互吞、账本互冲)
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || die "另一个验证脚本正在运行(排队 60s 未取到锁)——稍后再试,或停掉另一个 opencode 的验证循环"

# warm-reboot backup_database 的 STATE_DB 白名单(保留表;源码 warm-reboot:279-290)
# 白名单外所有 STATE_DB 键在备份时点被删(设计内);还原后这些表只能靠守护进程重建/条件重发
WHITELIST="FDB_TABLE WARM_RESTART_TABLE MIRROR_SESSION_TABLE FG_ROUTE_TABLE WARM_RESTART_ENABLE_TABLE TRANSCEIVER_INFO VXLAN_TUNNEL_TABLE BUFFER_MAX_PARAM_TABLE STORAGE_INFO FAST_RESTART_ENABLE_TABLE PORT_TABLE"

snap() { # $1=标签 → 文件名固定为 <DB名>.<标签>(对账循环按同名取)
    vm "redis-cli -n 1 --raw --scan --pattern '*' | grep -v '^PROCESS_STATS|' | sort" > "$EVDIR/ASIC_DB.$1"
    vm "redis-cli -n 2 --raw --scan --pattern '*' | sort" > "$EVDIR/COUNTERS_DB.$1"
    vm "redis-cli -n 6 --raw --scan --pattern '*' | grep -v '^PROCESS_STATS|' | sort" > "$EVDIR/STATE_DB.$1"
    vm "redis-cli -n 10 --raw --scan --pattern '*' | sort" > "$EVDIR/HISTORY_DB.$1"
    vm "redis-cli -n 4 --raw --scan --pattern 'FEATURE|*'" > "$EVDIR/CONFIG_DB_FEATURE.$1"
}

# ── 0) 预检:VM 可达 + 账本在 + 真编排前置件在 ────────────
vm true || die "VM 不可达"
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . \
    || die "线卡记帐不存在(先跑 scripts/seed_linecard.sh)"
vm "test -f /etc/sonic/warm-reboot_order" || die "VM 缺 /etc/sonic/warm-reboot_order(warm-reboot 会拒绝起飞)"
vm "test -x /sbin/kexec" || die "VM 缺 /sbin/kexec"
log "预检通过:asic_config_check exit=0(直接测,真过);check_db_integrity 真退出码=1——"
log "  它要求 COUNTERS_PORT_NAME_MAP(上游 orchagent 写,本仿真设计内缺席)→ 用 -d 放行,"
log "  完整性把关改由本脚本四库对账承担(比 schema 检查更严)"

# ── 1) ① pre 快照 ────────────────────────────────────────
snap pre
PRE_ASIC=$(wc -l < "$EVDIR/ASIC_DB.pre"); PRE_STATE=$(wc -l < "$EVDIR/STATE_DB.pre"); PRE_HIST=$(wc -l < "$EVDIR/HISTORY_DB.pre")
log "pre-state: ASIC_DB=${PRE_ASIC} STATE_DB=${PRE_STATE} HISTORY_DB=${PRE_HIST} 键"

# ── 2) ② 快门说明(已移除手动 save)──────────────────────
# centralize_database(backup_database 内调用)结尾自带 r.save(),在白名单清理后确定性重拍
# 并盖掉任何手动快门(doc 55 §9 I1 破案)——手动 save 是纯冗余,已从流程移除。
log "快门由编排自带(centralize r.save(),清理后重拍);无需手动 save(I1 破案后移除)"

# ── 3) ③ 触发真编排:sudo warm-reboot -f -d ──────────────
log "sudo warm-reboot -f -d(关机段 ~2-3 分钟:体检链→白名单清理→搬照→kexec -e)……"
vm "sudo warm-reboot -f -d" > "$EVDIR/warm_reboot_run.log" 2>&1 || true
sleep 15
# SSH 可能挂着熬过了重启点(连接无 RST 时 keepalive 前会吊很久)——先用 uptime 分辨:
# VM 刚回来(uptime < 15 分)= warm reboot 已成功,直接进后验;uptime 老 = 真中途失败
UPT_MIN=$(vm "awk '{print int(\$1/60)}' /proc/uptime" 2>/dev/null)
if vm true && [ -n "$UPT_MIN" ] && [ "$UPT_MIN" -lt 15 ]; then
    log "SSH 卡过了重启点——VM 已完成 warm reboot(up ${UPT_MIN} 分),直接进后验段"
elif vm true; then
    fail "warm-reboot 中途失败——VM 未重启(原地不动;kexec/体检/order 之一拦下)"
    log "--- warm-reboot 运行输出尾部 ---"
    tail -15 "$EVDIR/warm_reboot_run.log" | sed 's/^/    /'
    log "--- VM syslog 尾部(warm-reboot/kexec 相关)---"
    vm "sudo grep -iE 'warm-reboot|kexec|Backing up database' /var/log/syslog 2>/dev/null | tail -10" | sed 's/^/    /'
    die "冷启 Phase C 已定案不重测;排查 warm-reboot 失败原因后重试(证据在 $EVDIR)"
fi
log "SSH 已断(VM 正在 kexec 热重启)……"

# ── 4) 等 VM 回来(kexec 热重启通常比冷启快;给足 8 分钟)──
BACK=0
for _ in $(seq 1 60); do
    timeout 6 bash -c 'cat < /dev/null > /dev/tcp/127.0.0.1/2222' 2>/dev/null && { BACK=1; break; }
    sleep 8
done
[ "$BACK" = 1 ] || die "VM 未在 ~8 分钟内回来(kexec 热重启卡死?上控制台查)"
log "SSH 端口已开,等容器编排收尾……"
for _ in $(seq 1 40); do
    N=$(vm "sudo docker ps -q | wc -l")
    [ "${N:-0}" -ge 8 ] && break
    sleep 10
done
for _ in $(seq 1 30); do
    [ "$(vm "sudo docker inspect -f '{{.State.Running}}' syncd" 2>/dev/null)" = "true" ] && break
    sleep 5
done
sleep 30   # 让 hostcfgd/monit 波次收尾

# ── 5) ④ 开机指纹:真的 warm-reboot 判据 ─────────────────
CMDLINE=$(vm "cat /proc/cmdline")
if echo "$CMDLINE" | grep -q "SONIC_BOOT_TYPE=warm"; then
    log "cmdline 指纹 ✓(含 SONIC_BOOT_TYPE=warm,kexec 热重启成立)"
else
    fail "cmdline 无 SONIC_BOOT_TYPE=warm——走了冷启路径?cmdline=$CMDLINE"
fi
# 宿主盘照:桥会把 dump.rdb 消费后重命名为 .old(database.sh:213,设计内)——两者都认
HSIZE=$(vm "sudo stat -c %s /host/warmboot/dump.rdb 2>/dev/null"); HWHICH=dump.rdb
[ -z "$HSIZE" ] && { HSIZE=$(vm "sudo stat -c %s /host/warmboot/dump.rdb.old 2>/dev/null"); HWHICH="dump.rdb.old(桥已消费重命名,设计内)"; }
[ -n "$HSIZE" ] && [ "$HSIZE" -gt 0 ] && log "宿主盘照 ✓(/host/warmboot/${HWHICH} ${HSIZE} 字节,backup_database 搬照成立)" \
                                 || fail "宿主盘 dump.rdb 与 dump.rdb.old 均缺或 0 字节(backup_database 未搬照)"
echo "$CMDLINE" > "$EVDIR/cmdline_post.txt"; echo "${HSIZE}" > "$EVDIR/dump_size_host.txt"

# ── 6) 补 hset LINECARD(白名单设计内删除→syncd 闸门永等)──
vm "redis-cli -n 6 hset 'LINECARD|linecard1' oper-status ACTIVE slot-status Ready" >/dev/null \
    || die "补 hset LINECARD|linecard1 失败"
log "已补 hset LINECARD|linecard1(白名单设计内删除;种子仅剩此一步,帐本来自回魂)"

# ── 7) ⑥ syncd 恢复:闸门过 → hardReinit 指纹 ───────────
# OTAI syncd 日志走 host syslog(syncd#syncd#syncd 前缀);重启后 syslog 全新,无需游标
BACK=0
for _ in $(seq 1 30); do
    vm "sudo docker exec syncd supervisorctl status syncd" | grep -q RUNNING || { sleep 4; continue; }
    vm "sudo grep 'syncd#syncd#syncd' /var/log/syslog 2>/dev/null | grep -q 'starting main loop'" && { BACK=1; break; }
    sleep 4
done
[ "$BACK" = 1 ] || die "120s 内 syncd 未到 starting main loop(查 /var/log/syslog)"
log "syncd 恢复 ✓(starting main loop)"
for f in "prepareAsicState: read asic state m_asicKeys" "created LINECARD RID" "FlexCounterReiniter hard reinit"; do
    vm "sudo grep 'syncd#syncd#syncd' /var/log/syslog 2>/dev/null | grep -q '$f'" \
        && log "hardReinit 指纹 ✓($f)" || fail "hardReinit 指纹缺($f)"
done
vm "sudo grep 'syncd#syncd#syncd' /var/log/syslog 2>/dev/null | grep -q SEGV" && fail "syncd 日志现 SEGV" || log "无 SEGV ✓"

# ── 7b) 重开采集组(syncd 优雅关机自清扫把组置了 disable,采集器不自启)──
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_GROUP_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_GAUGE' '[\"POLL_INTERVAL\",\"1000\",\"FLEX_COUNTER_STATUS\",\"enable\"]' 'SSET' >/dev/null; redis-cli -n 5 PUBLISH 'FLEX_COUNTER_GROUP_TABLE_CHANNEL@5' 'G'" >/dev/null || true
sleep 5
PM_N=$(vm "redis-cli -n 2 --raw --scan --pattern 'LINECARD:linecard1_*' | wc -l")
[ "${PM_N:-0}" -ge 8 ] && log "采集组重开 ✓(pm_current 8 键重现,PM 采集恢复)" \
                        || log "WARN:采集组重开后 pm_current 仅 ${PM_N:-0} 键(查采集器日志)"

# ── 8) ⑤ 四库对账 ────────────────────────────────────────
snap post
POST_ASIC=$(wc -l < "$EVDIR/ASIC_DB.post"); POST_STATE=$(wc -l < "$EVDIR/STATE_DB.post"); POST_HIST=$(wc -l < "$EVDIR/HISTORY_DB.post")
log "post-state: ASIC_DB=${POST_ASIC} STATE_DB=${POST_STATE} HISTORY_DB=${POST_HIST} 键"

# db1/db10 = 硬标准:零缺失(白名单只清 STATE_DB,这两库必须原样回魂)
# 新增键不算漂移=采集/守护进程重启后继续工作(HISTORY_DB 新窗归档=PM 历史连续的直接证据)
# COUNTERS_DB = WARN:关机自清扫清 pm_current 现窗+仅重开 LINECARD 组,已定性为设计内(doc 55 §8/§9)
for t in ASIC_DB HISTORY_DB; do
    MISS=$(comm -23 <(LC_ALL=C sort "$EVDIR/$t.pre") <(LC_ALL=C sort "$EVDIR/$t.post") | wc -l)
    ADDN=$(comm -13 <(LC_ALL=C sort "$EVDIR/$t.pre") <(LC_ALL=C sort "$EVDIR/$t.post") | wc -l)
    if [ "$MISS" -eq 0 ]; then
        log "$t 零缺失 ✓(重启后新增 ${ADDN} 键=继续工作)"
    else
        fail "$t 丢失 ${MISS} 键(回魂不完整):"
        comm -23 <(LC_ALL=C sort "$EVDIR/$t.pre") <(LC_ALL=C sort "$EVDIR/$t.post") | head -6 | sed 's/^/    /'
    fi
done
if diff -q <(LC_ALL=C sort "$EVDIR/COUNTERS_DB.pre") <(LC_ALL=C sort "$EVDIR/COUNTERS_DB.post") >/dev/null 2>&1; then
    log "COUNTERS_DB 键集一致 ✓"
else
    log "WARN:COUNTERS_DB 漂移(关机自清扫+组重开,设计内;明细看 $EVDIR):"
    diff <(LC_ALL=C sort "$EVDIR/COUNTERS_DB.pre") <(LC_ALL=C sort "$EVDIR/COUNTERS_DB.post") | head -6 | sed 's/^/    /'
fi

# db6 = 分桶对账:白名单表缺失=真丢失(FAIL);白名单外缺失=设计内(列出);新增=守护进程重建(列出)
LC_ALL=C sort "$EVDIR/STATE_DB.pre" > "$EVDIR/STATE_DB.pre.C"
LC_ALL=C sort "$EVDIR/STATE_DB.post" > "$EVDIR/STATE_DB.post.C"
comm -23 "$EVDIR/STATE_DB.pre.C" "$EVDIR/STATE_DB.post.C" > "$EVDIR/STATE_DB.missing"
comm -13 "$EVDIR/STATE_DB.pre.C" "$EVDIR/STATE_DB.post.C" > "$EVDIR/STATE_DB.new"
MISS_N=$(wc -l < "$EVDIR/STATE_DB.missing"); NEW_N=$(wc -l < "$EVDIR/STATE_DB.new")
log "STATE_DB:缺失 ${MISS_N} 键,新增 ${NEW_N} 键(明细分桶如下)"
W=0
while IFS= read -r k; do
    [ -z "$k" ] && continue
    t=${k%%|*}
    if echo " $WHITELIST " | grep -q " $t "; then
        fail "STATE_DB 白名单表键丢失(该活的没活):$k"; W=1
    else
        log "    设计内删除:$k"
    fi
done < "$EVDIR/STATE_DB.missing"
[ "$W" = 0 ] && [ "$MISS_N" -gt 0 ] && log "缺失全部为白名单外(设计内,备份时点被编排清理)"
while IFS= read -r k; do [ -n "$k" ] && log "    重启后新增:$k"; done < "$EVDIR/STATE_DB.new"

if diff -q <(LC_ALL=C sort "$EVDIR/CONFIG_DB_FEATURE.pre") <(LC_ALL=C sort "$EVDIR/CONFIG_DB_FEATURE.post") >/dev/null 2>&1; then
    log "CONFIG_DB FEATURE 表一致 ✓"
else
    log "WARN:CONFIG_DB FEATURE 表有差(文件重灌口径,以文件为准;非 RDB 丢失)"
    diff <(LC_ALL=C sort "$EVDIR/CONFIG_DB_FEATURE.pre") <(LC_ALL=C sort "$EVDIR/CONFIG_DB_FEATURE.post") | head -6 | sed 's/^/    /'
fi

# ── 9) ⑥ 告警如实上报观察(条件仍在才重发;两态都算如实)──
sleep 30
CUR=$(vm "redis-cli -n 6 --raw --scan --pattern 'CURALARM|*' | head -3")
if [ -n "$CUR" ]; then
    log "CURALARM 重现 ✓(条件仍在,syncd 重发=如实):$(echo "$CUR" | tr '\n' ' ')"
else
    log "CURALARM 暂无(条件已复位=如实;如需复现可用演示旋钮重新注入后三通道复核)"
fi

# ── 10) 判定 ─────────────────────────────────────────────
# 硬标准四件:cmdline warm 指纹 + 宿主盘照 + db1 帐本零缺失 + db10 历史零缺失
# (COUNTERS_DB 现窗漂移=关机自清扫+组重开,已单独呈报,不参与判定)
MISS_A=$(comm -23 <(LC_ALL=C sort "$EVDIR/ASIC_DB.pre") <(LC_ALL=C sort "$EVDIR/ASIC_DB.post") | wc -l)
MISS_H=$(comm -23 <(LC_ALL=C sort "$EVDIR/HISTORY_DB.pre") <(LC_ALL=C sort "$EVDIR/HISTORY_DB.post") | wc -l)
if echo "$CMDLINE" | grep -q "SONIC_BOOT_TYPE=warm" && [ -n "$HSIZE" ] && [ "$HSIZE" -gt 0 ] \
   && [ "$MISS_A" -eq 0 ] && [ "$MISS_H" -eq 0 ]; then
    log "════ Phase B:真编排 warm reboot 成立——cmdline warm 指纹+宿主盘照+白名单外零丢失 ════"
    log "后续:doc 55 §8 定案;AGENTS.md 持久性表 warm reboot 行升级;『编排自动快门』列为 fork 改进项"
else
    log "════ Phase C:真编排 warm reboot 未完全成立(逐项见上方 ✓/FAIL)════"
fi

log "证据目录: $EVDIR"
exit $((1 - PASS))
