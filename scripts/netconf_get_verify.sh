#!/usr/bin/env bash
# netconf_get_verify.sh — OTN 告警 NETCONF GET 端到端驗證(T7 範圍)
#
# 架構: vslib 注入旋鈕(json)→ syncd → STATE_DB CURALARM
#        → netconfd(VM host :830, translib alarm_app)→ ncclient GET(WSL :1830 隧道)
#
# 前置: VM(otn-kvm)在跑;netconfd 裸進程在 VM host 跑著
#        (沒跑就: ssh VM 後 sudo /tmp/netconfd -port 830 &)
# 用法: ./scripts/netconf_get_verify.sh
# 可調: VM_SSH / NETCONF_PORT(隧道本地端口)環境變數
#
# 流程: 預檢 → 隧道 → 重置旋鈕靜默 → pre-clear → raise → ncclient GET → DB 對賬
# 注意: 結束後告警留在 CURALARM(人眼對照用);手動清除 = 再注入 alarm_status:1

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
NETCONF_PORT=${NETCONF_PORT:-1830}                # 本地隧道端口(830 在 WSL 屬保留段)
GNMI_BIN_DIR=/tmp/opencode
LOG=/tmp/opencode/netconf_get.$$.log
TUNNEL_MINE=0

log() { echo "[verify] $*"; }
die() { echo "[verify][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

cleanup() {
    [ "$TUNNEL_MINE" = 1 ] && pkill -f "ssh .*-L ${NETCONF_PORT}:localhost:830" 2>/dev/null
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
python3 -c "import ncclient" 2>/dev/null || die "ncclient 未安裝: python3 -m pip install --user --break-system-packages ncclient"
vm true || die "VM 不可達"
vm "ps aux | grep -q '[n]etconfd'" || die "netconfd 沒在 VM host 跑(ssh VM 後: sudo /tmp/netconfd -port 830 &)"
vm "sudo ss -tln | grep -q ':830 '" || die "VM :830 沒監聽"
vm "redis-cli -n 6 ping" | grep -q PONG || die "redis db6 不可達"
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
OP=$(vm "sudo docker exec syncd cat /usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > /usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json'"
sleep 6
log "旋鈕已重置靜默(oper_status=${OP})"

# ── 3) 注入序列:pre-clear(強制邊沿)→ raise ─────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > /usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json'"
}

inject 1 22 5 "verify pre-clear"; sleep 8; log "① pre-clear 已注入"
inject 0 22 5 "verify raise $(date +%H:%M:%S)"; sleep 8; log "② raise 已注入(vslib 5s 輪詢 + 上報)"

# ── 4) ncclient GET ─────────────────────────────────────
echo
echo "════ NETCONF GET(/openconfig-alarms:alarms)════"
python3 - "$NETCONF_PORT" <<'PYEOF' | tee "$LOG"
import sys
from ncclient import manager
port = int(sys.argv[1])
m = manager.connect(host="localhost", port=port, username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False,
                    allow_agent=False, look_for_keys=False)
r = m.get(filter=("xpath", "/openconfig-alarms:alarms"))
print(r.data_xml)
m.close_session()
PYEOF

# ── 5) DB 對賬 ──────────────────────────────────────────
echo
echo "════ VM 側 DB 對賬(人眼比對上方 XML)════"
echo "-- STATE_DB CURALARM 現存鍵:"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'
echo "-- 注入鍵的內容(text 應 = 上方 raise 的 text):"
vm "redis-cli -n 6 HGETALL 'CURALARM|linecard1#LOW_TEMPERATURE_ALARM'" | paste - - | sed 's/^/    /'
echo
log "完成(GET 原始輸出:$LOG)"
log "手動清此告警: inject 1 22 5 \"clear\"(參考腳本內 inject 用法)"
