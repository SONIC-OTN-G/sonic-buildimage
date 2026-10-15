# SONiC-OTN RTOS 移植需求清单

> 文档版本: v2.1（REQ-01 增补阶段 0 冒烟测试——已完成，判定 GO，记录见计划文档 §5.1）
> 上游文档: `docs/rtos-poc-plan.md`（整合版计划）
> 格式说明: 每条需求按 **标题 / 描述 / 预算人力（人月）/ 输入 / 处理 / 输出** 六要素描述
> 预算口径: 1 人月 ≈ 21 工作日；总计 4.45 人月

---

## REQ-01 RTOS 裸进程 PoC

- **标题**: SONiC-OTN 在 HI1231 RTOS 上的裸进程 PoC
- **描述**: 验证 SONiC-OTN 核心软件栈（redis → syncd-ot(OTAI vs 模拟) → OTSS 三进程 → 远程 CLI）在设备现有 RTOS 环境上以裸进程形态完整跑通业务链路，验证范围为 `README.otn.md` 演示的全部功能（线卡开通/去激活、状态/告警/PM 查询、单属性配置、VOA/EDFA/OSC 查询）。真机 OTAI 对接、容器化、整机替换均不在本需求范围内。内部包含七个阶段：**阶段 0 冒烟前置验证（已完成，GO）** → 环境基线确认 → arm64 构建 → payload 组装 → 设备部署拉起 → CLI 远程化 → 全链路验证。
- **预算人力**: 0.70 人月（冒烟 0.05 内含 + 基线 0.05 + 构建 0.15 + 组装 0.10 + 部署 0.15 + CLI 0.15 + 验证 0.10）
- **输入**: 设备 SSH 访问权限；sonic-buildimage `otn_pre_202411` 分支源码；x86 构建服务器（Docker ≥ 17.06.1 + QEMU binfmt）；Bullseye arm64 软件源；P230C/E110C 线卡配置模板与 otai_sim_data 数据集
- **处理**:
  0. **阶段 0 冒烟前置验证【已完成，GO，2026-10】**：bullseye redis-server 6.0.16 + 17 个 deb 依赖闭包（收敛方法与组装规范见计划文档 §4.5）裸进程上机，验证 ELF 加载、glibc 2.34 符号契约（实测最大 GLIBC_2.29）、运行库链、unix socket + TCP 双监听、多 DB 布局、SONiC 等效 redis.conf 全量生效；同时发现设备 /tmp 疑似 noexec（部署须用 rw+exec 分区）（执行记录见计划文档 §5.1）
  1. 环境基线确认：补测 `cat /proc/mounts` 归档正式部署分区（rw+exec）与页大小（`grep -m1 KernelPageSize /proc/self/smaps`）（其余探测已完成：glibc 2.34、busybox init、无容器能力、sshd/ip 可用、/tmp noexec）
  2. arm64 构建：QEMU multiarch 构建 3 个 SONiC 包（`libswsscommon`、`sonic-otss`、`sonic-otairedis` 含 libotai/libotai-vs/libotairedis/syncd-ot）；运行库直接取用 Bullseye arm64 包（redis 栈 15 个 deb 已冒烟验证，另加 libstdc++6/libgcc-s1 及 swsscommon 依赖 libhiredis/libzmq/yaml-cpp/libnl-3）；**构建须锁定 bullseye slave**（BLDENV 由 slave 容器 lsb_release 决定，slave.mk:30，防止顺默认 bookworm slave 出 glibc 2.36 基线产物）；只出 .deb 目标不出安装器
  3. payload 组装：vs 模拟数据路径由编译期常量改为环境变量（`src/sonic-otairedis/vslib/OtaiObjectSimulator.h:9`）并重编；x86 侧预生成 dump.rdb 预置 CONFIG_DB；编写 busybox-sh 兼容 start/stop 脚本；解包组装自包含 tarball（.deb 仅存在于构建机，设备只接收目录树）；**执行 §4.5 组装规范**（usrmerge 双路径收集、收敛循环、glibc 符号放行门 ≤ 2.34、LUA_CPATH 处理 cjson）
  4. 设备部署拉起：scp 部署到 rw+exec 分区，按序拉起 redis-server（unix socket + TCP 127.0.0.1:6379）→ syncd-ot(vs) → OTSS 三进程（linecardmgrd/configsyncd/orchagent），确认就绪
  5. CLI 远程化：x86 主机部署 sonic-utilities（建议在 bullseye 容器内运行，与构建链同源）；SSH 隧道 + socat 将设备 redis TCP 映射为本地 unix socket（`/var/run/redis{N}/redis.sock`，见 db.py:9-11）；PoC fork 改造 `slot.py:261-307` 的本地编排命令（systemctl/docker exec → ssh 远程执行，j2/sonic-cfggen 留 x86 经隧道写库）
  6. 全链路验证：执行 8 项验收清单（计划文档 §7），问题分级（阻塞/非阻塞），输出 go/no-go 结论
- **输出**: 阶段 0 冒烟记录（已产出，计划文档 §5.1）；设备上稳定运行的最小进程链；PoC 验证报告（含验收结果矩阵、与 ot-vs 基准环境差异表、go/no-go 结论）；部署操作手册；slot.py PoC fork 补丁（独立分支管理）
- **预案**: QEMU multiarch 编译失败 → BiSheng SDK 动态编译（glibc 2.34 sysroot 兼容）→ aarch64-musl 全静态（源码预检已确认无 musl 阻塞项：无 execinfo、无 dlopen）

## REQ-02 容器化运行（iSulad 适配）

- **标题**: SONiC-OTN 容器栈在设备上的 iSulad 容器化
- **描述**: 在厂商提供容器能力后（当前设备镜像无 isulad 二进制、无 /sys/fs/cgroup、无 overlayfs，容器路线不可行），将 SONiC-OTN 最小容器集（docker-database / docker-syncd-ot / docker-orchagent-ot）以 iSulad 运行于设备，验证容器形态可行性并输出生产化宿主层适配的工作量评估。容器自带 Debian glibc rootfs，不受宿主用户态影响。
- **预算人力**: 0.75 人月（镜像构建 1–2 天 + isula 适配 3–5 天 + 联调 1–2 周）
- **输入**: REQ-01 PoC 结论（go）；**厂商件：含 CONFIG_CGROUPS/CGROUPS_V1|V2、CONFIG_NAMESPACES、CONFIG_OVERLAY_FS 的内核镜像 + isulad 软件包 + 可写 rootfs/overlay**；REQ-01 的 arm64 构建环境复用
- **处理**:
  1. QEMU multiarch 构建三个最小容器镜像的 arm64 版本（database / syncd-ot-vs / orchagent-ot）
  2. 镜像导入：`isula load` docker-archive 格式（不支持则 skopeo 转 OCI 布局兜底）
  3. `isula run` 以 host 网络模式手动拉起（挂载 /var/run/redis、/etc/sonic、otai_sim_data；vs 模拟无需 privileged）
  4. 对齐宿主层差异：梳理 `files/build_templates/docker_image_ctl.j2`（812 行）对 docker CLI 的依赖（start/inspect/exec/cp/network/restart policy/rsyslog 链路）与 isula 命令面的映射表
  5. 输出生产级宿主层适配的工作量评估与方案草案
- **输出**: 设备上以容器形态运行的 SONiC-OTN 最小集；iSulad 与 Docker 差异清单及适配评估报告；生产化容器方案草案
- **风险**: iSulad 镜像格式兼容性（skopeo 转换兜底）；iSulad 网络能力弱于 Docker（单 ASIC + host 网络规避，多 ASIC 需评估 `ip netns` 方案，其本身不依赖 Docker）；多 ASIC 编排脚本需重写
- **备注**: 生产化同样需要真机 OTAI 库，可复用 REQ-03 第一阶段产出

## REQ-03 整机安装（SONiC-OTN 替换 RTOS）

- **标题**: HI1231 整机替换为 SONiC-OTN 独立网元
- **描述**: 为 HI1231 打造完整 SONiC-OTN arm64 整机镜像，替换现有 RTOS 成为独立网元。含真机 OTAI 库对接（作为本需求第一阶段，因其为整机替换五项厂商前提之一）。注：设备无 ONIE，"安装"指厂商刷写机制或 dd 镜像方式，并非 ONIE 安装器（ONIE 为 bootloader 预安装环境，本设备为厂商自有 bootloader + squashfs 只读根 + OP-TEE 安全启动）。
- **预算人力**: 3.00 人月（真机 OTAI 对接 0.5 + 平台工程 2.5，约 2–3 个月级工程）
- **输入**: REQ-01 PoC 结论（go）；**五项厂商要货**：① 板级启动流程与镜像格式 + 串口救砖手段 ② OP-TEE/verified boot 签名策略（开发者签名或关闭校验）③ BiSheng 5.10 内核源码/BSP + DTB ④ OTN 板级硬件访问层（FPGA/CPLD 驱动、/dev 接口）⑤ 刷写通道；厂商 OTAI 库 aarch64 版本；BiSheng 交叉 SDK
- **处理**:
  1. 真机 OTAI 对接：BiSheng SDK 下试编译厂商 OTAI 库，梳理板级驱动依赖（注意 RT 内核 spinlock 语义对 out-of-tree 驱动的影响），与 vs 库行为差异比对
  2. 新建 `platform/hi1231` 平台（参照现有 arm64 平台模式如 centec-arm64）
  3. arm64 全栈构建 + 厂商内核集成（内核 .deb 化或复用厂商内核 + SONiC 用户态组合）
  4. 安全启动签名打通（OP-TEE 策略落地）
  5. 制作刷写镜像、恢复流程与救砖手册
- **输出**: `platform/hi1231` 平台代码；可刷写的 SONiC-OTN arm64 整机镜像；救砖/恢复手册；真机 OTAI 对接报告（此产出可反哺 REQ-02 生产化）

---

## 预算汇总

| 需求 | 预算（人月） | 阶段 |
|---|---|---|
| REQ-01 RTOS 裸进程 PoC | 0.70 | 立即可启动（余一项设备补测） |
| REQ-02 容器化运行（iSulad） | 0.75 | 前置：厂商出容器内核镜像 + isulad 包 |
| REQ-03 整机安装 | 3.00 | 前置：五项厂商要货齐备 |
| **总计** | **4.45** | |

## 依赖关系

```
REQ-01 PoC ──→ REQ-02 容器化（生产化路线二选一或并行评估）
    │              ↑ 真机 OTAI 产出反哺（来自 REQ-03 阶段 1）
    └──────→ REQ-03 整机安装
```

- REQ-01 与 REQ-02/03 之间为决策依赖：PoC 结论（go/no-go）决定后两者是否立项
- REQ-02 与 REQ-03 互为替代生产化路线；REQ-03 阶段 1（真机 OTAI）为两者共同的生产化地基
