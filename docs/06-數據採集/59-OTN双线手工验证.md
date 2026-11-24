# OTN 雙線手工驗證

## §1 sonic-yanggen 線(生成的命令寫讀 DB)

```bash
# 宿主 → 進 VM
sshpass -p 'YourPaSsWoRd' ssh -p 2223 admin@localhost

# sonic-cli-gen 現場生成(幂等;日誌應見 Auto-generation successful)
sudo systemctl restart sonic-yanggen
tail -n 6 /var/log/sonic-yanggen.log

# 重複演示先清(幂等,不存在也不報錯)
redis-cli -n 4 DEL 'OTN_OA|AMP1'

# 第 1 次查 DB:建之前,應無 AMP1
redis-cli -n 4 hgetall 'OTN_OA|AMP1'

# 第 1 次 show:應無 AMP1
show otn-oa

# 看生成出來的命令
sudo config otn-oa --help

# 寫:建對象(生成命令自己寫庫,無需先行造對象)
sudo config otn-oa add AMP1 --type EDFA --target-gain 20

# 第 2 次 show:應見 AMP1
show otn-oa

# 第 2 次查 DB:應有數據(證明真的寫入 CONFIG_DB)
redis-cli -n 4 hgetall 'OTN_OA|AMP1'
```

## §2 sonic-cli 線(ACL 寫讀 DB)

```bash
# 宿主 → 進 VM
sshpass -p 'YourPaSsWoRd' ssh -p 2223 admin@localhost

# 重複演示先清(幂等,不存在也不報錯)
redis-cli -n 4 DEL 'ACL_TABLE|DEMO1_ACL_IPV4' \
                  'ACL_RULE|DEMO1_ACL_IPV4|RULE_10' \
                  'ACL_RULE|DEMO1_ACL_IPV4|DEFAULT_RULE'

# 第 1 次查 DB:建之前,應無 DEMO1
redis-cli -n 4 hgetall 'ACL_TABLE|DEMO1_ACL_IPV4'

# VM 內 → 進 sonic-cli(需交互終端;root 不可用)
sonic-cli

# 第 1 次 show:應無 DEMO1
sonic# show ip access-lists

# 建 ACL 表 + 規則
sonic# configure terminal
sonic(config)# ip access-list DEMO1
sonic(config-ipv4-acl)# seq 10 permit tcp any any
sonic(config-ipv4-acl)# end

# 第 2 次 show:應見 DEMO1 與 10 permit tcp 0.0.0.0/0 0.0.0.0/0
sonic# show ip access-lists
sonic# exit

# 第 2 次查 DB:應有數據(證明真的寫入 CONFIG_DB)
redis-cli -n 4 hgetall 'ACL_TABLE|DEMO1_ACL_IPV4'
redis-cli -n 4 hgetall 'ACL_RULE|DEMO1_ACL_IPV4|RULE_10'
```
