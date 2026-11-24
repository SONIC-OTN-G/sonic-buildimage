#!/bin/bash
# 宿主驅動 ② sonic-cli 線驗證(klish → RESTCONF → translib → CONFIG_DB)
#   演示對象用 ACL:klish 可從零創建(ip access-list 直接 PATCH openconfig-acl),
#   不依賴平台端口/任何夾具(otn-kvm platform.json 無 Ethernet,故不能用 interface)
# 前置(由 otn-dev-tom 修復後的鏡像提供,本腳本不做任何補丁):
#   - 容器內 six/natsort(Dockerfile.j2 已修)
#   - rest_server client_auth 默認 none(rest-server.sh 已修)
# 用法: ./verify-sonic-cli-line.sh [端口]   默認 2223;VM 密碼走 VM_PASS(默認 YourPaSsWoRd)
set -u

PORT="${1:-2223}"
VM_PASS="${VM_PASS:-YourPaSsWoRd}"
SSH="sshpass -p $VM_PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 -p $PORT admin@localhost"

command -v sshpass >/dev/null || { echo "缺 sshpass"; exit 1; }
$SSH 'echo OK' >/dev/null 2>&1 || { echo "FAIL: VM 不可達"; exit 1; }

echo "════ ② sonic-cli 線(ACL 讀寫) ════"

PAYLOAD_FILE=$(mktemp)
cat > "$PAYLOAD_FILE" <<'PAYLOAD'
set +e
# 等 mgmt-framework 容器就緒(systemd Restart=always 自癒;只等,不注入配置)
for i in $(seq 1 30); do
  ST=$(docker inspect mgmt-framework --format '{{.State.Running}}' 2>/dev/null)
  [ "$ST" = "true" ] && break
  sudo systemctl start mgmt-framework >/dev/null 2>&1
  sleep 10
done
if [ "$ST" != "true" ]; then
  echo "FAIL: mgmt-framework 容器未就緒"
  exit 0
fi

# 冪等:刪舊 ACL(不存在也不報錯)
printf "configure terminal\nno ip access-list DEMO1\nend\nexit\n" \
  | script -qec "sonic-cli" /dev/null >/dev/null 2>&1

# 寫:klish 從零創建 ACL 表 + 規則 → PATCH openconfig-acl → CONFIG_DB
printf "configure terminal\nip access-list DEMO1\nseq 10 permit tcp any any\nend\nexit\n" \
  | script -qec "sonic-cli" /dev/null >/dev/null 2>&1

V=$(redis-cli -n 4 hget 'ACL_TABLE|DEMO1_ACL_IPV4' type)
[ "$V" = "L3" ] && echo "PASS: 寫 ip access-list DEMO1 → ACL_TABLE|DEMO1_ACL_IPV4" \
              || echo "FAIL: 寫 ACL 表 (實際[$V])"

V=$(redis-cli -n 4 hget 'ACL_RULE|DEMO1_ACL_IPV4|RULE_10' PACKET_ACTION)
[ "$V" = "FORWARD" ] && echo "PASS: 寫 seq 10 permit tcp any any → RULE_10(PACKET_ACTION=FORWARD)" \
                    || echo "FAIL: 寫 ACL 規則 (實際[$V])"

# 讀:show ip access-lists 渲染表格
OUT=$(printf "show ip access-lists\nexit\n" | script -qec "sonic-cli" /dev/null 2>&1)
echo "$OUT" | grep -q "DEMO1" && echo "PASS: 讀 show ip access-lists 含 DEMO1" \
                            || echo "FAIL: 讀 show ip access-lists (未見 DEMO1)"
echo "$OUT" | grep -q "permit tcp" && echo "PASS: 讀 規則內容(permit tcp 0.0.0.0/0 0.0.0.0/0)" \
                                  || echo "FAIL: 讀 規則內容"
PAYLOAD
$SSH 'cat > /tmp/verify-sonic-cli-line.sh' < "$PAYLOAD_FILE"
OUT=$($SSH 'bash /tmp/verify-sonic-cli-line.sh')
rm -f "$PAYLOAD_FILE"
echo "$OUT"

PASS_N=$(echo "$OUT" | grep -c '^PASS:'); FAIL_N=$(echo "$OUT" | grep -c '^FAIL:')
echo "════ 匯總:PASS=$PASS_N FAIL=$FAIL_N ════"
exit "$FAIL_N"
