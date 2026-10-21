# SONiC-OTN × HI1231 移植 PoC 执行实录（阶段 0–4）

> 文档版本: v1.0（2026-10-21，阶段 4 完成 + 配置翻译真机验证通过时点）
> 性质: **过程实录**——做了什么、遇到什么、怎么解的。方案与计划见 `docs/rtos-poc-plan.md`（v1.2），需求清单见 `docs/rtos-poc-requirements.md`（v2.1），功能演示基准见 `README.otn.md`
> 代码基线: sonic-otn `otn_pre_202411` 分支（Debian Bullseye 用户态）
> 目标设备: HI1231，`arm64le-preempt-pro-llvm-5.10`（BiSheng Embedded，glibc 2.34，busybox init）
> 当前状态: 阶段 0–4 全绿，"万事俱备，只欠插卡"；阶段 5/6（CLI 远程化 + 全链路验收）待启动

---

## 0. 一句话结论

**核心命题已答**：SONiC-OTN 软件栈（bullseye 工具链构建的 arm64 二进制）可以在 HI1231 RTOS 上以裸进程形态直接运行。redis、syncd（vs 模拟）、linecardmgrd、orchagent 四进程真机常驻；configsyncd 的配置翻译与 ConfigDone 就绪屏障真机实证。从冒烟到配置翻译，每一环均为实测而非推论。

## 1. 时间线总览

| 时间 | 里程碑 | 产出 |
|---|---|---|
| 2026-10 上旬 | 阶段 0 冒烟（GO）+ 阶段 1 基线 | redis 6.0.16 + 17-deb 闭包真机运行；设备探测结论归档 |
| 2026-10-16 | 阶段 2 arm64 构建打通 | `a1ee5cc1f0`（9 文件）+ 子模块 `91cf68c`；PoC 文档入库 `ba05347ee4` |
| 2026-10-20 | 阶段 3 payload 组装收敛 | otn-poc 树（6 二进制 + 85 库 + 21 sim JSON + dump.rdb），GLIBC_CLEAN；`1adc83141e` |
| 2026-10-20/21 | 阶段 4 设备拉起 + 配置翻译验证 | 四进程常驻；三轮排障；TRANSCEIVER 翻译 + ConfigDone 实证 |
| 2026-10-21 | 写档 + 复现性缺口修复 | 本文档；start-all.sh v2（sonic-db 供给固化）+ conf/database_config.json + 重打包；payload 树迁入仓库根 |

## 2. 阶段 0：冒烟测试（GO）

命题、方法与结果详见 `rtos-poc-plan.md` §5.1。要点：

- 仅取 bullseye redis-server 6.0.16 + 17-deb 依赖闭包，组装自包含目录树，scp 上机以 `LD_LIBRARY_PATH=./lib` 裸进程运行
- 验证全过：ELF 加载、glibc 2.34 符号契约（最大 GLIBC_2.29）、unix socket + TCP 双监听、100 DB 布局、keyspace 通知 AKE、pubsub 不限流
- redis 版本决策：基准 VM 实际跑 bookworm redis 7.0.11，但 bookworm 包按 glibc 2.36 基线链接（设备 2.34 有符号风险）→ 取 bullseye 6.0.16（OTN 用到的 redis 特性 6.x 全有）
- **结论**："bullseye arm64 二进制可直接运行于设备 glibc 2.34"从假设变实测，Track A 地基解除风险

## 3. 阶段 1：环境基线

探测结论详表见 plan §2.1。要点：

- **可用**：glibc 2.34（≥2.31）、内核 5.10 SMP PREEMPT、sshd + iproute2（SSH 隧道成立）、约 5.7GB 内存、rw+exec 可写分区
- **缺失**（决定 Track A 形态）：无 systemd（busybox init）、无 python3、无容器运行时/cgroup/overlayfs（Track B 不可行）、无 ONIE（Track C 不可行）
- **设备侧核验补充（阶段 4 前完成）**：`/lib64/libresolv.so.2` 存在（kerberos 链最后一环闭合）；`KernelPageSize: 4kB`（无 64K 页疑虑，冒烟时代最后一个"待归档"项关闭）
- **部署约束**：/tmp 疑似 noexec（冒烟实测 Permission denied）→ payload 部署于 rw+exec 分区

## 4. 阶段 2：arm64 构建

### 4.1 目标与产出

构建系统 QEMU multiarch 模式（`sonic-slave-bullseye` 容器内），**锁定 bullseye slave**（OTN 容器栈全部基于 bullseye；BLDENV 由 slave 容器 lsb_release 决定，`slave.mk:30`）。

产出 **52 个 arm64 deb**（`target/debs/bullseye/*_arm64.deb`），其中 OTN 核心 18 个：otss、syncd-ot-vs、libswsscommon（含 python3 绑定）、libotairedis 全家（libotai / libotai-vs / libotairedis / libotaimetadata）等。

### 4.2 构建障碍编年（穿越约 10 层）

| # | 障碍 | 解法 |
|---|---|---|
| 1 | EOL 源：bullseye 部分上游源失效 | 迁移 mirror 源（sources.list.j2、build_mirror_config.sh） |
| 2 | apt 密钥死锁：keyring 过期 | debian-archive-keyring.gpg 种入 slave Dockerfile |
| 3 | CDN 假 404 | 启用 snapshot 快照机制 |
| 4 | nodesource 密钥轮换 | 替换 nodesource 源 |
| 5 | QEMU binfmt 异常 | 换血 tonistiigi/binfmt |
| 6 | 模拟态下测试挂死 | nocheck 豁免 |
| 7 | 并行度问题 | Makefile.work / slave.mk 管道修正 |
| … | 其余为同类衍生问题 | |

修复已提交 **`a1ee5cc1f0`**（Makefile.work、files/apt/sources.list.j2、rules/otss.mk、rules/swss-common.mk、scripts/build_mirror_config.sh、slave.mk、sonic-slave-bullseye/Dockerfile.j2、debian-archive-keyring.gpg、src/sonic-otairedis 子模块指针，共 9 文件）。

子模块补丁 **`91cf68c`**：vslib 模拟数据路径从编译期硬编码（`OtaiObjectSimulator.h:9`）改为 `OTAI_SIM_DATA_PATH` 环境变量——裸进程部署的关键前提。

### 4.3 双门检验（产物可信度）

| 门 | 判据 | 结果 |
|---|---|---|
| glibc 符号门 | 全部产物最大符号 ≤ 设备 2.34 | ✅ 实测最大 GLIBC_2.30（syncd） |
| 工具链门 | DW_AT_producer 为 bullseye 原生工具链 | ✅ GCC 10.2.1，无交叉污染 |

## 5. 阶段 3：payload 组装

### 5.1 依赖闭包收敛

以 6 个二进制的 NEEDED 递归收敛，三轮补口：

1. 初轮：OTN 三件套 + redis → C++ 栈（libstdc++6 / libgcc-s1，10.2.1-6 与构建链同源）+ swsscommon 依赖（libhiredis、libzmq、yaml-cpp、libnl-3 系）
2. libzmq → kerberos 链：libkrb5-3 → libgssapi-krb5-2 → libk5crypto3 → libcom-err2 → libkeyutils1（libresolv 由设备提供 ✅）
3. libmd0；libyang → libpcre3

最终闭包：**85 个库文件（~18MB）零缺口**，全部 GLIBC_CLEAN。

### 5.2 最终 payload 树（仓库根 `otn-poc/`，2026-10-21 自 `~/sonic-otn/` 迁入）

```
otn-poc/
├── bin/            6 个二进制：redis-server, redis-cli, syncd, orchagent, linecardmgrd, configsyncd
├── lib/            85 个捆绑库（LD_LIBRARY_PATH 隔离）
├── conf/           redis.conf（docker-database 8 条 sed 等效）
│                   database_config.json（swsscommon 默认路径供给，2026-10-21 补入）
├── otai_sim_data/  21 个 vs 模拟 JSON
├── swss/           7 个 swss Lua 脚本（阶段 4 补入）
├── dump.rdb        CONFIG_DB 预置（CONFIG_DB_INITIALIZED + DEVICE_METADATA）
├── scripts/        start-all.sh（v2：sonic-db 供给已固化）
├── debs/           16 个闭包用 bullseye arm64 deb（留存复现）
└── stage/          deb 解包全集（数据文件来源）
```

### 5.3 CONFIG_DB 预置（绕过 sonic-cfggen / python）

设备无 python3 → 无法跑 sonic-cfggen。方案：构建侧用 redis 生成最小种子 dump.rdb（CONFIG_DB_INITIALIZED=1 + DEVICE_METADATA 表），payload 携带，设备上 redis 以 `--dir rdb-work` 启动加载。

## 6. 阶段 4：设备拉起（三轮排障实录）

### 6.1 启动序（start-all.sh，supervisord 顺序还原）

```
redis-server（daemonize，--dir rdb-work 加载 dump.rdb 预置）
  └→ sleep 2 → syncd（-p /tmp/otai_test.profile：
        OTAI_LINECARD_LOCATION=1, OTAI_VS_LINECARD_TYPE=OTAI_VS_LINECARD_TYPE_OTN）
      └→ sleep 5 → orchagent（-b 8192）→ linecardmgrd → configsyncd
```

### 6.2 故障一：syncd 崩溃 "Sonic database config file"

**现象**：`terminate called after throwing std::runtime_error: what(): Sonic database config fil...`（设备 RSM 看门狗随之输出 SIG[6]/SIG[9] 跟踪日志，文本被截断交错；coredump limit=0 无 core 留存）。

**根因**：libswsscommon 的 `SonicDBConfig` 启动即加载 DB 配置，路径硬编码于 `src/sonic-swss-common/common/dbconnector.h:90`：

```cpp
static constexpr const char *DEFAULT_SONIC_DB_CONFIG_FILE = "/var/run/redis/sonic-db/database_config.json";
```

VM 里由 database 容器 init 生成该文件；设备上无人提供。

**修复**：`/var/run` 为 tmpfs 可写 → 启动脚本 `mkdir -p /var/run/redis/sonic-db` + 放置单实例 database_config.json。内容取 deb 自带的单实例标准配置（16 个 DB 映射 + chassis 条目——连接是惰性的，列出不连无害；**缺失 DB 名是硬错误**，宁全勿缺）；separator 为键格式规范来源（CONFIG_DB/STATE_DB 用 `|`，APPL/ASIC 用 `:`），勿动。修复已固化进 start-all.sh v2（见 §10.2）。

### 6.3 故障二：syncd 崩溃 "readTextFile: consumer_table_pops.lua"

**现象**：越过 DB 配置加载后死于 `/usr/share/swss/consumer_table_pops.lua: No such file or directory`。

**根因**：`src/sonic-swss-common/common/redisapi.h:66` 硬编码 `readTextFile("/usr/share/swss/" + path)`，无环境变量无配置项；且 `loadLuaScript` 在 ConsumerTable 等类的**构造函数**里执行——进程一启动、第一个表对象一构造就要读文件。我们的组装只收集了 deb 的 bin/lib，data 文件漏了。

**一次性排雷盘点**：stage 内 OTN 全家 deb 的非 bin/lib 文件，除 doc/copyright 外**仅 7 个 Lua 脚本**，本轮全部补入 payload `swss/`：

| 脚本 | 调用方 | 作用 |
|---|---|---|
| consumer_table_pops.lua | consumertable.cpp:22 | ASIC_DB 消费主路径：LRANGE+LTRIM+物化 `ASIC_STATE:类型:对象ID` hash，一次 EVAL 原子批量弹出 |
| consumer_state_table_pops.lua | consumerstatetable.cpp:18 | 状态表版（"G" 门铃+脏键集合的服务端半边） |
| producer_state_table_apply_view.lua | producerstatetable.cpp:45 | 整视图批量发布（apply view） |
| portcounter.lua | countertable.cpp:25 | 端口计数器批量读（COUNTERS_DB） |
| fdb_flush.lua / fdb_flush.v2.lua | FDB 路径 | MAC 表整表刷除 |
| table_dump.lua | 调试 | 整表导出 |

这些脚本经 EVALSHA 在 redis 6.0.16 服务端原子执行——**OTN 数据面每批消费的必经之路**（op 词汇表含本 fork 扩展的 `attribute_capability_query` / `stats_capability_query` 等 OTN 同步查询流）。

**修复**：设备实测 `/usr/share` 可写 → 启动脚本直接 `mkdir -p /usr/share/swss && cp $BASE/swss/*.lua /usr/share/swss`（预备的 bind-mount 兜底方案未启用）。

### 6.4 "故障"三（非故障）：configsyncd 立即退出

**现象**：`[3]+ Done ./bin/configsyncd`——干净退出（Done），非崩溃非被杀。

**定性（设计行为）**，源码三重证据：

```cpp
// configsyncd.cpp:51 — usage 文本自述
"this program will exit if configDB does not contain that info"

// configsyncd.cpp:127-130 — 退出路径
if (objects.size() == 0) { goto exit; }   // 17 个同步器全空 → return 0

// configsync.cpp:99 — 判空逻辑
if (keys.empty()) { return false; }       // CONFIG_DB 无该表 → 不注册
```

VM 流程中它从不空跑：`config slot N type X` 的顺序是**先停服务 → sonic-cfggen 写全量模板表 → 再启动**（slot.py:270-295）。我们的最小种子（CONFIG_DB_INITIALIZED + DEVICE_METADATA）没有 OTN 表 → 退出完全正确。

**当前态**：待命。配置一到位（插卡流程）重启即活。

### 6.5 隐藏里程碑：二进制兼容的真机实证

第一轮崩溃本身就证明：syncd 在真机上完成了动态链接（85 个捆绑库 + 设备 glibc 链全部解析成功）并跑到 C++ 异常抛出。"arm64 二进制在设备上兼容"从"理论 + 符号检查"升级为"真机运行实证"——阶段 2/3 的全部闭包工作在真机闭环。

### 6.6 配置翻译验证（选项 A 快速实验）

手工向 CONFIG_DB 种一条最小记录，验证 configsyncd 翻译路径：

```
设备： redis-cli -s /var/run/redis/redis.sock -n 4 \
          HSET "TRANSCEIVER|TEST-1-1" "index" "1" "enabled" "true"
       → (integer) 2                                # 写入成功（index + enabled 两字段）

重启 configsyncd 后 APPL_DB KEYS：
       1) "APP_TRANSCEIVER:TEST-1-1"                # 翻译本体：CONFIG_DB TRANSCEIVER → APPL_DB APP_TRANSCEIVER ✅
       2) "APP_TRANSCEIVER:ConfigDone"              # 表级就绪屏障信号（两级就绪屏障的第一级）✅
```

configsyncd 驻留不再退出。OTSS 核心机制（配置翻译 + 就绪屏障协议，CLI 同步确认所依赖）真机实证。

## 7. 排障方法论沉淀

1. **故障模式识别**："deb 的 data 文件 + 硬编码绝对路径 + 极简 rootfs"。对策：一次性盘点 stage 全集（本轮已清零——除 7 个 Lua 外无其他 data 文件），不逐个打地鼠。若后续再现 "file not found"，优先怀疑 `/etc/sonic/` 类路径。
2. **崩溃点前移 = 链路推进**：动态链接成功 → DB 配置加载成功 → Lua 加载成功 → 连上 redis——每轮崩溃都是里程碑。
3. **干净退出 ≠ 故障**：先查退出路径的设计意图（configsyncd 案例：usage 文本自己写明了退出条件）。
4. **设备看门狗噪声**：RSM SIG 跟踪日志会截断/交错异常文本，读 `what()` 主体需拼接耐心。
5. **设备写路径分级**（实测）：`/var/run` tmpfs 可写（socket + sonic-db 配置）、`/usr/share` 可写（Lua 直拷）、`/tmp` noexec（避用）、部署分区 rw+exec。

## 8. 验证矩阵（当前）

```
✅ 二进制兼容（85 库闭包 + 设备 glibc 2.34）          阶段 2/4 实证
✅ redis + dump.rdb 预置                             阶段 4 实证
✅ database_config + 7 Lua 供给                      阶段 4 实证（两轮修复）
✅ syncd(vs) 常驻                                     阶段 4 实证
✅ configsyncd 翻译 + ConfigDone 就绪屏障             阶段 4 实证（选项 A）
⏳ orchagent → ASIC_DB → syncd → vs 硬件模拟         等插卡数据
⏳ linecardmgrd 生命周期状态机（NOT_READY→READY→WORK）等插卡事件
⏳ CLI 全链路（config slot / show slot）              阶段 5/6
```

## 9. 工件清单

| 工件 | 位置 / 标识 |
|---|---|
| 构建修复提交 | `a1ee5cc1f0`（9 文件，2026-10-16） |
| PoC 文档入库 | `ba05347ee4`（rtos-poc-plan/requirements，2026-10-16） |
| arm64 产物 WIP | `1adc83141e`（2026-10-20）；冒烟期 `9dd7757eeb` |
| 子模块补丁 | src/sonic-otairedis `91cf68c`（vslib sim 数据路径 OTAI_SIM_DATA_PATH 环境变量化） |
| arm64 deb 产物 | `target/debs/bullseye/*_arm64.deb`（52 个） |
| payload 主树 | 仓库根 `otn-poc/`（bin/lib/conf/swss/otai_sim_data/dump.rdb/scripts/debs/stage，2026-10-21 迁入仓库） |
| 部署 tarball | 仓库根 `otn-poc.tar.gz`（历史副本：`~/sonic-otn/otn-poc.tar.gz`） |
| 设备部署 | HI1231 rw+exec 分区，`sh scripts/start-all.sh` 拉起 |

## 10. 未竟事项与下一步

### 10.1 阶段 5/6（下一步主线）

x86 CLI 隧道 + `config slot 1 type P230C` 全流程 → 8 项验收清单（plan §7）。两个待决策：

| 决策点 | 选项 | 建议 |
|---|---|---|
| CLI 宿主 | A. Fedora 原生（venv + 从 bookworm deb 解包 amd64 python3-swsscommon，glibc 2.36 ≤ Fedora 2.38+）；B. bookworm 容器 | A（零容器、路径最短） |
| `config slot` 本地编排 | a. slot.py fork（261-307 行 systemctl/docker exec → ssh 路由，保留 CLI 单入口）；b. x86 侧半手工脚本（j2 生成 → 隧道写库 → 设备重启进程） | a（验证"CLI 原样可用"命题） |

### 10.2 payload 复现性缺口（2026-10-21 写档时发现，同日已修复）

**发现**：x86 侧 `scripts/start-all.sh` 为中间版本——已有 Lua 供给段，但缺 database_config.json 放置段；该文件仅存于 `stage/var/run/redis/sonic-db/`，未进 `conf/`，设备侧当时靠手工放置。且 `/var/run` 为 tmpfs，设备重启后 sonic-db 配置丢失，syncd 将再次崩溃于启动线。

**修复（已落地）**：

1. `conf/database_config.json` ← stage 副本（deb 自带的单实例标准配置）
2. start-all.sh v2：redis 段前补 `mkdir -p /var/run/redis/sonic-db` + `cp`——每次拉起自动供给，冷启动可复现
3. 重新打包 `otn-poc.tar.gz`（含 conf 新文件与脚本 v2）

### 10.3 设备侧待归档

- `cat /proc/mounts` 输出（正式部署分区定名与容量，REQ-01 阶段 1 收尾）
- libresolv 存在、4KB 页已核验通过 → 待回写 plan §2.1 对应两行"待归档"

### 10.4 文档收尾

- `rtos-poc-plan.md` v1.3：阶段 2–4 执行记录（可直接引用本文）、设备核验数据、§5 阶段图勾新、§10 待办清账
- 远期：REQ-02 容器化（需厂商容器内核镜像 + isulad 包）/ REQ-03 整机安装（需五项厂商要货，见 plan §9.2）
