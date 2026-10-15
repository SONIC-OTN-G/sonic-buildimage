# SONiC-OTN 线卡配置下发流程解析

本文以 `config slot 1 type P230C`(在槽位 1 配置一块 P230C 终端线卡)为例,追踪配置从 CLI 一路下发到模拟硬件层(OTAI vs 库)的完整链路,并在源码层面逐一说明每次函数调用与数据库事件。

槽位与命名空间映射关系:`asic_id = slot_id - 1`,即槽位 1 对应 `asic0` 网络命名空间,槽位 2 对应 `asic1`,依此类推。

## 1. 总体流程

```
config slot 1 type P230C (CLI)
    │
    ▼
sonic-cfggen --write-to-db  ──►  CONFIG_DB (asic0 命名空间)
    │                                   │ redis keyspace 通知
    ▼                                   ▼
linecardmgrd(otss0 容器内)          读取 CONFIG_DB,做电源状态检查
    │                                   │ 写 APP_LINECARD 表
    ▼                                   ▼
APPL_DB                             ──►  linecardorch(同容器)
    │                                   │ 状态机触发创建
    ▼                                   ▼
ASIC_DB(OTAI 对象序列化)           ──►  syncd-ot0 容器
    │
    ▼
OTAI vs 库(模拟线卡硬件)──► 状态/告警/性能数据写回 STATE_DB
```

## 2. 两种数据库通知机制

SONiC 的事件分发层(swss-common)针对不同数据库使用两种完全不同的通知机制,由 `Orch::addConsumer`(`src/sonic-otss/orchagent/orch.cpp:605-616`)在消费者创建时决定:

| 被消费的数据库 | 消费者类 | 通知方式 | 使用者 |
|---|---|---|---|
| CONFIG_DB / STATE_DB | `SubscriberStateTable` | **redis keyspace 通知** —— redis 服务器在键变更时自动推送消息 | linecardmgrd、configsyncd 等 cfgmgr 类守护进程 |
| APPL_DB / ASIC_DB | `ConsumerStateTable` | **'G' 门铃 + 工作队列** —— 生产者主动发布 | linecardorch、syncd 等 orchagent 类进程 |

两者的关键区别:

- **keyspace 通知**:生产者只需 HSET,不需要显式通知任何人。redis 服务端开启了 `notify-keyspace-events AKE`(`dockers/docker-database/Dockerfile.j2:38`),对每次写操作自动执行 `PUBLISH "__keyspace@<db>__<key>" "hset"`。消费者收到消息后用 HGETALL **重新读回整行数据**。
- **'G' 门铃**:生产者把数据写入哈希表的同时,在**同一个 Lua 脚本**里向队列表 LPUSH 一条工作项,并 PUBLISH 一条 'G' 消息。消费者收到 'G' 后从队列 pop 工作项,数据与事件一起送达,无需回读。

## 3. 阶段 0:linecardmgrd 启动与订阅

发生在 otss 容器启动时(supervisord 拉起 `linecardmgrd`):

```
linecardmgrd main()                                        [src/sonic-otss/cfgmgr/linecardmgrd.cpp:50]
  → LineCardMgr linecardmgr(cfgDb, appDb, stateDb, {"LINECARD"})    [linecardmgrd.cpp:59]
    → Orch(cfgDb, tableNames) 基类构造                            [orch.cpp:26-28]
      → Orch::addConsumer(cfgDb, "LINECARD")                    [orch.cpp:605]
        → 数据库名为 CONFIG_DB
          → new SubscriberStateTable(cfgDb, "LINECARD")         [orch.cpp:609]
             │
             ├─ psubscribe "__keyspace@4__:LINECARD|*"   ← 开一条专用 redis 连接,
             │                                              订阅该表的变更事件
             │                                              [subscriberstatetable.cpp:20-24]
             │
             └─ m_table.getKeys() + get() 逐个读取             ← 启动补读(dump):守护进程
                                                           不在期间发生的写入,在此时
                                                           被读出并填入待处理队列 m_toSync
                                                           [subscriberstatetable.cpp:26-29]
```

**启动补读机制解决了"错过唤醒"的问题**:配置时序为——CLI 先停止 otss 服务,再用 sonic-cfggen 写库,最后重启服务(slot.py:295-301 停服务 → :303 写库 → :305 起服务)。写库发生的 keyspace 通知没有任何进程接收,但 linecardmgrd 启动时通过全表扫描把已有数据补进待处理队列。

主循环(`linecardmgrd.cpp:74-97`)随后进入阻塞:

```cpp
ret = s.select(&sel, SELECT_TIMEOUT);
if (ret == Select::TIMEOUT) {
    linecardmgr.doTask();      // 超时重试通路:重处理 m_toSync 中被保留的条目
    continue;
}
auto *c = (Executor *)sel;
c->execute();                  // 事件通路:处理新到达的通知
```

## 4. 阶段 A:CLI 写入(生产者只做 HSET,不做通知)

```
CLI: config slot 1 type P230C                                [src/sonic-utilities/otn/slot/slot.py card_type]
  → config_terminal_linecard(1, "P230C", board_mode)         [slot.py:337]
    → create_linecard_config_file(1, "P230C")                [slot.py:279]
        asic_id = slot_id - 1 = 0
        ① j2 渲染模板 → /etc/sonic/config_db0.json
           (模板来源:/usr/share/sonic/device/x86_64-ot_kvm_x86_64_4_asic-r0/
            linecards/P230C/<board_mode>/config_db.json.j2)
        ② sudo systemctl stop otss@0 / syncd-ot@0
        ③ run_command("sudo sonic-cfggen -j /etc/sonic/config_db0.json
                        -n asic0 --write-to-db")              [slot.py:303]
             │
             ▼
        sonic-cfggen main()(Python 进程)
          → args.write_to_db                                  [fsroot /usr/local/bin/sonic-cfggen:498]
          → ConfigDBPipeConnector(namespace="asic0")          [sonic-cfggen:503]
          → configdb.mod_config(data)                         [sonic-cfggen:506]
             │  C++ 实现:
             ▼
          ConfigDBPipeConnector_Native::mod_config            [src/sonic-swss-common/common/configdb.cpp:403]
            → 对每个 (table, key, fields):
                _mod_entry(pipe, "LINECARD", "LINECARD-1-1", {...})   [configdb.cpp:421 → :378]
                  → pipe.enqueue("HSET LINECARD|LINECARD-1-1 field value ...")  [configdb.cpp:390]
            → pipe.exec()   ← MULTI/EXEC 事务,所有 HSET 一次往返完成
                                 (这就是 "Pipe" 的含义:即使 JSON 里有几十张表,
                                   网络往返也只有一次)           [configdb.cpp:424]
        ④ sudo systemctl start otss@0 / syncd-ot@0
        ⑤ 模拟上电(ot-vs 平台专属):
           sonic-db-cli -n asic0 STATE_DB hset "LINECARD|LINECARD-1-1"
                        "power-admin-state" "POWER_ENABLED" ...  [slot.py:306-310]
           docker exec syncd-ot0 touch /tmp/linkup  ← 通知 vs 库模拟链路建立 [slot.py:314]
```

**生产者到此为止** —— 没有显式发布任何通知。redis 服务端因 keyspace 通知配置而自动广播变更:

```
redis 服务端(自动):PUBLISH "__keyspace@4__:LINECARD|LINECARD-1-1" "hset"
```

时序注意:第 ③ 步(CONFIG_DB 写入)与第 ⑤ 步(STATE_DB 上电)发生在服务重启之后,所以刚启动的 linecardmgrd 会先遇到"CONFIG_DB 有数据但电源未使能"的情况,这正是阶段 C 中重试逻辑所处理的场景。

## 5. 阶段 B:keyspace 通知唤醒 linecardmgrd

```
linecardmgrd 主循环阻塞于:
  s.select(&sel, SELECT_TIMEOUT)                              [linecardmgrd.cpp:79]
    ← psubscribe 连接收到 pmessage → select 返回 OBJECT
  c = (Executor *)sel                                         [linecardmgrd.cpp:95]
  c->execute() → Consumer::execute()                          [orch.cpp:199]
    → getConsumerTable()->pops(entries)
      → SubscriberStateTable::pops()                          [subscriberstatetable.cpp]
          ① 解析 pmessage:key="LINECARD|LINECARD-1-1", event="hset"
          ② op = (event=="del") ? DEL : SET                   [subscriberstatetable.cpp:146,156]
          ③ HGETALL "LINECARD|LINECARD-1-1"    ← 重新读回整行数据
          ④ 构造元组 (key, op, 全部字段-值对)
    → addToSync(entries) → m_toSync                           [orch.cpp:147]
    → drain():m_toSync 非空 → m_orch->doTask(*this)           [orch.cpp:211-214]
      → LineCardMgr::doTask(consumer)                         [linecardmgr.cpp:71]
```

## 6. 阶段 C:linecardmgrd 决策逻辑与 APPL_DB 写入

`LineCardMgr::doTask`(`src/sonic-otss/cfgmgr/linecardmgr.cpp:71-110`)遍历 `m_toSync` 中的条目:

```
LineCardMgr::doTask(consumer)
  → op == SET_COMMAND:
    │
    ├─ isLineCardStateOk("LINECARD-1-1")                      [linecardmgr.cpp:49]
    │    m_stateLineCardTable.hget("LINECARD-1-1", "power-admin-state")
    │    == "POWER_ENABLED" ?
    │
    ├─ 否 → it++; continue       ← 条目保留(不从 m_toSync 删除),等待 CLI 第 ⑤ 步
    │                                写入上电状态。主循环每次 SELECT_TIMEOUT 超时,
    │                                超时分支调用 linecardmgr.doTask() 重试
    │                                (linecardmgrd.cpp:88)
    │
    └─ 是 → 拷贝全部字段 → setLineCardAttr(alias, newfvs)     [linecardmgr.cpp:38]
               → m_appLineCardTable.set(alias, fvs)
                 m_appLineCardTable 是 APPL_DB 上的 ProducerStateTable
                   → ProducerStateTable::set → 单个 Lua 脚本:  [producerstatetable.cpp:66,75,108]
                       HSET   APP_LINECARD:LINECARD-1-1 linecard-type P230C ... (数据)
                       LPUSH  工作项到 APP 侧队列表
                       PUBLISH 'G' 到 APP_LINECARD_CHANNEL@0                   (门铃)
  → op == DEL_COMMAND:
       SWSS_LOG_NOTICE("Delete LineCard: ...")                (当前无动作)
```

另外,configsyncd(otss 容器内第三个进程)在启动时注入 `object-count` 字段,表示该线卡的 OTN 对象类型总数(configsyncd.cpp:135-153:先统计 CONFIG_DB 中非空的表类型数,再**循环等待 linecardmgrd 先写出 APP_LINECARD 键**,最后 HSET)。该字段不是配置数据,而是给 linecardorch 的就绪信号(见阶段 D)。

## 7. 阶段 D:'G' 门铃唤醒 linecardorch

```
linecardorch 的 ConsumerStateTable(针对 APP_LINECARD 表)
  (由 Orch::addConsumer 的 else 分支创建 [orch.cpp:613],
   订阅 APP_LINECARD_CHANNEL@0 [consumerstatetable.cpp:27])
  → select() 收到 'G' 唤醒 → execute() → pops() 从工作队列取数据
  → LinecardOrch::doTask(consumer)                             [linecardorch.cpp:275]
    → table == APP_LINECARD → doAppLinecardTableTask          [linecardorch.cpp:283]
       → 解析字段:object-count → initConfigTotalNum()         [linecardorch.cpp:209-212]
                    board-mode/必选字段 → create_attrs
                    其余可配字段 → set_attrs
       → OrchFSM 状态机门控:
          READY 状态 → createLinecard(key, create_attrs)      [linecardorch.cpp:226 → 343]
            → otai_linecard_api->create_linecard()            [linecardorch.cpp:345]
            → SWSS_LOG_NOTICE("Create a linecard, id:...")    [linecardorch.cpp:351]
            → otai_redis 将 OTAI 调用序列化进 ASIC_DB
          WORK 状态 → setOtaiObjectAttrs(key, set_attrs, op_id)(属性修改)
       → 此后 ASIC_DB 的工作项由 syncd-ot0 消费,
         对 vs 库执行,状态/告警写回 STATE_DB
```

状态机转移路径:`NOT_READY → READY`(由 STATE_DB 的 oper-status=ACTIVE 触发,linecardorch.cpp:264)→ `WORK`(createLinecard 完成后,linecardorch.cpp:377)。

## 8. 时序图

```
sonic-cfggen        redis(CONFIG_DB)         linecardmgrd            redis(APPL_DB)        linecardorch
     |                   |                        |                       |                     |
     |--HSET LINECARD|-->|                       |                       |                     |
     | (MULTI/EXEC)      |--keyspace 通知-------->|                       |                     |
     |                   |  "__keyspace@4__:..."  |                       |                     |
     |                   |                     select() 唤醒              |                     |
     |                   |<----HGETALL 整行-------|                       |                     |
     |                   |                        | 检查 STATE_DB 电源   |                     |
     |                   |                        | setLineCardAttr      |                     |
     |                   |                        |--HSET APP_LINECARD +-->|                     |
     |                   |                        |  LPUSH + PUBLISH 'G'  |----'G' 门铃------>|
     |                   |                        |                       |                  create_linecard()
```

## 9. 观察与调试

```bash
# CONFIG_DB 条目(CLI 写入后)
sudo sonic-db-cli -n asic0 CONFIG_DB HGETALL "LINECARD|LINECARD-1-1"

# APPL_DB 条目(linecardmgrd 写入后;注意 object-count 字段由 configsyncd 注入)
sudo sonic-db-cli -n asic0 APPL_DB KEYS "APP_LINECARD*"
sudo sonic-db-cli -n asic0 APPL_DB HGETALL "APP_LINECARD:LINECARD-1-1"

# 实时观察各跳(在执行配置命令之前开启)
sudo sonic-db-cli -n asic0 CONFIG_DB MONITOR
sudo sonic-db-cli -n asic0 APPL_DB  MONITOR
sudo sonic-db-cli -n asic0 ASIC_DB  MONITOR

# 应用日志(进 syslog;docker logs 只有 supervisord 进程事件)
grep -iE "linecard|orchagent" /var/log/syslog | tail -40
# 预期顺序:"power enabled" → "Set object-count..." → "Linecard is active"
#          → "Create a linecard, id:..."

# 最终状态
sudo sonic-db-cli -n asic0 STATE_DB HGETALL "LINECARD|LINECARD-1-1"   # oper-status=ACTIVE
show slot 1 info
```

## 10. 关键源码索引

| 文件 | 关键内容 |
|---|---|
| `src/sonic-utilities/otn/slot/slot.py:279-315` | CLI 配置入口:渲染配置、写库、重启服务、模拟上电 |
| `src/sonic-swss-common/common/configdb.cpp:403-425` | ConfigDBPipeConnector::mod_config(MULTI/EXEC 批量写入) |
| `src/sonic-otss/orchagent/orch.cpp:605-616` | 消费者路由选择:CONFIG/STATE→SubscriberStateTable,其余→ConsumerStateTable |
| `src/sonic-swss-common/common/subscriberstatetable.cpp:20-29` | keyspace psubscribe 与启动补读 |
| `src/sonic-otss/cfgmgr/linecardmgr.cpp:49-64,71-110` | 电源检查、保留重试、1:1 拷贝到 APPL_DB |
| `src/sonic-otss/configsyncd/configsyncd.cpp:118-153` | object-count 注入 |
| `src/sonic-swss-common/common/producerstatetable.cpp:66-108` | APPL_DB 三合一动作(HSET+LPUSH+PUBLISH 'G') |
| `src/sonic-otss/orchagent/linecardorch.cpp:195-235,343-353` | FSM 门控、OTAI create_linecard |
| `dockers/docker-database/Dockerfile.j2:38` | redis notify-keyspace-events AKE(keyspace 通知总开关) |
