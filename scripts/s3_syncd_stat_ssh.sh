#!/usr/bin/env bash
# =============================================================
# 場景三(方案一版):syncd C++ 採集全流程——WSL 端編排,全操作 ssh 封裝
#
# 與 VM 內版 s3_syncd_stat.sh 等價(同斷言、同順序):正本在 WSL 倉庫,
# VM 無需預裝任何腳本——每個 VM 操作都以 ssh 下發(方案一:測試端腳本)。
# VM 內版 = 方案二候補(設備常駐工具);兩版斷言一致,結果應相同。
#
# 用法   :bash scripts/s3_syncd_stat_ssh.sh [--keep] [--wait-history]
#   --keep          結束時不關採集(線程保留跑著,便於人工看 db10)
#   --wait-history  真等到 5 分窗跨界,驗證 db10 歸檔(線卡 GAUGE + 網卡 COUNTER 兩源)
# 前提   :VM(otn-kvm)在跑且 ssh 可達;syncd 容器 RUNNING;三件套已套;線卡記帳在(冪等)
# 性質   :冪等可重跑——線卡/網卡記帳在即跳過 create;組行/counter 行可重推;默認結束三組 disable
# 對象   :線卡(vid=oid:0x1000000000000,GAUGE)
#          + ETHERNET 網卡(vid=oid:0x6000000000000,STATUS+COUNTER)
# 警告   :線卡重複 create 必死(SIGABRT);網卡重複 create 會重綁 RID——冪等護欄讓兩者都到不了
# 時長   :預設約 3 分鐘(含 ssh 往返開銷);--wait-history 最長約 7 分
# 對應文檔:45-syncd数据采集 §3(目標表 G1-G9);54 §5(方案一執行模型)
# =============================================================
set -u -o pipefail

if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: 请用 bash 运行本脚本:./scripts/<脚本名> 或 bash scripts/<脚本名>(sh/dash 不支持 flock fd 与 pipefail)" >&2
    exit 1
fi

VM_SSH=${VM_SSH:-"sshpass -p YourPaSsWoRd ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 admin@localhost"}
PASS=0; FAIL=0
G='\033[32m'; R='\033[31m'; B='\033[1m'; N='\033[0m'

# 互斥鎖(與 D1/D2/seed 同鎖):旋鈕/賬本/採集組配置都是共享狀態,併發互踩
LOCK=/tmp/opencode/.vm_test.lock
exec 9>"$LOCK"
flock -w 60 9 || { echo "ERROR: 另一個驗證腳本正在運行(排隊 60s 未取到鎖)" >&2; exit 1; }

# ssh 封裝:保留遠端 stdout/stderr,只濾掉 ssh 橫幅雜訊
vm() { $VM_SSH "$@" 2>&1 | grep -vE "Warning: Permanently|Debian GNU/Linux"; }

log() { echo "[s3ssh] $*"; }
die() { echo "[s3ssh][ERROR] $*" >&2; exit 1; }
ok()  { echo -e "  ${G}✓${N} $1"; PASS=$((PASS+1)); }
bad() { echo -e "  ${R}✗${N} $1 —— 預期: $2"; FAIL=$((FAIL+1)); }
expect() { # $1=標題 $2=遠端命令 $3=輸出指紋(遠端輸出會先展示再斷言)
  echo -e "${B}── $1${N}"
  echo "\$ vm: $2"
  out=$(vm "$2")
  echo "$out" | sed 's/^/    /' | head -8
  if echo "$out" | grep -qE "$3"; then ok "$1"; else bad "$1" "$3"; fi
  echo
}
note() { echo "# $1"; echo; }

KEEP=0; WAIT=0
for a in "$@"; do
  [ "$a" = "--keep" ] && KEEP=1
  [ "$a" = "--wait-history" ] && WAIT=1
done

# 0. 環境自檢
vm "sudo docker ps" >/dev/null || die "VM 不可達或 docker 不可用——本腳本經 ssh 操作 VM"

echo -e "${B}========== 場景三:syncd C++ 採集(STAT 三組:GAUGE / STATUS / COUNTER)==========${N}\n"
note "故事:需求輸入三組,一次全測——
      GAUGE  =線卡均值類(OtaiGaugeCollector:max/min/avg/instant,雙窗+歸檔)
      STATUS =網卡屬性類(OtaiAttrCollector:寫 STATE_DB)
      COUNTER=網卡計數類(OtaiStatCollector:增量累加,歸檔)
      無 orchagent,配置用 op 佇列手工種(原理見 43);先得有硬體物件——冪等 create。"

# ─────────────────────────────────────────────────────────────
# 第一部:線卡 GAUGE(均值類,場景三主體)
# ─────────────────────────────────────────────────────────────

# 1. 前檢:syncd 活著、依賴完整
expect "syncd 進程 RUNNING" \
  "sudo docker exec syncd supervisorctl status" \
  '^syncd +RUNNING'
expect "三件套依賴完整(ldd not found 歸零)" \
  "sudo docker exec syncd ldd /usr/lib/x86_64-linux-gnu/libswsscommon.so.0.0.0 | grep -c 'not found'" \
  '^0$'

# 2. 手工 create 線卡(冪等:記帳在即跳過;重複 create 必死,規則 5)
#    ASIC_DB(db1):記帳查詢 + op 佇列長度檢查
LEDGER=$(vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_LINECARD:*' | head -1")
QLEN=$(vm "redis-cli -n 1 --raw LLEN 'ASIC_STATE_KEY_VALUE_OP_QUEUE'")
if [ -n "$LEDGER" ]; then
  echo "  (✓ 線卡記帳已存在:${LEDGER}——跳過 create,只重推配置段)"
  echo "  (若 syncd 剛重啟,hardReinit 會自動重放記帳,無需任何操作)"; echo
else
  [ "$QLEN" = "0" ] || die "op 佇列非空(=${QLEN}),先 DEL ASIC_STATE_KEY_VALUE_OP_QUEUE 再跑(冷啟積壓的 notify 副本會害 syncd 崩)"
  # ASIC_DB(db1):LPUSH 線卡 create op——線卡記帳寫入由此起,syncd 消費後重建線卡物件
  vm "redis-cli -n 1 LPUSH 'ASIC_STATE_KEY_VALUE_OP_QUEUE' 'OTAI_OBJECT_TYPE_LINECARD:oid:0x1000000000000' '[\"OTAI_LINECARD_ATTR_LINECARD_STATE_CHANGE_NOTIFY\",\"0xdeadbeef\",\"OTAI_LINECARD_ATTR_LINECARD_ALARM_NOTIFY\",\"0xdeadbeef\",\"OTAI_LINECARD_ATTR_LINECARD_OTDR_RESULT_NOTIFY\",\"0xdeadbeef\",\"OTAI_LINECARD_ATTR_LINECARD_OCM_SPECTRUM_POWER_NOTIFY\",\"0xdeadbeef\"]' Screate" >/dev/null
  # ASIC_DB(db1):PUBLISH 敲鐘——喚醒 syncd 的 op 佇列消費者
  vm "redis-cli -n 1 PUBLISH 'ASIC_STATE_CHANNEL@1' G" >/dev/null
  echo "  (線卡 create 已下發;成功的硬指標是後面 db2 出 8 鍵,此處不設門檻)"; echo
fi

# 3. 給線卡起名(採集鍵名的來源)
# COUNTERS_DB(db2):HSET NAME_MAP——oid→linecard1,採集輸出鍵名由此而來
vm "redis-cli -n 2 HSET 'COUNTERS_LINECARD_NAME_MAP' 'oid:0x1000000000000' linecard1" >/dev/null
echo "  (NAME_MAP 已寫)"; echo

# 4. 開 GAUGE 採集組(組名含 1S_STAT_GAUGE 才會被採集器認;1 秒採樣)
# FLEX_COUNTER_DB(db5):LPUSH 組行 enable(POLL_INTERVAL=1000ms)——採集器訂閱這張組表,一行一條採集線程
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_GROUP_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_GAUGE' '[\"POLL_INTERVAL\",\"1000\",\"FLEX_COUNTER_STATUS\",\"enable\"]' SSET" >/dev/null
# FLEX_COUNTER_DB(db5):PUBLISH 敲鐘——喚醒採集器的組訂閱
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_GROUP_TABLE_CHANNEL@5' G" >/dev/null
echo "  (GAUGE 組已下發 enable)"; echo

# 5. 指定采什麼(線卡的 4 項 stat)
# FLEX_COUNTER_DB(db5):LPUSH counter 行——id_list 告訴採集線程採這 4 個 OTAI 統計 id
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_GAUGE:oid:0x1000000000000' '[\"id_list\",\"OTAI_LINECARD_STAT_TEMPERATURE,OTAI_LINECARD_STAT_CPU_UTILIZATION,OTAI_LINECARD_STAT_MEMORY_AVAILABLE,OTAI_LINECARD_STAT_MEMORY_UTILIZED\"]' SSET" >/dev/null
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_TABLE_CHANNEL@5' G" >/dev/null
echo "  (counter 行已下發;等採集器開工…)"; echo

sleep 8

# 6. 看當前窗:COUNTERS_DB(db2)浮現 8 鍵(4 stat × 15min/24h 雙窗)
#    8 鍵明細(採集線程憑空寫出的「滾動記分板」,每 stat × 每窗口類一鍵):
#      LINECARD:linecard1_CpuUtilization:15_pm_current   LINECARD:linecard1_CpuUtilization:24_pm_current
#      LINECARD:linecard1_MemoryAvailable:15_pm_current  LINECARD:linecard1_MemoryAvailable:24_pm_current
#      LINECARD:linecard1_MemoryUtilized:15_pm_current   LINECARD:linecard1_MemoryUtilized:24_pm_current
#      LINECARD:linecard1_Temperature:15_pm_current      LINECARD:linecard1_Temperature:24_pm_current
expect "當前窗 8 鍵浮現" \
  "redis-cli -n 2 --scan --pattern 'LINECARD:linecard1_*' | wc -l" \
  '^8$'
#    逐鍵存在斷言(COUNTERS_DB(db2):不只數量,8 個名字都要對):
for K in CpuUtilization MemoryAvailable MemoryUtilized Temperature; do
  for W in 15 24; do
    expect "當前窗鍵存在:LINECARD:linecard1_${K}:${W}_pm_current" \
      "redis-cli -n 2 EXISTS 'LINECARD:linecard1_${K}:${W}_pm_current'" \
      '^1$'
  done
done

# 7. 當刻值
# COUNTERS_DB(db2):HGET 溫度當前窗的 instant 欄——模擬器的即時讀數
expect "溫度 instant=35.0" \
  "redis-cli -n 2 HGET 'LINECARD:linecard1_Temperature:15_pm_current' instant" \
  '^35\.0$'

# 8. 看整個對象(--json;interval=5 分窗=補丁#5 生效)
# COUNTERS_DB(db2):--json 整窗欄位(interval/starttime/max/min/avg/validity)
expect "JSON 全欄(interval=5 分窗)" \
  "redis-cli --json -n 2 HGETALL 'LINECARD:linecard1_Temperature:15_pm_current'" \
  '"interval":"300000000000".*"current_validity":"complete"'

# ─────────────────────────────────────────────────────────────
# 第二部:網卡 STATUS + COUNTER(屬性類 + 計數類)
# ─────────────────────────────────────────────────────────────

# 9. create 網卡(冪等同款;vid=6<<48,ETHERNET=6)
# ASIC_DB(db1):記帳查詢 + op 佇列長度檢查
LEDGER=$(vm "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_ETHERNET:*' | head -1")
QLEN=$(vm "redis-cli -n 1 --raw LLEN 'ASIC_STATE_KEY_VALUE_OP_QUEUE'")
if [ -n "$LEDGER" ]; then
  echo "  (✓ 網卡記帳已存在:${LEDGER}——跳過 create)"; echo
else
  [ "$QLEN" = "0" ] || die "op 佇列非空(=${QLEN}),先 DEL ASIC_STATE_KEY_VALUE_OP_QUEUE 再跑"
  # ASIC_DB(db1):LPUSH 網卡 create op(冪等同款)
  vm "redis-cli -n 1 LPUSH 'ASIC_STATE_KEY_VALUE_OP_QUEUE' 'OTAI_OBJECT_TYPE_ETHERNET:oid:0x6000000000000' '[]' Screate" >/dev/null
  # ASIC_DB(db1):PUBLISH 敲鐘
  vm "redis-cli -n 1 PUBLISH 'ASIC_STATE_CHANNEL@1' G" >/dev/null
  echo "  (網卡 create 已下發;等 3 秒…)"; sleep 3; echo
fi
expect "網卡記帳落地" \
  "redis-cli -n 1 --raw --scan --pattern 'ASIC_STATE:OTAI_OBJECT_TYPE_ETHERNET:*' | head -1" \
  'OTAI_OBJECT_TYPE_ETHERNET'

# 10. 起名(狀態/計數鍵名的來源)
# COUNTERS_DB(db2):HSET ETHERNET NAME_MAP——oid→eth1
vm "redis-cli -n 2 HSET 'COUNTERS_ETHERNET_NAME_MAP' 'oid:0x6000000000000' eth1" >/dev/null
echo "  (NAME_MAP 已寫:eth1)"; echo

# 11. 開 STATUS 組(採屬性 → STATE_DB;STATE_DB 鍵用 | 分隔)
# FLEX_COUNTER_DB(db5):LPUSH 組行+counter 行 enable——1S_STAT_STATUS 採 3 個屬性 id
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_GROUP_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_STATUS' '[\"POLL_INTERVAL\",\"1000\",\"FLEX_COUNTER_STATUS\",\"enable\"]' SSET" >/dev/null
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_GROUP_TABLE_CHANNEL@5' G" >/dev/null
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_STATUS:oid:0x6000000000000' '[\"id_list\",\"OTAI_ETHERNET_ATTR_CLIENT_ALS,OTAI_ETHERNET_ATTR_ALS_DELAY,OTAI_ETHERNET_ATTR_MAINTENANCE\"]' SSET" >/dev/null
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_TABLE_CHANNEL@5' G" >/dev/null
echo "  (STATUS 組已下發:CLIENT_ALS / ALS_DELAY / MAINTENANCE)"; echo

# 12. 開 COUNTER 組(增量累加 → COUNTERS_DB;選 json 裡唯一非零值,指紋才看得見)
# FLEX_COUNTER_DB(db5):LPUSH 組行+counter 行 enable——1S_STAT_COUNTER 採 IN_PCS_UNAVAILABLE_SECONDS
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_GROUP_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_COUNTER' '[\"POLL_INTERVAL\",\"1000\",\"FLEX_COUNTER_STATUS\",\"enable\"]' SSET" >/dev/null
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_GROUP_TABLE_CHANNEL@5' G" >/dev/null
vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_TABLE_KEY_VALUE_OP_QUEUE' '1S_STAT_COUNTER:oid:0x6000000000000' '[\"id_list\",\"OTAI_ETHERNET_STAT_IN_PCS_UNAVAILABLE_SECONDS\"]' SSET" >/dev/null
vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_TABLE_CHANNEL@5' G" >/dev/null
echo "  (COUNTER 組已下發:IN_PCS_UNAVAILABLE_SECONDS(非零值,每拍 +10427))"; echo

sleep 8

# 13. STATUS 指紋:STATE_DB 浮現 eth1 屬性行(STATE_DB 鍵用 | 分隔)
# STATE_DB(db6):HGETALL ETHERNET|eth1——STATUS 採集線程寫入的屬性行(als-delay/client-als/maintenance)
expect "STATUS:STATE_DB 屬性行落地(ETHERNET|eth1)" \
  "redis-cli -n 6 HGETALL 'ETHERNET|eth1'" \
  'als-delay'

# 14. COUNTER 指紋:累加值線性增長(每拍 +10427;跨窗界重置會假跌,重試一輪)
# COUNTERS_DB(db2):連續兩拍 HGET 當前窗的 in-pcs-unavailable-seconds,驗增量累加
echo -e "${B}── COUNTER:累加線性增長(每拍 +10427)${N}"
GROW=0
for i in 1 2 3; do
  VA=$(vm "redis-cli -n 2 HGET 'ETHERNET:eth1:15_pm_current' in-pcs-unavailable-seconds")
  sleep 2
  VB=$(vm "redis-cli -n 2 HGET 'ETHERNET:eth1:15_pm_current' in-pcs-unavailable-seconds")
  echo "    第 $i 輪:${VA:-<空>} → ${VB:-<空>}"
  if [ -n "$VB" ] && [ -n "$VA" ] && [ "$VB" -gt "$VA" ] 2>/dev/null; then
    ok "累加值增長($VA → $VB)"; GROW=1; break
  fi
  sleep 1
done
[ "$GROW" = "1" ] || bad "累加值增長" "連續兩拍遞增(讀空/持平 = 機制沒跑,查 redis-cli -n 2 HGETALL 'ETHERNET:eth1:15_pm_current')"
echo

# ─────────────────────────────────────────────────────────────
# 第三部:歷史歸檔(--wait-history 才驗;線卡+網卡兩源)
# ─────────────────────────────────────────────────────────────

# 15. 等窗口跨界,看歷史歸檔
# HISTORY_DB(db10):窗口關閉後的歸檔鍵(線卡 GAUGE + 網卡 COUNTER 兩源)
if [ "$WAIT" = "1" ]; then
  # 以 VM 時鐘計算跨界點(5 分窗對齊 VM 牆鐘;VM 與 WSL 時鐘有偏移,不能用本地 date)
  VNOW=$(vm "date +%s" | tr -d '[:space:]')
  wait_s=$(( 300 - VNOW % 300 + 15 ))
  note "--wait-history:等 5 分窗跨界(VM 時鐘整點+15 秒),約 ${wait_s} 秒…(VM 時鐘會 NTP 跳躍,之後用輪詢兜底,實測踩過)"
  sleep "$wait_s"
  # 輪詢兜底:VM 時鐘 NTP 跳躍 + 各採集器歸檔時刻有抖動;以 db10 鍵為準
  # 注意 pattern:線卡鍵含 stat 名(linecard1_Temperature:15_pm_history_…),別寫成 linecard1:15_pm_history
  LC=""; ETH=""
  for i in $(seq 1 36); do
    [ -z "$LC" ] && LC=$(vm "redis-cli -n 10 --raw --scan --pattern '*linecard1*15_pm_history*' | head -1")
    [ -z "$ETH" ] && ETH=$(vm "redis-cli -n 10 --raw --scan --pattern '*eth1:15_pm_history_*' | head -1")
    [ -n "$LC" ] && [ -n "$ETH" ] && break
    sleep 5
  done
  echo -e "${B}── 歸檔指紋(輪詢後)${N}"
  echo "    線卡(GAUGE):${LC:-<未出現>}"
  echo "    網卡(COUNTER):${ETH:-<未出現>}"
  [ -n "$LC" ] && ok "GAUGE 歸檔:db10 落線卡歷史鍵" || bad "GAUGE 歸檔" "db10 有 *linecard1*15_pm_history* 鍵"
  [ -n "$ETH" ] && ok "COUNTER 歸檔:db10 落網卡歷史鍵" || bad "COUNTER 歸檔" "db10 有 *eth1:15_pm_history* 鍵"
  if [ -n "$ETH" ]; then
    expect "歸檔行 validity=complete" \
      "redis-cli -n 10 HGET '$ETH' validity" \
      'complete'
  fi
else
  note "歷史歸檔要等 5 分窗跨界;之後手動驗證(或重跑本腳本加 --wait-history,護欄會自動跳過 create):
        redis-cli -n 10 --scan --pattern '*linecard1*15_pm_history*'
        redis-cli -n 10 --scan --pattern '*eth1:15_pm_history*'
        ※ 注意查 db10(GB_COUNTERS_DB),不是 db15——42 §8.5"
fi

# 16. 收尾:關採集(配置驅動,即時生效;數據凍結留庫無妨,重開機即清)
if [ "$KEEP" = "1" ]; then
  note "--keep:三組保持 enable,線程繼續跑"
else
  # FLEX_COUNTER_DB(db5):三組逐一下發 disable(配置驅動,即時生效)
  for GRP in 1S_STAT_GAUGE 1S_STAT_STATUS 1S_STAT_COUNTER; do
    vm "redis-cli -n 5 LPUSH 'FLEX_COUNTER_GROUP_TABLE_KEY_VALUE_OP_QUEUE' '$GRP' '[\"POLL_INTERVAL\",\"1000\",\"FLEX_COUNTER_STATUS\",\"disable\"]' SSET" >/dev/null
    vm "redis-cli -n 5 PUBLISH 'FLEX_COUNTER_GROUP_TABLE_CHANNEL@5' G" >/dev/null
  done
  echo "  (三組已 disable 收尾;重跑本腳本即可再開)"; echo
fi

echo -e "${B}========== 結果:PASS=$PASS FAIL=$FAIL ==========${N}"
[ "$FAIL" -eq 0 ]
