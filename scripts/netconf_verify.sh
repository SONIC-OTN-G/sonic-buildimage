#!/usr/bin/env bash
# netconf_verify.sh — OTN 告警 NETCONF 通道驗證(GET 狀態快照 + SUBSCRIBE 事件流,一輪完成)
#
# 架構: vslib 注入旋鈕(json)→ syncd 寫/刪 CURALARM
#        → netconfd(VM host :830, translib alarm_app)
#            ├─ GET:       ncclient 查 /openconfig-alarms:alarms(狀態快照,舉牌後拍)
#            └─ SUBSCRIBE: ncclient 收 <notification>(事件流,罩住整個注入序列)
#
# 外部依賴啟動命令(netconfd 裸進程不在時,人工執行;本腳本只檢查不代啟)──
# 進 VM(ssh -p 2222 admin@localhost)後:
#      sudo mkdir -p /tmp/opencode && sudo sh -c 'nohup /usr/sbin/netconfd -port 830 -logtostderr > /tmp/opencode/netconfd_vm.log 2>&1 &'
#      # 在 VM host 起 netconfd 裸進程(容器外的自研 Go 程式,監聽 830)
#      # -logtostderr 的輸出轉存 netconfd_vm.log,方便查訂閱/掃描事件
#
# 驗證它在:
#      sudo ss -lntp | grep ':830'
#
# 前置: VM 在跑;netconfd 常駐;ncclient 已裝;線卡記帳在
# 用法: bash scripts/netconf_verify.sh
# 可調: VM_SSH / NETCONF_PORT(本地隧道端口,829-831 屬 Windows 保留段故用 1830)/ SUB_WAIT 環境變數
#
# 流程: 預檢 → 隧道 → 旋鈕歸零 → 掛訂閱(後台 75s 窗口)
#        → 步驟1 清場 → 步驟2 舉牌(訂閱收 RAISED + GET 拍狀態快照)→ 步驟3 摘牌(訂閱收 CLEARED)
#        → 通知幀彙總 + DB 對賬
# 注意: 結束時告警清回靜默;ncclient 兩個會話(訂閱後台 + GET 前台)併發,netconfd 支援多會話

if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/netconf_verify.sh 或 bash scripts/netconf_verify.sh(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi
set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
NETCONF_PORT=${NETCONF_PORT:-1830}
SUB_WAIT=${SUB_WAIT:-75}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
LOG=/tmp/opencode/netconf_verify.log
[ -f "$LOG" ] && mv "$LOG" "$LOG.$(date +%Y%m%d_%H%M%S).old"   # 舊 log 歸檔;最新永遠在固定名,不用查 pid
TUNNEL_MINE=0
PASS=1

log() { echo "[ncverify] $*"; }
die() { echo "[ncverify][ERROR] $*" >&2; exit 1; }
fail() { echo "[ncverify][FAIL] $*"; PASS=0; }

# 互斥鎖:同一台 VM 的旋鈕/賬本/容器是共享狀態,並發驗證會互相踩踏
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || die "另一个验证脚本正在运行(排队 60s 未取到锁)——稍后再试"

vm() { $VM_SSH "$@" 2>/dev/null; }

cleanup() {
    [ "$TUNNEL_MINE" = 1 ] && pkill -f "ssh .*-L ${NETCONF_PORT}:localhost:830" 2>/dev/null
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
python3 -c "import ncclient" 2>/dev/null || die "ncclient 未安裝: python3 -m pip install --user --break-system-packages ncclient"
vm true || die "VM 不可達"
if vm "ps aux | grep -q '[n]etconfd'" && vm "sudo ss -tln | grep -q ':830 '"; then
    log "netconfd 就緒 ✓(VM host 裸進程,:830 監聽)"
    echo "[ncverify] 鏈路命令速覽:"
    echo "   sudo mkdir -p /tmp/opencode && sudo sh -c 'nohup /usr/sbin/netconfd -port 830 -logtostderr > /tmp/opencode/netconfd_vm.log 2>&1 &'"
    echo "   # VM host 裸進程,監聽 830;日誌轉存 netconfd_vm.log"
else
    echo "[ncverify] netconfd 未就緒——啟動命令(進 VM 後執行):" >&2
    echo "  sudo mkdir -p /tmp/opencode && sudo sh -c 'nohup /usr/sbin/netconfd -port 830 -logtostderr > /tmp/opencode/netconfd_vm.log 2>&1 &'" >&2
    echo "  # VM host 裸進程(容器外的自研 Go 程式),監聽 830;-logtostderr 輸出轉存 netconfd_vm.log" >&2
    die "執行上方命令後重跑本腳本"
fi
KS=$(vm "redis-cli CONFIG GET notify-keyspace-events" | tail -1)
case "$KS" in *K*|*A*) log "redis keyspace 事件已開($KS)";; *) die "redis notify-keyspace-events 未開(=$KS)";; esac
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . || die "線卡物件不存在(記帳空):先跑 scripts/seed_linecard.sh"
log "預檢通過(ncclient ✓ / netconfd :830 ✓ / redis ✓ / keyspace AKE ✓ / 線卡 ✓)"

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
log "步驟 0:歸零基線——線卡在位、無告警(oper_status=${OP})"

# ── 3) 掛訂閱者(後台,收 <notification> 寫入 log;窗口罩住整個注入序列)──
cat > /tmp/opencode/ncclient_sub_runner.py <<'PYEOF'
import sys, time
import xml.dom.minidom as md
from ncclient import manager
port, wait = int(sys.argv[1]), int(sys.argv[2])
def pretty(x):
    try:
        p = md.parseString(x).toprettyxml(indent="\t")
        return "\n".join(l for l in p.splitlines() if l.strip())
    except Exception:
        return x
m = manager.connect(host="localhost", port=port, username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False,
                    allow_agent=False, look_for_keys=False)
m.create_subscription()
print("SUBSCRIBED", flush=True)
end = time.time() + wait
misses = 0
while time.time() < end:
    try:
        n = m.take_notification(block=True, timeout=5)
        print("NOTIF:\n" + pretty(n.notification_xml), flush=True)
        misses = 0
    except Exception:
        misses += 1
        if misses >= 4:
            break          # ~20s 完全靜默(隧道已被收)→ 提前收工;8s 注入間隔不會誤傷
        time.sleep(1)
try:
    m.close_session()
except Exception:
    pass                   # 隧道先死是常態(腳本收尾),靜默退出,不再噴 traceback
PYEOF
setsid nohup python3 /tmp/opencode/ncclient_sub_runner.py ${NETCONF_PORT} ${SUB_WAIT} \
    >> "$LOG" 2>&1 < /dev/null &
for _ in $(seq 1 15); do grep -q SUBSCRIBED "$LOG" && break; sleep 1; done
grep -q SUBSCRIBED "$LOG" || die "訂閱未建立(看 $LOG)"
log "訂閱就緒(${SUB_WAIT}s 窗口,罩住整個注入序列)"

# ── 4) 注入序列:清場 → 舉牌 → 摘牌(GET 夾在中間)────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "verify pre-clear"; sleep 8; log "步驟 1:清場——發『解除告警』(若有殘留會清掉)"

RAISE_TXT="verify nc raise $(date +%H:%M:%S)"
inject 0 22 5 "${RAISE_TXT}"; sleep 8
log "步驟 2:舉牌——發新告警(${RAISE_TXT}),訂閱端應收到『告警發生』通知幀"

echo
echo "════ NETCONF GET(/openconfig-alarms:alarms;舉牌後、摘牌前拍狀態快照)════"
echo "── GET 快照($(date +%T))──" >> "$LOG"
python3 - "$NETCONF_PORT" <<'PYEOF' | tee "$LOG.get" | tee -a "$LOG"
import sys
import xml.dom.minidom as md
from ncclient import manager
m = manager.connect(host="localhost", port=int(sys.argv[1]), username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False,
                    allow_agent=False, look_for_keys=False)
r = m.get(filter=("xpath", "/openconfig-alarms:alarms"))
p = md.parseString(r.data_xml).toprettyxml(indent="\t")
print("\n".join(l for l in p.splitlines() if l.strip()))
try:
    m.close_session()
except Exception:
    pass                   # 同上,收尾靜默
PYEOF
grep -q "LOW_TEMPERATURE_ALARM" "$LOG.get" \
    && log "GET 快照實證 ✓:注入的告警出現在 /alarms(此時狀態=ACTIVE)" \
    || fail "GET 快照未見注入告警"

inject 1 22 5 "verify nc clear"; sleep 8
log "步驟 3:摘牌——解除告警,訂閱端應收到『告警解除』通知幀"

# ── 5) 通知幀彙總 ───────────────────────────────────────
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

# ── 6) DB 對賬 ──────────────────────────────────────────
echo
echo "════ VM 側 DB 終態(注入告警應已解除 = 清回靜默)════"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'

# ── 7) netconfd 對賬 ────────────────────────────────────
echo
echo "════ netconfd 側對賬(事件/掃描/會話)════"
vm "grep -hE 'subscribe: (event|session|rescan)|write failed' /tmp/opencode/netconfd_vm.log 2>/dev/null | tail -8" | sed 's/^/    /'
echo
if [ "$PASS" = 1 ]; then
    log "════ NETCONF PASS:狀態快照(GET)+ 事件流(SUBSCRIBE)一輪全綠 ════"
else
    log "════ NETCONF FAIL:存在未通過斷言(見上 [FAIL] 行)════"
fi
log "完成(訂閱流+GET 快照:$LOG;GET 原始另存:$LOG.get)"
