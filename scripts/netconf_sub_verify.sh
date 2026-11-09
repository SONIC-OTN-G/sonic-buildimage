#!/usr/bin/env bash
# netconf_sub_verify.sh — OTN 告警 NETCONF SUBSCRIBE 端到端驗證(T8 範圍)
#
# 架構: vslib 注入旋鈕(json)→ syncd 寫/刪 CURALARM
#        → redis keyspace 事件 → netconfd PSubscribe watcher
#        → diff 緩存(RAISED/CLEARED)→ <notification> 推訂閱會話(WSL ncclient)
#
# 前置: VM(otn-kvm)在跑;netconfd 常駐(容器或 VM host 裸跑)
# 用法: ./scripts/netconf_sub_verify.sh
# 可調: VM_SSH / NETCONF_PORT(隧道本地端口,829-831 屬 Windows 保留段故用 1830)
#
# 流程: 預檢 → 隧道 → 重置旋鈕 → 掛訂閱(後台 60s)→ pre-clear → raise → clear
#        → 打印收到的 <notification> 幀 + netconfd 日誌對賬
# 注意: 腳本結束時告警已清回靜默;訂閱者超時自動退出

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
NETCONF_PORT=${NETCONF_PORT:-1830}
SUB_WAIT=${SUB_WAIT:-50}                           # 訂閱者存活秒數
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
LOG=/tmp/opencode/netconf_sub.$$.log
TUNNEL_MINE=0

log() { echo "[subverify] $*"; }
die() { echo "[subverify][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

cleanup() {
    [ "$TUNNEL_MINE" = 1 ] && pkill -f "ssh .*-L ${NETCONF_PORT}:localhost:830" 2>/dev/null
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
python3 -c "import ncclient" 2>/dev/null || die "ncclient 未安裝: python3 -m pip install --user --break-system-packages ncclient"
vm true || die "VM 不可達"
vm "ps aux | grep -q '[n]etconfd'" || die "netconfd 沒在跑(ssh VM 後: sudo /tmp/netconfd -port 830 &)"
vm "sudo ss -tln | grep -q ':830 '" || die "VM :830 沒監聽"
KS=$(vm "redis-cli CONFIG GET notify-keyspace-events" | tail -1)
case "$KS" in *K*|*A*) log "redis keyspace 事件已開($KS)";; *) die "redis notify-keyspace-events 未開(=$KS)";; esac
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . || die "線卡物件不存在(記帳空):先跑 scripts/seed_linecard.sh"
log "預檢通過"

# ── 1) SSH 隧道(已開就複用)──────────────────────────────
if ! ss -tln 2>/dev/null | grep -q ":${NETCONF_PORT} "; then
    $VM_SSH -f -N -L ${NETCONF_PORT}:localhost:830 >/dev/null 2>&1 || die "建隧道失敗"
    TUNNEL_MINE=1
    sleep 1
    log "隧道已建(本機:${NETCONF_PORT} → VM:830)"
else
    log "隧道已存在,複用(本機:${NETCONF_PORT})"
fi

# ── 2) 重置注入旋鈕到靜默(保留 oper_status 原值)─────────
OP=$(vm "sudo docker exec syncd cat ${SIM_JSON}" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
sleep 6
log "旋鈕已重置靜默(oper_status=${OP})"

# ── 3) 掛訂閱者(後台,收 <notification> 寫入 log)────────
cat > /tmp/opencode/ncclient_sub_runner.py <<'PYEOF'
import sys, time
from ncclient import manager
port, wait = int(sys.argv[1]), int(sys.argv[2])
m = manager.connect(host="localhost", port=port, username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False,
                    allow_agent=False, look_for_keys=False)
m.create_subscription()
print("SUBSCRIBED", flush=True)
end = time.time() + wait
while time.time() < end:
    try:
        n = m.take_notification(block=True, timeout=5)
        print("NOTIF:", n.notification_xml, flush=True)
    except Exception:
        pass
m.close_session()
PYEOF
setsid nohup python3 /tmp/opencode/ncclient_sub_runner.py ${NETCONF_PORT} ${SUB_WAIT} \
    > "$LOG" 2>&1 < /dev/null &
for _ in $(seq 1 15); do grep -q SUBSCRIBED "$LOG" && break; sleep 1; done
grep -q SUBSCRIBED "$LOG" || die "訂閱未建立(看 $LOG)"
log "訂閱就緒(${SUB_WAIT}s 窗口,等待 <notification>)"

# ── 4) 注入序列:pre-clear → raise → clear ───────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "verify pre-clear"; sleep 8; log "① pre-clear 已注入"
inject 0 22 5 "verify sub raise $(date +%H:%M:%S)"; sleep 8; log "② raise 已注入(期待 RAISED 幀)"
inject 1 22 5 "verify sub clear"; sleep 8; log "③ clear 已注入(期待 CLEARED 幀,欄位由緩存補全)"

# ── 5) 通知摘要 ─────────────────────────────────────────
echo
echo "════ 訂閱端收到的 <notification> ════"
python3 - "$LOG" <<'PYEOF'
import re, sys
txt = open(sys.argv[1]).read()
notifs = re.findall(r"<notification.*?</notification>", txt, re.S)
if not notifs:
    print("  (未收到任何通知幀!)")
for i, n in enumerate(notifs, 1):
    status = (re.search(r"<status>(\w+)</status>", n) or [None, "?"])[1]
    etime  = (re.search(r"<eventTime>([^<]+)</eventTime>", n) or [None, "?"])[1]
    rid    = (re.search(r"<id>([^<]+)</id>", n) or [None, "?"])[1]
    sev    = (re.search(r"<severity>([^<]+)</severity>", n) or [None, "?"])[1]
    text   = (re.search(r"<text>([^<]*)</text>", n) or [None, "?"])[1]
    print(f"  {i}. [{status}] {rid}  {sev}  text={text!r}  eventTime={etime}")
PYEOF

# ── 6) netconfd 對賬 ────────────────────────────────────
echo
echo "════ netconfd 側對賬(事件/掃描/會話)════"
vm "sudo sh -c 'cat \$(ls -t /tmp/netconfd.sonic.root.log.INFO.* 2>/dev/null | head -1) /tmp/netconfd.log 2>/dev/null | grep -hE \"subscribe: (event|session|rescan)|write failed\" | tail -8'" | sed 's/^/    /'
echo
log "完成(訂閱原始流:$LOG)"
