#!/usr/bin/env bash
# gnmi_alarm_demo.sh — OTN 告警 gNMI 上報端到端演示(模式1: gNMI native 訂閱 STATE_DB)
#
# 架構: vslib 注入旋鈕(json)→ syncd 寫 CURALARM → gnmi 容器 → 訂閱端(本機 gnmic)
# 前置: VM(otn-kvm)在跑,gnmi/syncd 容器健康;WSL 有 gnmic 二進制
# 用法: ./scripts/gnmi_alarm_demo.sh
# 可調: VM_SSH / GNMI_ADDR / GNMI_BIN 環境變數
# 產出: 流上事件摘要(快照/UPDATE/DELETE)+ VM 側 DB 終態對賬
#
# 注意: 演示序列 = 重置靜默 → pre-clear → raise → clear
#       (先 pre-clear 是為了強制邊沿: vslib 對 alarm_status 跳變才發,同值不發)

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
GNMI_ADDR=${GNMI_ADDR:-"localhost:8080"}          # 經 SSH 隧道到 VM 的 gnmi 容器
GNMI_BIN=${GNMI_BIN:-$(command -v gnmic || echo /tmp/opencode/gnmic)}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
TUNNEL_PORT=8080
LOG=/tmp/opencode/gnmi_demo_sub.$$.log
TUNNEL_MINE=0
SUB_PID=

log() { echo "[demo] $*"; }
die() { echo "[demo][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

cleanup() {
    [ -n "$SUB_PID" ] && kill "$SUB_PID" 2>/dev/null
    [ "$TUNNEL_MINE" = 1 ] && pkill -f "ssh .*-L ${TUNNEL_PORT}:localhost:${TUNNEL_PORT}" 2>/dev/null
}
trap cleanup EXIT

# ── 0) 預檢 ──────────────────────────────────────────────
[ -x "$GNMI_BIN" ] || die "gnmic 不存在: $GNMI_BIN(可用 GNMI_BIN= 指定)"
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}}'" | grep -qx gnmi  || die "gnmi 容器沒在跑"
vm "sudo docker ps --format '{{.Names}}'" | grep -qx syncd || die "syncd 容器沒在跑"
log "預檢通過(gnmic=$GNMI_BIN)"

# ── 1) SSH 隧道(已開就複用)──────────────────────────────
if ! ss -tln 2>/dev/null | grep -q ":${TUNNEL_PORT} "; then
    $VM_SSH -f -N -L ${TUNNEL_PORT}:localhost:${TUNNEL_PORT} >/dev/null 2>&1 || die "建隧道失敗"
    TUNNEL_MINE=1
    sleep 1
    log "隧道已建(本機:${TUNNEL_PORT} → VM:${TUNNEL_PORT})"
else
    log "隧道已存在,複用(本機:${TUNNEL_PORT})"
fi

# ── 2) 重置注入旋鈕到靜默(保留 oper_status 原值)─────────
OP=$(vm "sudo docker exec syncd cat ${SIM_JSON}" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
sleep 6
log "旋鈕已重置靜默(oper_status=${OP})"

# ── 3) 掛訂閱(後台,90 秒自滅)───────────────────────────
"$GNMI_BIN" -a "$GNMI_ADDR" --insecure --encoding json_ietf \
    subscribe --path 'sonic-db:/STATE_DB/localhost/CURALARM' --stream-mode on-change \
    > "$LOG" 2>&1 &
SUB_PID=$!
for _ in $(seq 1 15); do grep -q sync-response "$LOG" && break; sleep 1; done
grep -q sync-response "$LOG" || die "訂閱未收到初始快照(看 $LOG)"
log "訂閱就緒,初始快照已收(路徑 sonic-db:/STATE_DB/localhost/CURALARM)"

# ── 4) 注入序列:pre-clear → raise → clear ───────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "demo pre-clear";  sleep 8; log "① pre-clear 已注入(強制邊沿到 INACTIVE)"
inject 0 22 5 "demo raise $(date +%H:%M:%S)"; sleep 8; log "② raise 已注入(期待 UPDATE 事件)"
inject 1 22 5 "demo clear";      sleep 8; log "③ clear 已注入(期待 DELETE 事件)"

# ── 5) 流上事件摘要 ─────────────────────────────────────
echo
echo "════ 訂閱端收到的事件流 ════"
python3 - "$LOG" <<'PYEOF'
import json, sys
txt = open(sys.argv[1]).read()
dec, i, evs = json.JSONDecoder(), 0, []
while i < len(txt):
    while i < len(txt) and txt[i] != '{':
        i += 1
    try:
        obj, i = dec.raw_decode(txt, i)
    except Exception:
        i += 1
        continue
    if 'sync-response' in obj:
        continue
    if 'updates' in obj:
        for u in obj['updates']:
            vals = u.get('values', {})
            if not vals:
                continue
            data = list(vals.values())[0]
            if not isinstance(data, dict) or not data:
                continue
            if len(data) > 1:   # 多鍵一次推 = 初始快照
                evs.append(('SNAPSHOT', u.get('Path', ''), f'{len(data)} 條現存告警(初始對賬)', ''))
            else:
                for key, f in data.items():
                    evs.append(('UPDATE', key, f.get('text', ''), f.get('severity', '')))
    if 'deletes' in obj:
        for d in obj['deletes']:
            evs.append(('DELETE', d, '(native 模式表級,不帶鍵名)', ''))
print(f"  {'事件':10} {'鍵/路徑':50} {'text':32} severity")
for e in evs:
    print(f"  {e[0]:10} {e[1][:50]:50} {e[2][:32]:32} {e[3]}")
PYEOF

# ── 6) VM 側 DB 終態對賬 ────────────────────────────────
echo
echo "════ VM 側 DB 終態 ════"
echo "-- STATE_DB CURALARM 現存鍵:"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'
echo "-- HISTORY_DB(db10) HISALARM 最近 2 條封存:"
vm "redis-cli -n 10 --scan --pattern 'HISALARM*' | tail -2" | sed 's/^/    /'
echo
log "完成(訂閱原始流:$LOG)"
