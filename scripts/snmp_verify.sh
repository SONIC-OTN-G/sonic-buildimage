#!/usr/bin/env bash
# snmp_verify.sh — OTN 告警 SNMP trap 端到端驗證(S1–S5 範圍)
#
# 链路: vslib 注入旋鈕(json)→ syncd 寫/刪 CURALARM
#        → redis keyspace 事件 → alarmsync.py(snmp 容器)
#        → snmptrap UDP → 網管端 snmptrapd(WSL udp:16162)
#
# 前置: VM 在跑;snmp 容器 Up 且 alarmsync RUNNING;WSL snmptrapd 監聽 16162
# 用法: ./scripts/snmp_verify.sh
# 可調: VM_SSH / TRAP_PORT(本地接收端口;829-831 與 <1024 有 WSL 限制,故 16162)
#
# 流程: 預檢 → 接收端就緒 → 重置旋鈕 → pre-clear → raise → clear → trap 摘要
# 注意: 結束時告警清回靜默;trap 摘要取自 snmptrapd 日誌尾部

# ── 外部依賴啟動命令(接收端不在時,人工執行;本腳本只檢查不代啟)──────────
# 1) 建 trapd 配置(僅首次):
#      printf 'authCommunity log public\n' > /tmp/opencode/snmptrapd.conf
#      # 用最小 community「public」授權接收;log = 收到的 trap 一律記進日誌
#
# 2) 起 snmptrapd(網管側接收端,常駐):
#      snmptrapd -Lf /tmp/opencode/snmptrapd.log -c /tmp/opencode/snmptrapd.conf udp:16162 &
#      # 用 16162 端口監聽 UDP(SNMP trap;<1024 屬保留段故避開)
#      # -Lf 把收到的每一條 trap 追加記錄到 snmptrapd.log
#      # (snmptrapd 自管檔案,重啟不截斷歷史——別用 > 重定向,那會清空舊證據)
#
# 3) 即時盯日誌(演示可選):
#      tail -f /tmp/opencode/snmptrapd.log
#      # trap 一到就在終端跳出來,適合給主管看實時效果

set -u -o pipefail

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -p 2222 admin@localhost"}
TRAP_PORT=${TRAP_PORT:-16162}
TRAP_LOG=/tmp/opencode/snmptrapd.log
SIM_JSON=/usr/include/vslib/otai_sim_data/otai_linecard_notification_sim.json

log() { echo "[snmpverify] $*"; }
die() { echo "[snmpverify][ERROR] $*" >&2; exit 1; }
vm() { $VM_SSH "$@" 2>/dev/null; }

# ── 0) 預檢 ──────────────────────────────────────────────
vm true || die "VM 不可達"
vm "sudo docker ps --format '{{.Names}} {{.Status}}' | grep -q '^snmp Up'" || die "snmp 容器沒在跑"
vm "sudo docker exec snmp supervisorctl status alarmsync" | grep -q RUNNING || die "容器內 alarmsync 非 RUNNING"
if ss -ulnp 2>/dev/null | grep -q ":${TRAP_PORT} "; then
    log "接收端就緒 ✓(udp:${TRAP_PORT})"
    echo "[snmpverify] 鏈路命令速覽:"
    echo "   snmptrapd -Lf /tmp/opencode/snmptrapd.log -c /tmp/opencode/snmptrapd.conf udp:16162"
    echo "   # 16162 端口監聽 UDP trap;收到即追加 snmptrapd.log(-Lf 追加模式,重啟不截斷歷史)"
else
    echo "[snmpverify] 接收端未就緒——啟動命令(WSL 執行):" >&2
    echo "  1) printf 'authCommunity log public\n' > /tmp/opencode/snmptrapd.conf" >&2
    echo "     # 僅首次:最小 community「public」授權;log = 收到的 trap 一律記日誌" >&2
    echo "  2) snmptrapd -Lf /tmp/opencode/snmptrapd.log -c /tmp/opencode/snmptrapd.conf udp:16162 &" >&2
    echo "     # 用 16162 端口監聽 UDP(SNMP trap);收到的 trap 追加記錄到 snmptrapd.log" >&2
    echo "     # (-Lf 由 snmptrapd 自管檔案,重啟不截斷歷史;別用 > 重定向,會清空舊證據)" >&2
    echo "  3) tail -f /tmp/opencode/snmptrapd.log" >&2
    echo "     # 可選:實時盯,trap 一到就跳出來" >&2
    die "執行上方命令後重跑本腳本"
fi
vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1" | grep -q . || die "線卡物件不存在(記帳空):先跑 scripts/seed_linecard.sh"
vm "redis-cli CONFIG GET notify-keyspace-events" | tail -1 | grep -qE "K|A" || die "redis keyspace 事件未開"
log "預檢通過(snmp 容器 + alarmsync RUNNING + 接收端 udp:${TRAP_PORT})"

# ── 1) 接收端日誌不打標記(雙寫者會互相覆蓋;摘要改按注入 text 過濾)──

# ── 2) 重置注入旋鈕到靜默 ────────────────────────────────
OP=$(vm "sudo docker exec syncd cat ${SIM_JSON}" \
        | grep -o '"linecard_oper_status"[^,}]*' | grep -o '[0-9-]\+$')
OP=${OP:-0}
vm "echo '{\"linecard_oper_status\": ${OP}}' | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
sleep 6
log "步驟 0:歸零基線——線卡在位、無告警(oper_status=${OP})"

# ── 3) 注入序列:pre-clear → raise → clear ───────────────
inject() { # $1=alarm_status $2=alarm_type $3=severity $4=text
    vm "echo '{\"linecard_oper_status\": ${OP}, \"alarm_status\": $1, \"alarm_type\": $2, \"alarm_severity\": $3, \"alarm_text\": \"$4\"}' \
        | sudo docker exec -i syncd sh -c 'cat > ${SIM_JSON}'"
}

inject 1 22 5 "verify pre-clear"; sleep 8; log "步驟 1:清場——發一筆『解除告警』(上一輪若有殘留,這一步會把它清掉)"
inject 0 22 5 "verify snmp raise $(date +%H:%M:%S)"; sleep 8; log "步驟 2:舉牌——發一筆新告警(LOW_TEMPERATURE_ALARM),網管應收到『告警發生』的 trap"
inject 1 22 5 "verify snmp clear"; sleep 14; log "步驟 3:摘牌——解除這筆告警,網管應收到『告警解除』的 trap"

# ── 4) trap 摘要(只取標記之後)──────────────────────────
echo
echo "════ 網管端收到的 trap(snmptrapd 日誌,本輪注入)════"
python3 - "$TRAP_LOG" <<'PYEOF'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
starts = [i for i, l in enumerate(lines) if re.match(r"\d{4}-\d{2}-\d{2} .*\[UDP:", l)]
blocks = [lines[s:e] for s, e in zip(starts, starts[1:] + [len(lines)])]
otai = [b for b in blocks if "99999.1.1.0.1" in "\n".join(b)]
# 本腳本每輪固定注入 raise+clear;取最後兩個(舊輪與其他來源的 trap 不混入)
ANN = [
    ("1.2.1.1.3.0",     "發送端 SNMP 起點至今的運行時長(標準必帶欄位)"),
    ("1.6.3.1.1.4.1.0", "trap 的身份證:OTAI-ALARM-MIB 告警通知(99999=佔位企業號)"),
    ("99999.1.7",       "告警狀態:1=RAISED 發生 / 2=CLEARED 解除"),
    ("99999.1.2",       "資源名(空=VID2NAME 未登記,已知退化)"),
    ("99999.1.3",       "告警類型名"),
    ("99999.1.4",       "嚴重度代碼(3=WARNING)"),
    ("99999.1.5",       "告警文字=注入的唯一標記(發生/解除配對用)"),
    ("99999.1.6",       "time-created:告警誕生的納秒時間戳(syncd 打的)"),
]
for i, b in enumerate(otai[-2:], 1):
    joined = "\n".join(b)
    def vb(oid):
        m = re.search(r"99999\.1\.%s = \w+: ?\"?([^\"\t\n]*)" % oid, joined)
        return (m.group(1) if m else "?").strip()
    state = {"1": "RAISED", "2": "CLEARED"}.get(vb("7"), vb("7"))
    print(f"  {i}. [{state}] {vb('2')}#{vb('3')}  severity={vb('4')}  text={vb('5')!r}")
    print("── 原始 trap 逐行註解 ──")
    for l in b:
        if "[UDP:" in l:
            print(f"    {l.strip()}")
            print("    // trap 到達時間 + 傳輸路徑(發送端臨時端口 → 接收端 16162)")
            continue
        for part in [pp.strip() for pp in l.split("\t") if pp.strip()]:
            print(f"    {part}")
            for key, note in ANN:
                if key in part:
                    print(f"    // {note}")
                    break
    print()
if not otai:
    print("  (未收到 OTAI trap!)")
PYEOF

# ── 5) DB 終態 ──────────────────────────────────────────
echo
echo "════ VM 側 DB 終態(應無 LOW_TEMPERATURE_ALARM = 已清回靜默)════"
vm "redis-cli -n 6 --scan --pattern 'CURALARM*'" | sed 's/^/    /'
echo
log "完成(trap 原始日誌:$TRAP_LOG)"
