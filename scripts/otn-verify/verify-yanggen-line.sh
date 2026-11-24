#!/bin/bash
# 宿主驅動 ① sonic-yanggen 命令線驗證
#   驗證開機時 sonic-yanggen/sonic-cli-gen 生成的命令(config otn-oa)
#   能寫/改/讀 CONFIG_DB(sonic-utilities click CLI 路徑)
# 依賴: sonic-yanggen.service 已在開機時生成插件(本腳本只等產物就緒,不做任何補丁)
# 用法: ./verify-yanggen-line.sh [端口]    默認 2223;VM 密碼走 VM_PASS(默認 YourPaSsWoRd)
set -u

PORT="${1:-2223}"
VM_PASS="${VM_PASS:-YourPaSsWoRd}"
SSH="sshpass -p $VM_PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 -p $PORT admin@localhost"

command -v sshpass >/dev/null || { echo "缺 sshpass"; exit 1; }
$SSH 'echo OK' >/dev/null 2>&1 || { echo "FAIL: VM 不可達"; exit 1; }

echo "════ ① sonic-yanggen 命令線 ════"

PAYLOAD_FILE=$(mktemp)
cat > "$PAYLOAD_FILE" <<'PAYLOAD'
set +e
# 等 yanggen 產物(開機服務生成;依賴就緒,非補丁)
PLUGIN=/usr/local/lib/python3.13/dist-packages/config/plugins/auto/sonic-optical-amplifier_yang.py
for i in $(seq 1 60); do [ -f "$PLUGIN" ] && break; sleep 5; done
if [ ! -f "$PLUGIN" ]; then
  echo "FAIL: yanggen 插件未生成(sonic-yanggen.service 未完成?)"
  exit 0
fi

# 冪等:刪舊建新
sudo config otn-oa delete AMP1 >/dev/null 2>&1

sudo config otn-oa add AMP1 --type EDFA --target-gain 20 >/dev/null 2>&1
V=$(redis-cli -n 4 hget 'OTN_OA|AMP1' type)
[ "$V" = "EDFA" ] && echo "PASS: 寫 config otn-oa add → OTN_OA|AMP1 type=EDFA" \
                  || echo "FAIL: 寫 config otn-oa add (實際[$V])"

sudo config otn-oa update AMP1 --target-gain 25 >/dev/null 2>&1
V=$(redis-cli -n 4 hget 'OTN_OA|AMP1' target-gain)
[ "$V" = "25" ] && echo "PASS: 改 config otn-oa update → target-gain=25" \
                || echo "FAIL: 改 config otn-oa update (實際[$V])"

OUT=$(show otn-oa 2>&1)
echo "$OUT" | grep -q AMP1 && echo "PASS: 讀 show otn-oa 含 AMP1" \
                           || echo "FAIL: 讀 show otn-oa"
PAYLOAD
$SSH 'cat > /tmp/verify-yanggen-line.sh' < "$PAYLOAD_FILE"
OUT=$($SSH 'bash /tmp/verify-yanggen-line.sh')
rm -f "$PAYLOAD_FILE"
echo "$OUT"

PASS_N=$(echo "$OUT" | grep -c '^PASS:'); FAIL_N=$(echo "$OUT" | grep -c '^FAIL:')
echo "════ 匯總:PASS=$PASS_N FAIL=$FAIL_N ════"
exit "$FAIL_N"
