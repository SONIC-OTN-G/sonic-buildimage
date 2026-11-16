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

# ── 外部依賴(訂閱端二進制;腳本經 GNMI_BIN 變數找它)──────────────────
# WSL 執行,僅首次準備(/tmp 重啟即丟,丟了重做):
#      tar -xzf /tmp/opencode/gnmic.tar.gz -C /tmp/opencode/
#      # 解出訂閱端 gnmic(開源 gNMI 客戶端,扮演網管側訂閱者)
#      # 原始 tar.gz 來源:gnmic 官方 release v0.49.1(gnmic_0.49.1_Linux_x86_64.tar.gz,
#      # 已存在於 /tmp/opencode/,正常情況不需重抓)

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
GNMI_ADDR=${GNMI_ADDR:-"localhost:8080"}          # 經 SSH 隧道到 VM 的 gnmi 容器
GNMI_BIN=${GNMI_BIN:-$(command -v gnmic || echo /tmp/opencode/gnmic)}
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json
TUNNEL_PORT=8080
LOG=/tmp/opencode/gnmi_demo_sub.log
[ -f "$LOG" ] && mv "$LOG" "$LOG.$(date +%Y%m%d_%H%M%S).old"   # 舊 log 歸檔;最新永遠在固定名,不用查 pid
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
if [ -x "$GNMI_BIN" ]; then
    log "gnmic 就緒 ✓($GNMI_BIN)"
    echo "[demo] 鏈路命令速覽:"
    echo "   $GNMI_BIN"
    echo "   # gNMI 訂閱客戶端(網管側);腳本稍後以它掛訂閱(經 SSH 隧道 :8080)"
else
    echo "[demo] gnmic 未就緒——準備命令(WSL 執行):" >&2
    echo "  tar -xzf /tmp/opencode/gnmic.tar.gz -C /tmp/opencode/" >&2
    echo "  # 解出訂閱端 gnmic(開源 gNMI 客戶端,扮演網管側訂閱者)" >&2
    echo "  # 原始 tar.gz 來源:gnmic 官方 release v0.49.1(壓縮包已在 /tmp/opencode/)" >&2
    die "執行上方命令後重跑本腳本(或 GNMI_BIN=<路徑> 指定別處)"
fi
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}}'" | grep -qx gnmi  || die "gnmi 容器沒在跑"
vm "sudo docker ps --format '{{.Names}}'" | grep -qx syncd || die "syncd 容器沒在跑"
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . || die "線卡物件不存在(記帳空):先跑 scripts/seed_linecard.sh"
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
log "步驟 0:歸零基線——線卡在位、無告警(oper_status=${OP})"

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

inject 1 22 5 "demo pre-clear";  sleep 8; log "步驟 1:清場——發『解除告警』強制出一個邊沿"
inject 0 22 5 "demo raise $(date +%H:%M:%S)"; sleep 8; log "步驟 2:舉牌——發新告警,訂閱端應收到 UPDATE 事件"
inject 1 22 5 "demo clear";      sleep 8; log "步驟 3:摘牌——解除告警,訂閱端應收到 DELETE 事件"

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
