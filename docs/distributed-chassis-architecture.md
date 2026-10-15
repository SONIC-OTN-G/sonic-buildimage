# SONiC-OTN 分布式线卡架构设计

> 状态:设计提案(基于 `otn_pre_202411` 分支代码分析)
> 前置阅读:`docs/linecard-config-flow.md`(单机配置下发链路解析)

## 1. 背景与动机

当前 SONiC-OTN 的 `ot-vs` 平台将整机模拟在**单个虚拟机**内:4 个槽位对应 4 个 ASIC 网络命名空间(asic0-3),所有 syncd-ot、otss、redis 实例跑在同一个 Linux 内核上,线卡硬件由 OTAI vs 库在用户态模拟。

真实 OTN 机架中,每块线卡拥有**独立 CPU**,运行自己的 SONiC 实例;主控板(_supervisor_)承担机架管理、状态汇聚与对外管理接口。本文设计如何把单机模型演进为"每板独立 CPU + 主控集中管理"的分布式架构。

**动机:**
- 资源隔离与可扩展性:单机内存/CPU 无法承载 13 槽位真实业务规模
- 可用性分区:单板 CPU 故障不影响整机
- 贴近生产形态:传统 OTN 设备(华为 OptiX、诺基亚 1830 等)均为每板独立 CPU 架构

## 2. 现状分析:单机模拟模型

### 2.1 数据流向

```
                    ┌────────── 单个 Linux 内核 ──────────────┐
                    │  CLI(机架感知)                         │
                    │      │ unix socket /var/run/redis{N}/   │
                    │      ▼                                  │
                    │  asic0..3 命名空间,各有独立的:           │
                    │    CONFIG_DB / APPL_DB / ASIC_DB /      │
                    │    STATE_DB / COUNTERS_DB               │
                    │      │ keyspace 通知(本地)              │
                    │      ▼                                  │
                    │  linecardmgrd → otss → syncd-ot →       │
                    │  OTAI vs 库(模拟线卡硬件)               │
                    └─────────────────────────────────────────┘
```

### 2.2 关键代码事实

| 事实 | 位置 | 含义 |
|---|---|---|
| CLI 通过**本地 unix socket** 连各槽位 redis | `src/sonic-utilities/otn/utils/db.py:6-12`(`connect_muti_db_common`) | 全部 48 处 CLI 调用假设所有槽位在本机 |
| OTN 编排器无任何机架感知 | `src/sonic-otss/orchagent/`(grep "chassis" 零命中) | OTN 应用层不读写 CHASSIS_APP_DB |
| 槽位硬件由 vs 库**单进程模拟** | OTAI vs 库 + `chassis_cli_capability.json`(13 槽位) | `config slot N` 触发的"插卡"是本地模拟 |
| 跨组件操作采用**意图数据**模式 | `src/sonic-utilities/otn/slot/slot_upgrade.py:25-29` | 命令=写意图字段到 DB,执行者=消费数据的本地守护进程 |
| 配置持久化按槽位独立 | `/etc/sonic/config_db{N}.json` | 天然支持每板一份 |
| swss-common `DBConnector` 支持 TCP | `src/sonic-swss-common/common/dbconnector.cpp:655` | 远程连库的底层能力已存在(CHASSIS_APP_DB 即用此路径) |
| 机架共享库机制完整存在 | `chassisdb.conf` → `database-chassis.service` → `redis_chassis.server:6380`(参考 Arista 7800) | 参考 `device/arista/x86_64-arista_7800_sup/chassisdb.conf` |

### 2.3 配置天然按槽位分片

对数据模型的审计结论:OTN 的全部业务表(LINECARD / PORT / TRANSCEIVER / AMPLIFIER / ATTENUATOR / APS / OSC / OCH / OTN / ETHERNET / LOGICAL_CHANNEL / ASSIGNMENT / INTERFACE / LLDP / APS_PORT / PHYSICAL_CHANNEL / OCM / OTDR)**均存放在各槽位自己的 CONFIG_DB**。全局 CONFIG_DB(槽位 0)只含机架级数据(DEVICE_METADATA、机架策略)。各槽位守护进程(linecardmgrd/otss/syncd-ot)只连本槽位命名空间的库,不消费全局配置。

> 这一事实是架构 A 成立的基础:**分布式化不产生"全局配置同步到单板"的需求**。

## 3. 三种架构方案对比

| | 架构 A:远程直连(对等模型) | 架构 B:管理面代理(gNMI 网关) | 架构 C:OTN 原生分布式 |
|---|---|---|---|
| **思路** | 主控 CLI 通过 TCP 直连各线卡 redis;线卡本地反应链不变 | 主控 gNMI 服务把整机级配置翻译成逐线卡 gNMI Set 下发 | 改造 OTAI/orchagent,槽位模拟与状态真正跨板分布 |
| **改造量** | 小(CLI 连接层 + 线卡 redis 开 TCP) | 大(mgmt-framework 无代理功能,需开发) | 很大(OTAI 接口、编排器、平台 API、CLI 全面开发) |
| **对现有代码的侵入** | 仅 `connect_muti_db_common` 一处模式 | 新增代理服务,线卡侧不变 | 全面重构 |
| **管理视图** | CLI 在主控,槽位感知(与现状一致) | SDN 控制器视整机为单一网元(生产 OTN 主流) | 同 B |
| **适用阶段** | **近期落地、概念验证** | 中期演进 | 长期目标 |

**推荐:先落架构 A**,验证分布式数据面与管理面分离;管理面按需演进到 B;C 作为远期方向(其 DB 传输层复用 A 的成果)。

## 4. 架构 A 详细设计

### 4.1 部署拓扑

```
                     中板网络 (Midplane, 10.0.10.0/24)
      ┌────────────────────────┼────────────────────────┐
      │                        │                        │
┌─────┴──────────┐    ┌───────┴────────┐    ┌──────────┴─────┐
│   主控 (SUP)    │    │   线卡 1 (LC1) │    │   线卡 N (LCn) │
│ 10.0.10.1      │    │ 10.0.10.2      │    │ 10.0.10.x     │
├────────────────┤    ├────────────────┤    ├────────────────┤
│ 机架 CLI        │    │ redis (TCP)     │    │ redis (TCP)    │
│ 全局 CONFIG_DB  │    │  CONFIG_DB      │    │  CONFIG_DB     │
│ redis_chassis   │    │  APPL_DB        │    │  APPL_DB       │
│  :6380 (共享)   │    │  ASIC_DB        │    │  ASIC_DB       │
│ database-chassis│    │  STATE_DB       │    │  STATE_DB      │
│ bgp / lldp      │    │  COUNTERS_DB    │    │  COUNTERS_DB   │
│ eventd / gnmi   │    │ linecardmgrd    │    │ linecardmgrd   │
│ mgmt-framework  │    │ otss / syncd-ot │    │ otss/syncd-ot  │
│ pmon(机架级)  │    │ (本地 CLI 可选,  │    │                │
│                │    │  仅维护通道)     │    │                │
└────────────────┘    └────────────────┘    └────────────────┘
```

### 4.2 数据分布模型

| 数据 | 实例位置 | 主控访问方式 | 线卡访问方式 |
|---|---|---|---|
| 各槽位 CONFIG_DB | **线卡本地** | TCP 远程写(CLI 下发配置) | 本地 unix socket(守护进程) |
| 各槽位 STATE_DB / COUNTERS_DB | **线卡本地** | TCP 远程读(`show slot N`) | 本地(守护进程写) |
| 各槽位 APPL_DB / ASIC_DB | **线卡本地** | 一般不访问 | 本地(otss ↔ syncd-ot 内部通道) |
| CHASSIS_APP_DB / CHASSIS_STATE_DB | **主控,全机架仅一份** | 本地 | TCP 6380 远程直连 |
| 全局 CONFIG_DB(机架级配置) | 主控 | 本地 | 不消费(见 4.7) |

### 4.3 容器部署模型

#### 4.3.1 现状(单机模型)部署粒度

权威依据:FEATURE 表生成模板(`files/build_templates/init_cfg.json.j2`,ot- 平台分支)、各容器 `manifest.json` 的 `asic-service` 声明、`files/build_templates/per_namespace/` 的 systemd 模板、`pmon_daemon_control.json`:

| 容器 | 当前部署粒度 | 依据 |
|---|---|---|
| database | 每槽位 1 个(database0-3)+ 全局 1 个 | `per_namespace/database.service.j2` |
| otss / syncd-ot | **每槽位 1 个** | `per_namespace/otss\|syncd-ot.service.j2`;FEATURE `has_per_asic_scope=True` |
| bgp | 全局 1 个 | ot- 平台特判:`has_global_scope=true, has_per_asic_scope=False`(init_cfg.json.j2:125) |
| lldp | 全局 1 个 | 同上特判(init_cfg.json.j2:117) |
| pmon | 全局 1 个,**且为空壳** | manifest `asic-service:false`;ot-vs 上 xcvrd / thermalctld / psud / ledd / pcied / syseepromd **全部跳过**(pmon_daemon_control.json) |
| eventd / gnmi / mgmt-framework | 全局 1 个 | manifest `asic-service:false`,无 @ 模板 |
| swss / syncd / snmp(标准版) | disabled | init_cfg.json.j2 ot- 分支:被 otss / syncd-ot **替代** |

#### 4.3.2 架构定位:整机一台交换机 vs 每线卡独立路由器

标准 SONiC VOQ 机架(Arista 7800 等)采用**每线卡独立路由器**模型:bgp / lldp / swss 均为每命名空间一份,每块线卡是独立 L3 节点,各自建立 BGP / LLDP 邻接。

OTN fork 刻意反其道而行 —— FEATURE 表把 bgp / lldp **强制为全局 scope**(标准多 ASIC 平台上它们是 per-asic)。这表明 OTN 的设计选择是**整机一台交换机**模型,证据链:

1. FEATURE 特判(init_cfg.json.j2 的 ot- 分支,显式覆盖标准行为);
2. pmon 在 ot- 平台是空壳 —— OTN 硬件监控(收发器、放大器、温度、告警)**不走标准 pmon 路径,而走每槽位 OTAI 链路**(syncd-ot → 状态写回 STATE_DB;`show slot N alarm/info` 即读此库);
3. otss / syncd-ot 的 manifest `after: []` 均为空,不依赖 eventd / pmon 等全局容器 —— 线卡自治最小集是**闭合**的。

该选择对分布式化是重大利好:全局服务天然归主控,线卡只需自治最小集,不存在"每线卡一份 BGP"的跨板路由协调问题。

#### 4.3.3 分布式部署映射

| 板 | 容器清单 |
|---|---|
| **主控 (SUP)** | database(全局)、**database-chassis**(共享库 TCP 6380)、bgp、lldp、eventd、gnmi、mgmt-framework、pmon(按需 —— 管机架级风扇/电源;若监控全走 OTAI 可精简) |
| **每线卡 (LC)** | **自治最小集:database(本地)+ otss@local + syncd-ot@local**,零外部容器依赖 |

> 备注:若真实线卡硬件带有本地传感器必须由 pmon 类守护进程轮询(而非 OTAI 可见),则该线卡需要本地 pmon —— 属架构 C 阶段的实板问题。另:单机模型曾观察到 `bgp3`(Created 态)为首次启动竞态残留物(线卡配置模板确认无 FEATURE/bgp 条目),与设计无关。

### 4.4 配置下发路径(写路径)

**核心澄清:CLI 只在主控运行;线卡上"接收网络数据"的就是 redis 本身 —— redis 天生是 TCP 服务器,无需新增任何接收程序。**

```
主控                                     线卡 N
─────                                    ──────
config slot 2 edfa ...
  │
  ▼
create_linecard_config_file / set_slot_synchronized_save
  (src/sonic-utilities/otn/slot/slot.py,不变)
  │
  ▼
get_config_db_by_slot(2)
  → connect_muti_db_common(2)          ★唯一改造点
  → 从 CHASSIS_TOPOLOGY 表查出槽位 2 的中板 IP
  → DBConnector(dbId, host="10.0.10.3", port=6379)   ← TCP 直连
  │                                        │
  └────────── HSET(经中板网络) ──────────►│ redis 处理 HSET
                                           │ 自动发 keyspace 通知
                                           ▼
                                        linecardmgrd 被唤醒
                                        (src/sonic-otss/cfgmgr/linecardmgrd.cpp,不变)
                                           ▼
                                        setLineCardAttr → APP_LINECARD
                                           ▼
                                        linecardorch → OTAI create/set
                                        (src/sonic-otss/orchagent/,不变)
                                           ▼
                                        syncd-ot → 硬件(或 vs 库)
```

线卡侧的整条反应链(keyspace 通知 → linecardmgrd → APP_LINECARD → linecardorch → syncd-ot → OTAI)与单机模型**逐字节相同** —— 唯一区别是触发 HSET 的连接来自网络而非本地 socket,而 keyspace 通知机制对写命令的来源无感知。

`sonic-cfggen -n asicN --write-to-db` 的等效改造:分布式后主控不再用命名空间参数,而是 `sonic-cfggen -j config_db2.json --remote <LC_IP> --write-to-db`(或等价的连接参数),落到线卡 CONFIG_DB 的机制不变。

### 4.5 共享数据库访问(CHASSIS_APP_DB)

**不是"同步",而是"共享":全机架物理上只有一份实例,跑在主控 TCP 6380,所有线卡远程直连读写。**

该机制在代码树中已完整存在(以 Arista 7800 为参考实现):

| 角色 | 配置 | 效果 |
|---|---|---|
| 主控 | `chassisdb.conf`: `start_chassis_db=1`, `chassis_db_address=<本板中板IP>` | `database-chassis.service` 拉起 redis_chassis 实例,监听 TCP 6380(门控:`/etc/sonic/chassisdb.conf` 存在) |
| 线卡 | `chassisdb.conf`: `chassis_db_address=<主控中板IP>` | `config-chassisdb` 服务在 `/etc/hosts` 写入 `redis_chassis.server=<主控IP>`;各容器获得 `--add-host=redis_chassis.server` |
| 解析 | `database_config.json` 中 CHASSIS_APP_DB/CHASSIS_STATE_DB 绑定到 `redis_chassis` 实例(hostname=redis_chassis.server, port=6380) | 任何进程访问 CHASSIS_APP_DB 时,swss-common 自动走 TCP 到主控 |

线卡 1 写入的键对线卡 2 和主控立即可见(同一个 redis 实例)。标准 SONiC 机架即此做法;OTN 侧未来若需要跨卡状态联动(如保护倒换协同、通道功率再平衡),其传输层直接复用此通道,只需定义 OTN 的表结构。

### 4.6 状态汇聚(读路径)

主控的 `show slot N` / `show chassis` 命令需要读取各线卡的 STATE_DB / COUNTERS_DB,两条路径按需选用:

1. **远程读(与写路径对称,零新增机制)**:`show slot 2 alarm` 时,主控 CLI 通过 TCP 直连线卡 2 的 STATE_DB。适用于命令式、低频的管理查询。
2. **主动发布到 CHASSIS_STATE_DB**:线卡把机架级可见的状态(告警汇总、在线/离线、槽位在场)写入共享库。适用于需要跨板联动或主控持续监控的状态。标准 SONiC 机架对邻居表等即用此方式。

建议:起步阶段全用路径 1(纯读,无额外开发);跨板联动需求出现时再引入路径 2。

### 4.7 全局配置为什么不需要同步到线卡

对数据模型的审计(见 2.3)表明:

1. **业务配置天然按槽位分片** —— 线卡守护进程只读自己槽位的 CONFIG_DB,不消费全局库;
2. 每卡需要的少量上下文**本地即可获得**:
   - 自己的 slot_id / 机架 ID:装机时本地置备(平台 API `get_my_slot()`,`docker_image_ctl.j2:185` 已在调用)
   - 主控地址(连 CHASSIS_APP_DB 用):本地文件 `chassisdb.conf`
   - 自己的 platform / DEVICE_METADATA:每卡本来就有独立一份
3. 若未来出现"全局配置线卡必须消费"的场景,按优先级:装机预置 → 远程读取 → 极少数真正跨卡数据升格进 CHASSIS_APP_DB(**注意**:该库定位是运行时状态,放配置会绕过 YANG 校验并丢失 redis 事务语义,应克制使用)。

### 4.8 配置持久化

- 每线卡保存自己的 `/etc/sonic/config_db.json`(去掉单机模型的槽位后缀 N)
- 主控保存全局 CONFIG_DB 与 `CHASSIS_TOPOLOGY` 表(槽位号 ↔ 中板 IP ↔ 在线状态)
- 线卡重启后:本地守护进程从本地 CONFIG_DB 冷启动加载,与单机行为一致;主控无需重推配置(除非线卡换卡/首次插入)

## 5. 实施改造清单(架构 A)

### 5.1 主控侧

| # | 改造点 | 位置 | 说明 |
|---|---|---|---|
| 1 | `connect_muti_db_common` 支持 TCP | `src/sonic-utilities/otn/utils/db.py:6-12` | 按 slot_id 查 `CHASSIS_TOPOLOGY` 表获取 IP;slot 0(全局)仍走本地 socket |
| 2 | 新增 `CHASSIS_TOPOLOGY` 表 | CONFIG_DB,全局 | 字段:`slot_id, midplane_ip, online`;YANG 模型 + CLI(`config chassis topology ...`) |
| 3 | 启用 chassis DB 服务 | `device/<sup-platform>/chassisdb.conf`:`start_chassis_db=1` | `database-chassis.service` 与 `config-chassisdb` 均已随镜像安装,配置即生效 |
| 4 | `set_slot_synchronized_save` 的应答通道跨板化 | `src/sonic-utilities/otn/utils/config_utils.py` | operation-id 应答当前走线卡本地 redis 的 pub/sub;跨板后改为:订阅线卡 TCP 连接上的频道,或降级为异步(写后轮询确认) |
| 5 | 线卡上下线检测 | CLI 层 | 连接失败/超时的处理与 `show chassis slots` 的在线状态展示 |

### 5.2 线卡侧

| # | 改造点 | 位置 | 说明 |
|---|---|---|---|
| 6 | redis 开启 TCP 监听 | `database_config.json` 的实例 hostname / supervisord 启动参数 | 绑定中板网卡 IP(非 0.0.0.0),仅暴露 CONFIG_DB / STATE_DB / COUNTERS_DB 所需实例 |
| 7 | redis 认证 | redis ACL(user/chassis_rw 限定命令集) | 防止中板内未授权访问 |
| 8 | 配置 `chassisdb.conf` | `/usr/share/sonic/device/<lc-platform>/chassisdb.conf`:`chassis_db_address=<主控IP>` | 复用现有 `config-chassisdb` 机制 |
| 9 | 本地槽位身份置备 | 装机/换卡流程 | slot_id、机架 ID 写入本地(供 `get_my_slot()` 等平台 API 返回) |
| 10 | (可选)禁用线卡本地机架 CLI | — | 线卡本地 CLI 保留 `show` 类只读命令作为维护通道;`config slot/chassis` 类仅主控可执行 |

**明确不改的部分**(架构 A 的核心价值):

- 线卡上 `linecardmgrd → otss → linecardorch → syncd-ot → OTAI` 整条反应链:**零改动**
- 线卡 redis 的库结构(库号、表名、分隔符):**零改动**
- `configsyncd` 的 object-count 注入、`Orch::addConsumer` 的消费者路由:**零改动**
- 意图数据模式(升级、`touch /tmp/linkup` 等模拟行为):分布式后语义不变

### 5.3 时序:一次分布式配置下发

```
运维            主控 CLI                线卡2 redis           线卡2 linecardmgrd
 │  config slot 2 │                        │                      │
 │  edfa 1 ...    │                        │                      │
 │───────────────►│                        │                      │
 │                │ 查 CHASSIS_TOPOLOGY     │                      │
 │                │ → 槽位2 = 10.0.10.3    │                      │
 │                │──TCP HSET AMPLIFIER───►│                      │
 │                │                        │──keyspace 通知──────►│
 │                │                        │                      │ 检查 STATE_DB 电源
 │                │                        │◄──HGETALL 整行──────│
 │                │                        │                      │ 写 APP_AMPLIFIER
 │                │                        │                      ▼
 │                │                        │                 (configsyncd → linecardorch
 │                │                        │                  → OTAI → 硬件,链路同前)
 │◄──── CLI 应答(成功/超时+回滚)──────────│                      │
```

## 6. 安全考虑

| 威胁 | 缓解 |
|---|---|
| 中板网络内未授权 redis 访问 | redis ACL(限定用户/命令/键前缀);redis 仅绑定中板网卡 IP |
| 配置/凭据明文中板传输 | 中板为机架内部专用网络(物理隔离或独立 VLAN);如需加固,启用 redis TLS(redis 6+ 原生支持) |
| 线卡仿冒 | chassis_rw 用户仅授予 HSET/HGETALL/DEL 等业务命令;装机时预共享凭据 |
| 拒绝服务(慢消费者) | CLI 连接超时 + `set_slot_synchronized_save` 的回滚机制已有(config_utils.py),跨板化时保留 |

## 7. 与架构 C(OTN 原生分布式)的关系

架构 A 分布式化的是**管理与配置面**(CLI、redis 访问路径),线卡内部的 OTN 栈仍将整机槽位视为本地模拟(vs 库模拟本板及整机)。架构 C 在此基础上继续演进:

- vs 库按板裁剪:每线卡只模拟**自己槽位**的线卡硬件
- ot-orchagent 增加机架感知:把本板 OTN 状态(收发器、放大器、OSC、PM)发布到 CHASSIS_APP_DB,主控/其他线卡可订阅
- `config slot N` 类意图由主控路由到目标线卡执行
- 平台 API(`get_my_slot` / `get_supervisor_slot`)在每板真实实现

A 阶段交付的 TCP 连接层、CHASSIS_TOPOLOGY 表、chassis DB 通道、跨板应答机制,全部被 C 复用。

## 8. 验证方案

阶段化验证(每阶段可独立验收):

1. **单机回归**:改造后的 CLI 在单机模式(slot 0 本地 socket)行为与现状完全一致 —— 现有 `config slot 1 type P230C` 全链路追踪(`docs/linecard-config-flow.md` 第 9 节命令集)作为回归用例
2. **两板最小拓扑**:主控 + 1 线卡(可先用两台 ot-vs 虚拟机 + QEMU socket 对连模拟中板,复用既有构建产物)
   - `config slot 1 type P230C` 在主控执行 → 线卡侧 syslog 出现完整链路日志("power enabled" → "Set object-count" → "Create a linecard")
   - 主控 `show slot 1 info` 读到线卡状态
   - 线卡侧 `sonic-db-cli CHASSIS_APP_DB PING` 打到主控共享库(验证 4.5)
3. **故障注入**:线卡下电/重启 → 主控 CLI 超时与回滚;线卡恢复 → 本地冷启动自愈,状态重新可读
4. **并发**:多槽位同时下发,验证无串扰(每槽位独立 TCP 连接与应答通道)

## 9. 术语表

| 术语 | 说明 |
|---|---|
| 主控 / SUP | 机架管理板,运行机架 CLI、全局 CONFIG_DB、redis_chassis 共享实例 |
| 线卡 / LC | 每槽位一块,独立 CPU,运行完整 SONiC-OTN 本地栈(含 redis、linecardmgrd、otss、syncd-ot) |
| 中板 / midplane | 机架内部板间网络,承载配置下发与共享库访问 |
| CHASSIS_APP_DB / CHASSIS_STATE_DB | 全机架唯一共享 redis 实例(主控 TCP 6380),线卡远程直连 |
| 意图数据模式 | 命令以数据字段形式写入 DB,由消费方守护进程执行的异步模式(本代码树的既有惯例) |
| keyspace 通知 | redis 服务端在键变更时自动发布的订阅消息(CONFIG_DB 事件分发的底层机制) |
