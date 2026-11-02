#!/usr/bin/env bash
# netconf_alarm_demo.sh — OTN 告警 NETCONF 一鍵端到端演示(GET + 订阅推送)
#
# 链路: vslib 注入旋鈕(json)→ syncd → STATE_DB CURALARM
#        → netconfd(容器常駐 :830)→ NMS 侧 ncclient
#           ├─ GET   /openconfig-alarms:alarms(现狀查詢,告警表渲染)
#           └─ create-subscription → <notification> 实时推送
#
# 前置: VM 在跑;netconfd 容器常駐(supervisorctl status = RUNNING)
# 用法: ./scripts/netconf_alarm_demo.sh
# 可調: VM_SSH / NETCONF_PORT(本地隧道端口,829-831 属 Windows 保留段)
#
# 流程: 預檢 → 隧道 → 重置 → 掛訂閱 → pre-clear → raise → GET(對賬)
#        → clear → 通知摘要 → DB 終態
# 注意: 結束時告警清回靜默;訂閱者 60s 自退;隧道自清

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
NETCONF_PORT=${NETCONF_PORT:-1830}
SUB_WAIT=${SUB_WAIT:-60}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
LOG=/tmp/opencode/netconf_demo.$$.log
TUNNEL_MINE=0

log() { echo "[demo] $*"; }
die() { echo "[demo][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

cleanup() {
    [ "$TUNNEL_MINE" = 1 ] && pkill -f "ssh .*-L ${NETCONF_PORT}:localhost:830" 2>/dev/null
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
python3 -c "import ncclient" 2>/dev/null || die "ncclient 未安裝"
vm true || die "VM 不可達"
vm "sudo docker exec mgmt-framework supervisorctl status netconfd" | grep -q RUNNING \
    || die "容器內 netconfd 非 RUNNING(supervisorctl status 檢查)"
vm "redis-cli CONFIG GET notify-keyspace-events" | tail -1 | grep -qE "K|A" \
    || die "redis keyspace 事件未開"
log "預檢通過(容器內 netconfd RUNNING)"

# ── 1) SSH 隧道(已開就複用)──────────────────────────────
if ! ss -tln 2>/dev/null | grep -q ":${NETCONF_PORT} "; then
    $VM_SSH -f -N -L ${NETCONF_PORT}:localhost:830 >/dev/null 2>&1 || die "建隧道失敗"
    TUNNEL_MINE=1
    sleep 1
    log "隧道已建(本機:${NETCONF_PORT} → VM:830)"
else
    log "隧道已存在,複用(本機:${NETCONF_PORT})"
fi

# ── 2) 重置注入旋鈕到靜默 ────────────────────────────────
OP=$(vm "sudo docker exec syncd cat ${SIM_JSON}" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
sleep 6
log "旋鈕已重置靜默(oper_status=${OP})"

# ── 3) 掛訂閱者(後台)───────────────────────────────────
cat > /tmp/opencode/ncclient_demo_sub.py <<'PYEOF'
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
setsid nohup python3 /tmp/opencode/ncclient_demo_sub.py ${NETCONF_PORT} ${SUB_WAIT} \
    > "$LOG" 2>&1 < /dev/null &
for _ in $(seq 1 15); do grep -q SUBSCRIBED "$LOG" && break; sleep 1; done
grep -q SUBSCRIBED "$LOG" || die "訂閱未建立"
log "訂閱就緒(${SUB_WAIT}s 窗口)"

# ── 4) 注入 + GET + clear ────────────────────────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "demo pre-clear"; sleep 8; log "① pre-clear 已注入"
inject 0 22 5 "demo raise $(date +%H:%M:%S)"; sleep 8; log "② raise 已注入(訂閱端應收 RAISED)"

echo
echo "════ NMS 侧 GET(告警表渲染)════"
python3 - "$NETCONF_PORT" <<'PYEOF'
import sys
from ncclient import manager
from lxml import etree
m = manager.connect(host="localhost", port=int(sys.argv[1]), username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False,
                    allow_agent=False, look_for_keys=False)
r = m.get(filter=("xpath", "/openconfig-alarms:alarms"))
NS = "{http://openconfig.net/yang/alarms}"
root = etree.fromstring(r.data_xml.encode())   # str+declaration 需转 bytes
print(f"{'SEVERITY':10}| {'RESOURCE':12}| TEXT   [id]")
for a in root.iter(NS + "alarm"):
    sev = a.findtext(NS + "state/" + NS + "severity")
    res = a.findtext(NS + "state/" + NS + "resource")
    txt = a.findtext(NS + "state/" + NS + "text")
    rid = a.findtext(NS + "id")
    print(f"{sev:10}| {res:12}| {txt}   [{rid}]")
m.close_session()
PYEOF

inject 1 22 5 "demo clear"; sleep 8; log "③ clear 已注入(訂閱端應收 CLEARED)"

# ── 5) 訂閱端通知摘要 ────────────────────────────────────
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
    rid    = (re.search(r"<id>([^<]+)</id>", n) or [None, "?"])[1]
    sev    = (re.search(r"<severity>([^<]+)</severity>", n) or [None, "?"])[1]
    text   = (re.search(r"<text>([^<]*)</text>", n) or [None, "?"])[1]
    print(f"  {i}. [{status}] {rid}  {sev}  text={text!r}")
PYEOF

# ── 6) DB 終態 ──────────────────────────────────────────
echo
echo "════ VM 側 DB 終態(應無 LOW_TEMPERATURE_ALARM = 已清回靜默)════"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'
echo
log "完成(訂閱原始流:$LOG)"
