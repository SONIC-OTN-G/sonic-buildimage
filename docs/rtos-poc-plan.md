# SONiC-OTN 在 RTOS 设备（HI1231）上的移植 PoC 计划

> 文档版本: v1.2（阶段 0 冒烟测试完成，判定 GO；新增 §4.5 组装规范与 §5.1 冒烟执行记录）
> 代码基线: sonic-otn `otn_pre_202411` 分支（Debian Bullseye 用户态）
> 目标设备: HI1231，`arm64le-preempt-pro-llvm-5.10`（BiSheng Embedded）
> 前置阅读: `README.otn.md`（功能演示基准）、`docs/distributed-chassis-architecture.md`（部署形态演进）
> 状态: 阶段 0 冒烟已完成（GO）；待归档：设备挂载表（`cat /proc/mounts`）与页大小
> 配套文档: `docs/rtos-poc-requirements.md`（需求清单：REQ-01 PoC / REQ-02 容器化 / REQ-03 整机安装，按标题/描述/预算人力/输入/处理/输出格式）

---

## 1. 背景与目标

### 1.1 背景

需要在 RTOS 设备（独立网元/整机形态）上运行 SONiC-OTN。该 RTOS 为基于 Linux 的嵌入式发行版，版本标识 `arm64le-preempt-pro-llvm-5.10`。SONiC-OTN 本身是完整的 OS 发行版（Debian Bullseye 用户态 + 5.10 内核 + Docker 容器 + ONIE 安装器），两者不能直接叠加，需评估移植路径。

### 1.2 PoC 目标

**验证命题**：SONiC-OTN 的核心软件栈（redis → syncd-ot(OTAI vs 模拟) → OTSS 三进程 → CLI）能够在 HI1231 的现有 RTOS 环境上以裸进程形态完整跑通业务链路。

**验证范围**：`README.otn.md` 中演示的全部功能（线卡开通、状态/告警/PM 查询、单属性配置、光放/衰减器/OSC 查询）。

**明确不在范围内**：真机 OTAI 对接（PoC 后独立评估）、容器化、整机 OS 替换。

## 2. 设备环境基线（探测结论归档）

### 2.1 探测结果

| 探测项 | 结果 | 判定 |
|---|---|---|
| 内核 | `Linux HI1231 5.10.0 #1 SMP PREEMPT aarch64 GNU/Linux` | 与 SONiC Bullseye 内核代际（5.10）一致 |
| 工具链 | BiSheng Embedded CPU 305.0.0.B019，clang 15.0.4，GNU ld 2.41 | 华为毕昇嵌入式工具链，openEuler Embedded 系 |
| libc | **glibc 2.34**（`/lib64/libc.so.6`） | ≥ 2.31，**Debian Bullseye arm64 二进制可直接运行** |
| init | `/sbin/init` → `../bin/busybox.nosuid` | busybox init，无 systemd |
| Python3 / systemctl / isula / redis / socat / gcc / dnf | 均不存在 | 极简嵌入式 rootfs，SONiC 宿主层不可用 |
| 容器能力 | 无 isula* 二进制；无 `/sys/fs/cgroup`；`/proc/filesystems` 仅 squashfs 无 overlay | **容器路线在此镜像上不可行** |
| 安全特性 | `/lib/tee` 存在（OP-TEE），疑似只读 squashfs 根 | 整机替换需解决签名校验 |
| 远程访问 | sshd、iproute2（ip）可用 | SSH 隧道方案成立 |
| 内存 | 约 5.7 GB | 充足（4 槽模拟 VM 基准为 3 GB） |
| 内核配置 | 无 `/proc/config.gz` | 以运行时探测替代 |
| 可写分区 | 冒烟已在一处 rw+exec 分区部署成功；`cat /proc/mounts` 输出待归档定名 | 部署可行，正式分区名待归档 |
| /tmp 执行权限 | 冒烟实测 `Permission denied`（已加执行位）——疑似 noexec 挂载 | payload 不部署于 /tmp；候选分区以挂载表为准 |
| 页大小 | 设备无 getconf；改用 `grep -m1 KernelPageSize /proc/self/smaps` | 结果待归档；glibc 动态程序对页大小不敏感，低风险 |

### 2.2 源码侧预检结论（本仓库已完成）

| 检查项 | 结论 |
|---|---|
| musl/glibc 特有 API（execinfo/backtrace 等） | swss-common / otss / otairedis 均未使用，无移植阻塞 |
| 动态库加载方式 | syncd-ot 与 libotai-vs 直接链接，无 dlopen，可全静态 |
| vs 模拟数据路径 | 编译期常量硬编码（`src/sonic-otairedis/vslib/OtaiObjectSimulator.h:9`，`/usr/include/vslib/otai_sim_data/`），需改为环境变量 |
| CLI 本地依赖 | `src/sonic-utilities/otn/` 全部模块中仅 `slot/slot.py`（261-307 行）编排本机命令（systemctl/docker exec/j2/sonic-cfggen/cp），其余纯 redis 读写 |
| CLI 连库路径 | `src/sonic-utilities/otn/utils/db.py:6-12`：slot 1 → `/var/run/redis/redis.sock`，slot N → `/var/run/redis{N-1}/redis.sock` |
| arm64 构建支持 | 构建系统支持 multiarch（`Makefile.work:165-180`，QEMU/交叉两种模式），但 `platform/ot-vs` 的安装器部件为 x86_64 专属（PoC 不需要） |
| **镜像分层实测** | host 镜像 = bookworm + 内核 6.1（VM 实测）；OTN 容器栈 = bullseye（`docker-syncd-ot-vs` 与 `docker-orchagent-ot` 的 Dockerfile 均 `FROM docker-config-engine-bullseye`）；database 容器 = bookworm + stock redis 7.0.11（`rules/docker-database.mk:22`，`Dockerfile.j2:13` 为 `apt-get install`） | Track A 取用物全部来自容器侧 bullseye 链，host 版本不影响；arm64 构建须锁定 bullseye slave（BLDENV 由 slave 容器 lsb_release 决定，`slave.mk:30`） |
| **glibc 符号基线实测** | `syncd`：GCC 10.2.1（bullseye 工具链，`.comment` 段实证）、最大 GLIBC_2.30、ELF 标 `for GNU/Linux 3.2.0`；`libotairedis.so`：最大 GLIBC_2.14；`redis-server 6.0.16 arm64`：最大 GLIBC_2.29 | 全部 ≤ 设备 2.34，兼容性从推断升级为实测 |

## 3. 可行性结论

**条件可行，路径为裸进程（Track A）**：

1. **软件栈层面可行**：glibc 2.34 ≥ 2.31 使 Debian Bullseye arm64 二进制可直接运行；内核 5.10 与 SONiC-OTN 代际一致；PREEMPT/LLVM 内核不影响用户态 ABI。
2. **容器路线（Track B）在此设备镜像上不成立**：无容器运行时、无 cgroup、无 overlayfs，三项全缺。复活条件见 §9。
3. **ONIE 整机安装路线不成立**：设备无 ONIE（ONIE 是 bootloader 预安装环境，此设备为厂商自有 bootloader + squashfs 只读根）；OP-TEE 安全启动可能阻止自建内核；SONiC 通用内核缺板级 BSP/DTB/OTN 驱动。整机替换作为独立大工程后置，前提清单见 §9。
4. **BiSheng SDK 不阻塞 PoC**（glibc 兼容可直接复用 Bullseye 二进制），但为真机 OTAI 对接的必要条件。

## 4. 技术方案（Track A：裸进程 + 远程 CLI）

### 4.1 总体架构

```
┌─ x86 构建服务器 / CLI 主机 ─────────┐      ┌─ RTOS 设备 HI1231 ─────────────────┐
│                                    │      │ (busybox init, glibc 2.34, sshd)   │
│  构建（REQ-01 阶段 2）：             │      │                                     │
│   QEMU multiarch 出 arm64 .deb     │ scp  │  可写分区 /data/sonic-poc/：        │
│   + Bullseye arm64 运行库下载      │ ───→ │   ├─ bin/   (redis-server,          │
│                                    │      │   │          syncd-ot, linecardmgrd,│
│  payload 组装（REQ-01 阶段 3）：    │      │   │          configsyncd, orchagent)│
│   二进制+捆绑库+sim 数据+dump.rdb   │      │   ├─ lib/   (LD_LIBRARY_PATH 隔离)  │
│   + busybox-sh 启动脚本             │      │   ├─ otai_sim_data/ (vs 模拟数据)  │
│                                    │      │   ├─ dump.rdb (CONFIG_DB 预置)     │
│  CLI（REQ-01 阶段 5）：             │      │   └─ scripts/ (start/stop)         │
│   sonic-utilities (Debian python)  │      │                                     │
│   + socat: unix sock ⇄ TCP         │◄─────┤  redis-server:                     │
│                                     │ SSH  │    unix: /var/run/redis/redis.sock│
└────────────────────────────────────┘ 隧道 │    tcp:  127.0.0.1:6379            │
                                           └─────────────────────────────────────┘
```

数据链路（与标准 SONiC-OTN 一致）：

```
CLI → CONFIG_DB → OTSS(configsyncd/linecardmgrd/orchagent) → APPL_DB → orchagent
    → ASIC_DB → syncd-ot → libotai-vs(模拟硬件) → OTAI 通知 → STATE_DB/COUNTERS_DB → CLI
```

### 4.2 构建策略（三层保险，按优先级）

| 优先级 | 方案 | 条件 | 状态 |
|---|---|---|---|
| 1 | 直接复用：Bullseye arm64 .deb 解包二进制 + 捆绑运行库 | glibc ≥ 2.31 | ✅ 已满足（2.34），PoC 采用 |
| 2 | BiSheng SDK 对厂商 sysroot 动态编译 | SDK 到位 | 真机 OTAI 对接时采用 |
| 3 | aarch64-musl 全静态 | 无任何前提 | 兜底（已验证无 musl 阻塞项） |

需要 arm64 构建的 SONiC 包（仅 3 个）：`libswsscommon`、`sonic-otss`、`sonic-otairedis`（含 libotai/libotai-vs/libotairedis/syncd-ot）。

直接取用的 Bullseye arm64 运行库（redis 侧 17 个 deb 已在阶段 0 冒烟闭包验证，清单见 §5.1）：
- redis 栈：redis-server、redis-tools、libjemalloc2、libssl1.1、libatomic1、liblzf1、liblua5.1-0、lua-bitop、lua-cjson、libsystemd0、libgcrypt20、libgpg-error0、liblz4-1、liblzma5、libzstd1
- C++ 栈（完整 payload 必需，OTN 三件套全 C++）：libstdc++6、libgcc-s1（均 10.2.1-6，与构建链同源）
- swsscommon 依赖（完整 payload 追加）：libhiredis、libzmq、yaml-cpp、libnl-3 系列

redis 版本决策：基准 VM 的 database 容器实际运行 bookworm stock redis 7.0.11，但 bookworm 包按 glibc 2.36 基线链接（设备 2.34 有符号风险）；PoC 取 bullseye 6.0.16（glibc 2.31 基线，实测最大符号 2.29）。OTN 栈用到的 redis 特性（RESP、keyspace 通知、pub/sub、多 DB、RDB 加载）在 6.x 全部存在，版本差异风险可忽略。

### 4.3 CLI 远程化

| 命令类别 | 示例 | 可用性 |
|---|---|---|
| 状态查询 | `show slot 1 info / alarm current / line 1 state`、`show slot 2 voa/edfa/osc all info` | 零改动（纯 redis 读） |
| 单属性配置 | `config slot 2 voa 1 attenuation 20.0`（edfa/osc/olp/ocm/otdr/wss/port 同） | 零改动（redis 写 + pub/sub 同步确认） |
| 整卡开通 | `config slot 1 type P230C` / `type NONE` | 需 PoC fork 改造（见下） |

`config slot N type X` 改造点（`slot.py:261-307`）：
- `systemctl stop/start otss@N` / `syncd-ot@N` → `ssh root@HI1231 /data/sonic-poc/scripts/restart-otss.sh N`
- `docker exec syncd-otN touch /tmp/linkup` → `ssh root@HI1231 touch /tmp/linkup`（裸进程模式下 syncd-ot 直接可见设备 /tmp，语义一致）
- `j2 模板渲染` + `sonic-cfggen --write-to-db` 留在 x86 侧执行，经隧道写入设备 redis
- `cp 线卡模板` 指向 x86 侧副本

多槽位说明：CLI 按 slot 号连不同 socket（db.py:9-11）。PoC 验证 slot 1 只需 1 个 redis 实例 + 1 条隧道；多槽位为设备多 redis 实例 + x86 多 socat 映射，纯重复部署。

### 4.4 CONFIG_DB 初始化（绕过 sonic-cfggen/python）

x86 侧预生成 `dump.rdb`（先起 redis 填好 schema 后 `SAVE`），随 payload 部署，设备 redis-server 启动即加载。整卡开通所需的 `power-admin-state` 置位与 `linkup` 触发由脚本/改造后 CLI 完成。

### 4.5 裸进程 payload 组装规范（阶段 0 冒烟沉淀）

**取包**：
- 容器内 `apt download <pkg>:arm64` 自动解析版本（`docker run --rm -v $PWD/debs:/out -w /out debian:bullseye`；循环逐包下载，防单包失败丢全部）
- bullseye-security 索引指向已迁移文件时 404（实例：libsystemd0 的 u8、liblzma5 的 u2，安全池中 247.x/5.2.5 系文件已清空）→ 解法：**主池直取实存版本**（`libsystemd0_247.3-7+deb11u5_arm64.deb`、`liblzma5_5.2.5-2.1~deb11u1_arm64.deb`）；安全修订不改变 so ABI

**解包**：
- `.deb` 为 ar 归档：`ar p <deb> data.tar.xz | tar -xJ -C bundle`（非 Debian 宿主无需 dpkg）
- **usrmerge 双路径规范**（3 次应验）：部分包仍按旧布局安装到 `/lib/aarch64-linux-gnu/`（实测：libgpg-error0、liblzma5、libgcc-s1），解包树中无 `/lib → /usr/lib` 符号链接，**两条路径的库都必须收集**

**依赖闭包（收敛循环）**：
- 对 `bin/*` 与两条 lib 路径下所有 `.so*` 迭代 `readelf -d | grep NEEDED`，直到不再出现新的非 glibc 依赖
- 修剪死重：未被引用的 `liblua5.1-c++*`、`liblua5.2-*`（libstdc++/libgcc_s 假需求的来源）
- `libstdc++6` + `libgcc-s1` 必捆：libatomic→libgcc_s 链不可剪，且 OTN 栈全 C++
- **glibc 家族严禁捆绑**（libc/libm/libpthread/librt/libdl/ld-linux）：与设备 2.34 混版本（2.31 libc + 2.34 ld.so）会直接崩溃，必须用设备系统 glibc

**放行检查**（每个产物）：
- `objdump -T <binary> | grep -oE "GLIBC_2\.[0-9]+" | sort -Vu`：最大符号 ≤ 2.34
- `readelf -d <binary> | grep NEEDED`：每项 ∈ {设备 glibc 家族} ∪ {捆绑库}

**运行时杂项**：
- Lua cjson 装饰性报错（"Error loading cjson library"）：Debian redis 的 Lua `require` 走绝对 cpath，非 Debian 根下必然报错；SONiC-OTN 全链路不使用 EVAL+cjson（唯一 EVAL 在 `files/scripts/otss.sh:44-52`，仅 KEYS/DEL），无害。消除法（完整 payload 阶段做）：恢复 `lua/5.1/` 模块目录 + 启动加 `LUA_CPATH="./lib/lua/5.1/?.so"`
- 设备无 getconf，页大小用 `/proc/self/smaps` 的 `KernelPageSize` 字段

## 5. 执行阶段划分

REQ-01（PoC）内部七阶段：阶段 0 冒烟已完成（GO），其余串行，PoC 结论触发 REQ-02 / REQ-03 立项决策：

```
REQ-01 RTOS 裸进程 PoC（0.70 人月）
  阶段 0 冒烟测试 ✅ GO（执行记录见 §5.1）
   └→ 阶段 1 环境基线（余量：挂载表/页大小归档）
       └→ 阶段 2 arm64 构建 ──→ 阶段 3 payload 组装 ──→ 阶段 4 设备部署拉起
                                                          └→ 阶段 5 CLI 远程化 ──→ 阶段 6 全链路验证
                                                                               │
                                                          PoC go/no-go 决策点 ◄─┘
                                                                               │
                                          ┌────────────────────────────────────┴──────┐
                                          ▼                                             ▼
                      REQ-02 容器化运行 iSulad（0.75 人月）        REQ-03 整机安装（3.00 人月）
                      前置：厂商容器内核镜像 + isulad 包            前置：五项厂商要货齐备
                                                               （阶段 1 真机 OTAI 对接 0.5 人月，
                                                                 产出反哺 REQ-02 生产化）
```

### 5.1 阶段 0 冒烟测试执行记录（2026-10 完成，判定 GO）

**命题**：验证"Debian bullseye arm64 二进制可直接运行于设备 RTOS"这一 Track A 地基假设。

**方法**：仅取 redis-server（bullseye 6.0.16）+ 依赖闭包（17 个 deb，收敛方法见 §4.5），组装自包含目录树（bin/lib/redis-smoke.conf），scp 上机以裸进程运行（`LD_LIBRARY_PATH=./lib`），配置为 docker-database 8 条 sed 的等效参数（unixsocket、databases 100、notify-keyspace-events AKE、pubsub 不限流等）。

**依赖闭包结果**：全部产物 NEEDED 共 22 项——设备 glibc 家族 6 项 + 捆绑 16 个 so，零缺口。

**17 个 deb 清单**：redis-server、redis-tools、libjemalloc2、libssl1.1、libatomic1、liblzf1、liblua5.1-0、lua-bitop、lua-cjson、libsystemd0、libgcrypt20、libgpg-error0、liblz4-1、liblzma5、libzstd1、libstdc++6、libgcc-s1

**验证结果**：

| 验证点 | 结果 |
|---|---|
| ELF aarch64 加载 + 设备 /lib64 加载器 | ✅ |
| glibc 2.34 符号契约（redis-server 最大 GLIBC_2.29） | ✅ |
| 进程/事件循环/jemalloc/openssl/lua 运行库链 | ✅ |
| unix socket + TCP 双监听 | ✅ |
| 多 DB 布局（databases 100，`-n 4` set/get 回读） | ✅ |
| SONiC 等效 redis.conf 全量生效（keyspace 通知 AKE、pubsub 不限流、禁自动快照） | ✅ |
| Lua cjson 模块路径 | ⚠️ 装饰性报错，SONiC 用途无影响（定性见 §4.5） |
| 部署约束 | /tmp 不可执行（疑似 noexec），实际部署于 rw+exec 分区成功 |

**结论**：**GO**。"bullseye 二进制直接复用"从假设变为实测事实，§4.2 构建策略优先级 1 就绪，阶段 2 起的全部工作解除地基风险。

## 6. 风险与预案

| # | 风险 | 影响 | 预案 |
|---|---|---|---|
| 1 | otss/otairedis 在 QEMU multiarch 下编译失败 | 阶段 2 延期 | BiSheng SDK 动态编译（glibc 2.34 sysroot 与 Bullseye 兼容）；最坏切 musl 静态 |
| 2 | 设备运行库符号/版本冲突 | 进程起不来 | LD_LIBRARY_PATH 已隔离捆绑库；必要时逐项 ldd 排查 |
| 3 | 只读根导致预期路径不可写 | 部署失败 | 产物全放可写分区；sim 数据路径已改 env；redis 数据目录可配 |
| 4 | vs 库假设与裸进程环境不符（如 /tmp、信号处理） | 功能异常 | PoC 修复清单制，逐项小改源码重编（构建链路已打通，成本低） |
| 5 | slot.py 改造遗漏本地编排命令 | 整卡开通失败 | 方案 b 兜底：开通步骤封装为设备侧脚本 |
| 6 | 设备资源/存储不足 | 部署受限 | payload 精简（约 200-300 MB）；待挂载表归档确认 |
| 7 | 设备 /tmp 挂载 noexec（冒烟实测 Permission denied） | 部署失败 | 部署于 rw+exec 分区（冒烟已验证可行）；正式分区以 `cat /proc/mounts` 归档为准 |

## 7. 验收标准（对应 REQ-01 阶段 6）

以 `README.otn.md` 演示功能为基准，全部满足则 PoC 通过：

1. `config slot 1 type P230C`（改造后 CLI 或等价脚本）→ LINECARD 对象创建成功，OTSS 状态机走完 NOT_READY→READY→WORK
2. `show slot 1 config / info` 输出字段结构与 README 示例一致（Card Type、Board mode、Oper Status=ACTIVE 等）
3. `show slot 1 alarm current` 可见 vs 模拟的 `HIGH_TEMPERATURE_ALARM`
4. `show slot 1 line 1 state` 返回 TRANSCEIVER 状态与 PM 数据（TxPower、Frequency、OSNR 等）
5. `config slot 2 type E110C` → `show slot 2 voa/edfa/osc all info` 正常
6. `config slot 2 voa 1 attenuation 20.0` 单属性配置经同步确认协议返回成功
7. `config slot 1/2 type NONE` 去激活、清库正常
8. （加强项）最小进程链持续运行 24h 无 crash

## 8. 预算汇总

| 需求 | 预算（人月） | 前置条件 |
|---|---|---|
| REQ-01 RTOS 裸进程 PoC | 0.70 | 无（余一项设备补测） |
| REQ-02 容器化运行（iSulad） | 0.75 | 厂商容器内核镜像 + isulad 包 |
| REQ-03 整机安装 | 3.00 | 五项厂商要货齐备 |
| **合计** | **4.45** | |

明细（含各需求输入/处理/输出）见 `docs/rtos-poc-requirements.md`。

## 9. PoC 后决策点与厂商要货清单

### 9.1 决策树

```
Track A PoC 通过（REQ-01）
  └─→ 生产化路线二选一（或并行评估）：
        ├─ REQ-02 容器化：需厂商提供带 CONFIG_CGROUPS/NAMESPACES/OVERLAY_FS
        │   的内核镜像 + isulad 软件包 + 可写 rootfs/overlay
        └─ REQ-03 整机安装：需下表 5 项全齐（约 2-3 个月级工程）
              └─ 其阶段 1（真机 OTAI 对接，BiSheng SDK + 厂商硬件访问层）
                 同时是"整机替换"前提 #4，产出可反哺 REQ-02 生产化
```

### 9.2 整机替换前提清单（向厂商要货）

| # | 前提 | 说明 |
|---|---|---|
| 1 | 板级启动流程与镜像格式 | uboot/EFI？刷写方式？串口救砖手段 |
| 2 | OP-TEE/verified boot 策略 | 能否开发者签名或关闭校验 |
| 3 | BiSheng 5.10 内核源码/BSP + DTB | 做成 .deb 或复用厂商内核 + SONiC arm64 用户态 |
| 4 | OTN 板级硬件访问层 | FPGA/CPLD 驱动、/dev 接口——真机 OTAI 库的地基 |
| 5 | sonic-buildimage 新平台 `platform/hi1231` | 参照现有 arm64 平台模式（如 centec-arm64） |

## 10. 待办（开放项）

- [x] 阶段 0 冒烟测试（2026-10 完成，GO，执行记录见 §5.1）
- [ ] 设备侧补充 `cat /proc/mounts` 输出归档（确定正式部署分区名与容量，REQ-01 阶段 1 收尾）
- [ ] 设备侧补充 `grep -m1 KernelPageSize /proc/self/smaps` 结果归档
- [ ] 向厂商索取 BiSheng SDK（不阻塞 REQ-01，阻塞 REQ-03 阶段 1 真机 OTAI 对接）
- [ ] 确认 OTN 硬件厂商 OTAI 库的 aarch64 提供计划（REQ-03 输入）
- [ ] 向厂商提出容器内核镜像需求（CONFIG_CGROUPS/NAMESPACES/OVERLAY_FS + isulad 包，REQ-02 前置）
