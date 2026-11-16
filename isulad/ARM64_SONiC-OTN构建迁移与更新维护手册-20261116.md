# ARM64 SONiC-OTN 构建、迁移与更新维护手册

编制日期：2026-11-16（仓库复现版）  
归档路径核对：2026-11-16  
适用范围：现有 iSulad ARM64 离线引擎、database/OTSS/syncd 独立镜像，以及 D:\isulad 中的业务联调预览包和测试补丁。

本文依据现有构建脚本、源码版本记录、交付归档及既有验证结果编写。历史构建事实与后续维护建议分别注明。本文不代表完整 SONiC-OTN 发行版已经在目标设备验收；当前联调仍使用虚拟 OTAI，平台生命周期、完整北向模型、持久化及恢复存在未完成项。

## 1. 交付架构与维护原则

当前交付由三条构建路线组成，并非完整 SONiC 系统镜像重新编译后的直接替代品。

| 路线 | 输入 | 输出 | 职责 |
| --- | --- | --- | --- |
| 离线引擎 | openEuler ARM64 用户态、官方 iSulad RPM、runc 源码及补丁、脚本 | isulad-offline-arm64-fix-20261106-r1.tar.xz | 提供容器引擎与设备兼容环境 |
| 业务镜像 | SONiC 相关模块、ARM64 Debian 基础层和运行依赖 | database、OTSS、syncd 三个镜像归档 | 提供业务程序及运行库 |
| 联调增量 | 更新的业务二进制、虚拟后端、入口和配置、平台与北向组件 | otn-linklab / otn-system 归档及测试补丁 | 在现有镜像之上建立选定业务链路 |

iSulad 本身不由 SONiC 源码生成。修改 orchagent 通常不需要重建引擎；修改 runc 则需要更新引擎并回归业务容器。Docker 是开发机上的构建工具，设备侧用 iSulad 导入并运行兼容镜像，不要求构建工具与部署引擎相同。

镜像中的程序与增量挂载中的程序可能并存。发布必须记录实际执行路径，不能只记录镜像标签。每次更新应能追溯：源码提交 → 编译产物 → 镜像/增量包 → 设备执行文件 → 验证记录。

## 2. 仓库布局与复现路径

### 2.1 面向复现的路径约定

本文仅使用两个项目根目录，不依赖原开发者的个人工作目录：

- `sonic-buildimage/`：SONiC-OTN 源码仓库根目录。克隆所需仓库及分支到该目录，按发布清单固定提交并初始化子模块。
- `D:\isulad\`：本软件包仓库根目录，包含已构建交付件、文档，以及已收录的构建辅助文件。在 WSL 中对应 `/mnt/d/isulad`。

`sonic-buildimage` 是逻辑根目录名称，不要求位于某个用户主目录。Linux 示例中的 `SONIC_SRC` 应设置为源码仓库的实际绝对路径。设备侧使用 `ENGINE` 表示已解压的引擎目录，不使用 Windows 路径。

构建材料现已收录，实际布局如下。可执行命令以 [build/README.md](build/README.md) 为准。

| 目录 | 用途 | 当前状态 |
| --- | --- | --- |
| sonic-buildimage/src/、files/、dockers/、rules/ | 后续开发和原生部署源码 | 独立源码仓库 |
| D:\isulad\build\sources\ | swsscommon、otairedis/OTAI、OTSS 快照、原 database 定义、runc 原始源码归档 | 已收录 |
| D:\isulad\build\images\ | 三个镜像的 Dockerfile、编译脚本、独立入口 | 已收录 |
| D:\isulad\build\engine\ | 冻结用户态依赖、设备兼容脚本、runc 补丁和构建器 | 已收录 |
| D:\isulad\build\locks\ | 原始来源、编译器身份和软件包版本清单 | 已收录；APT 版本清单用于审计，未锁快照源 |
| D:\isulad\build\scripts\ | 源码导入和输入摘要校验工具 | 已收录 |
| D:\isulad\build\tests\ | database/OT/基础测试 | 已收录 |
| D:\isulad\build\rebuild.py | 从独立工作目录编译、装配、导出 | 新统一入口 |
| D:\isulad\build\otn-system\ | 联调、平台及北向系统的开发源码 | 不在本次补齐范围，不能由独立镜像构建入口生成 |

临时 target、容器编译工作树和 dist 均由 `--work` 指定，位于 Linux 文件系统。不要将输出写回源码输入目录。完整 SONiC 发行版与联调预览包的重建不在本次四个交付件的构建范围内。

### 2.2 统一软件包工作目录

软件包归档、上传准备和发布清单统一以 `D:\isulad` 为工作目录，WSL 对应 `/mnt/d/isulad`。该目录同时存放交付件和独立构建输入，不等于完整 SONiC 发行版源码仓库，也不是目标设备上的安装路径。

```text
D:\isulad\
├── ARM64_SONiC-OTN构建迁移与更新维护手册-20261116.md
├── ARCHIVE-SHA256SUMS.txt
├── docker-database-standalone-arm64.tar.gz
├── docker-otss-standalone-arm64.tar.gz
├── docker-syncd-standalone-arm64.tar.gz
├── isulad-offline-arm64-fix-20261106/
│   ├── isulad-offline-arm64-fix-20261106-r1.tar.xz
│   ├── isulad-exec-fix-source-20261106.tar.gz
│   ├── runc-exec-root.patch
│   ├── offline-bundle.patch
│   ├── busybox-arm64
│   ├── SHA256SUMS、XZ.sha256
│   └── 使用说明、修复报告及验证日志
├── otn-system-20261110/
│   ├── otn-system-arm64-preview-20261110.tar.gz
│   ├── otn-system-arm64-preview-20261110.tar.gz.sha256
│   └── 构建大小及既有测试报告
├── README.md                 # 仓库总览及构建入口
├── build/                    # 源码、构建脚本、依赖和测试
├── docs/温度恢复测试补丁-20261111.md
├── test_north.py             # 部署后需应用的独立测试补丁
└── test_pmon.py              # 部署后需应用的独立测试补丁
```

引擎子目录还带有 .git 和 .gitattributes，它们是版本管理资料，不是设备运行依赖，无需上传到设备。未对其进行清理或修改。

### 2.3 归档检查结果及完整性边界

本次逐项核查结论：

- 引擎目录 SHA256SUMS 所列文件全部匹配，XZ.sha256 匹配。
- otn-system 外部摘要匹配；三个业务镜像与 WSL 原始 bundle 中的归档 SHA256 一致。
- 根目录 test_north.py、test_pmon.py 与 20261111 原测试补丁逐字节一致。
- 当前目录未包含独立 otn-linklab 运行包和源码覆盖层归档；system 包含其 core 运行内容，但不能据此认为完整可重建 C++ 源码已经全部归档。
- 原补丁的 SHA256SUMS 未随两个脚本复制；本次新增全局 ARCHIVE-SHA256SUMS.txt 记录当前交付文件和补丁摘要。
- 当前仓库保留引擎随包说明和修复报告；如发布额外的完整操作手册，应将其一并收录，不引用仓库外的个人文档路径。

主要部署归档保留原基线。本次另行收录了独立镜像与引擎重建输入；具体构建和运行验证见 docs/重建验证-20261116.md。归档一致性检查与本地编译不等于重新执行设备业务验收。

可在 WSL 中核对全局清单：

```sh
cd /mnt/d/isulad
sha256sum -c ARCHIVE-SHA256SUMS.txt
```

全局清单排除 .git 和清单自身。修改手册后应相应更新全局摘要；原引擎目录的独立清单保持原样。

### 2.4 与 SONiC-OTN 仓库匹配的源码路径

SONiC-OTN 源码统一以 `sonic-buildimage/` 为根目录。业务修改应以仓库中的实际模块为准：

| 业务源码/部署内容 | SONiC-OTN 仓库内路径 |
| --- | --- |
| swsscommon | sonic-buildimage/src/sonic-swss-common |
| OTSS、orchagent、configsyncd | sonic-buildimage/src/sonic-otss |
| otairedis、syncd、虚拟 OTAI | sonic-buildimage/src/sonic-otairedis |
| OTAI 定义 | sonic-buildimage/src/sonic-otairedis/OTAI |
| 原生平台守护进程 | sonic-buildimage/src/sonic-platform-daemons |
| 平台公共 API | sonic-buildimage/src/sonic-platform-common |
| 原生容器启动模板 | sonic-buildimage/files/build_templates/docker_image_ctl.j2 |

上述模块应从 `sonic-buildimage/src/` 按固定提交导出到 `D:\isulad\build\sources\`。例如 `sonic-buildimage/src/sonic-otss` 对应 `sources/sonic-otss`。构建副本与源码仓库不自动同步，更新必须显式进行并记录来源。

联调覆盖层的 management.py、协议适配器和 system.sh 应归入 `D:\isulad\build\otn-system\`，不应误写成 SONiC 上游原生路径。通用运行时适配组件在 SONiC 仓库中的目标位置为 `sonic-buildimage/src/sonic-container-runtime/`；必须使用含相应迁移提交的版本，不能假定任意上游分支都包含它。

### 2.5 仓库内可用说明

- [引擎使用说明](isulad-offline-arm64-fix-20261106/使用说明.md)。
- [引擎修复与验证报告](isulad-offline-arm64-fix-20261106/修复与验证报告.md)。
- [引擎交付 README](isulad-offline-arm64-fix-20261106/README.md)。
- [温度恢复测试补丁说明](docs/温度恢复测试补丁-20261111.md)。

联调预览包的详细 README 位于 `otn-system-20261110/otn-system-arm64-preview-20261110.tar.gz` 内，解包后为 `otn-system/README.md`。仓库地址尚未在本文指定，复现者应以实际发布的 GitHub 仓库与发布清单为准。

下文第 4～5 节的相对脚本路径均相对于 `D:\isulad\build\images\`；第 6 节引擎装配路径相对于 `D:\isulad\build\engine\`。这些章节说明构建原理；可执行入口统一为 build/rebuild.py，输入已收录。正式交付归档统一放在 `D:\isulad` 的版本目录。

## 3. 源码基线与依赖关系

### 3.1 原始独立镜像的版本记录

| 模块 | 原始锁定提交 | 记录位置 |
| --- | --- | --- |
| sonic-buildimage | a77538a37517b9a953e32191a77e83c6b5a11c14 | provenance.json |
| sonic-swss-common | 558ee25235235a470057cbd7d6bd28d86c2aa0f4 | provenance.json |
| sonic-otairedis | 779999e5e00c498abb0c07493bc0fcc05938e5a3 | locks/ot-source-lock.json |
| OTAI | f80d325c5e0206809c67e983ecac08e6055bf1f4 | locks/ot-source-lock.json |
| sonic-otss | 89ca0d7907d8440d47ad6b5eece872fcbe09d97c | locks/ot-source-lock.json |

这些是初始构建记录，不代表所有当前工作树仍与该提交完全一致。后续源码补丁和联调覆盖必须另记版本。provenance.json 中历史 NOT RUN 也不代表后续设备基础验收未进行，应以各阶段报告为准。

### 3.2 编译依赖

```text
sonic-swss-common
  └─ libswsscommon、Python 绑定、数据库工具
       ├─ database 基础镜像
       └─ OTAI / sonic-otairedis
            ├─ 元数据、libotairedis、libotaivs、syncd
            └─ sonic-otss
                 └─ orchagent、configsyncd、linecardmgrd 等
```

修改上层业务实现与修改公共 ABI 的重建范围不同。修改公共头文件、对象定义、枚举或序列化格式时，必须检查发送端、接收端、元数据和后端的一致性，不能仅替换一个可执行文件。

## 4. database 镜像构建流程

统一构建工作目录：`D:\isulad\build\images\`（WSL：`/mnt/d/isulad/build/images`）；此处是输入目录，编译时复制到 --work 下的 images 工作树。

| 文件 | 输入/作用 | 输出 |
| --- | --- | --- |
| Dockerfile.build | 创建 ARM64 swsscommon 编译环境 | sonic-arm64-swss-builder 镜像 |
| compile.sh | 读取 sources/sonic-swss-common，配置并编译选定目标 | target/rootfs |
| Dockerfile | Debian ARM64 基础层、Redis/库、target/rootfs、配置及来源信息 | database 运行镜像 |
| ../rebuild.py images | 串联全新编译环境/源码编译、运行镜像与导出 | --work/dist 中的归档及清单 |

历史基础镜像固定为 Debian Bookworm ARM64 digest：

```text
sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
```

Redis 和基础运行库通过 Debian 软件包安装；swsscommon 从 SONiC 源码编译。开发机为 x86_64 WSL，业务构建在 ARM64 用户态中借助 QEMU 执行，未复用 AMD64 deb/wheel。

compile.sh 主要配置项：

```sh
./configure --prefix=/usr --disable-static --disable-python2   --disable-yangmodules PYTHON=/usr/bin/python3 PYTHON3=/usr/bin/python3
```

主要输出：

```text
target/rootfs/usr/lib/aarch64-linux-gnu/libswsscommon.so*
target/rootfs/usr/lib/python3/dist-packages/swsscommon/
target/rootfs/usr/bin/sonic-db-cli
target/rootfs/usr/bin/swssloglevel
target/rootfs/usr/share/swss/*.lua
target/rootfs/var/run/redis/sonic-db/database_config.json
```

最终 Dockerfile 复制 target/rootfs，并设置 Redis 独立入口。复制原 docker-database 文件到 /usr/share/sonic/upstream 仅保留参考内容，不等于执行原 SONiC database 初始化流程。

原始构建输出（正式归档位于 D:\isulad 根目录）：

```text
sonic-database-standalone:arm64-20261022
bundle/docker-database-standalone-arm64.tar.gz
```

新增 YANG 依赖功能时，必须重新评估被禁用的构建项。当前版本只编译选定 swsscommon 目标，不保证上游新增程序自动进入镜像。

## 5. OTSS、syncd 镜像构建流程

| 文件 | 作用 |
| --- | --- |
| Dockerfile.ot-builder | 准备 ARM64 OT 编译环境，继承本次 swss 构建器 |
| compile-ot.sh | 依次构建 otairedis/OTAI/syncd 和 OTSS |
| ../rebuild.py images | 新建工作树、运行编译、暂存产物、装配与导出 |
| Dockerfile.syncd | 在本次 database 镜像上加入 ot-rootfs |
| Dockerfile.otss | 在本次 database 镜像上加入 ot-rootfs 和 otss-rootfs |

compile-ot.sh 先安装 swsscommon 头文件和已编译库，再配置 otairedis：

```text
--prefix=/usr --disable-static --with-otai=vs
```

其中 vs 表示虚拟 OTAI 后端，不是厂商硬件实现。Bookworm 工具链适配包括 doxygen 配置更新和生成 C 代码的特定警告处理，应随源码补丁保存，不能以关闭全部错误检查代替。

```text
target/ot-rootfs/
  usr/lib/libotaimeta.so*
  usr/lib/libotaimetadata.so*
  usr/lib/libotairedis.so*
  usr/lib/libotaivs.so*
  usr/bin/syncd
  usr/bin/syncd_request_shutdown

target/otss-rootfs/
  OTSS make install 生成的程序及运行文件
```

两个 Dockerfile 均继承 database 镜像，因此公共基础层可共享。原始标签为：

```text
sonic-otss-standalone:arm64-20261023
sonic-syncd-standalone:arm64-20261023
```

归档采用 docker save 格式并压缩，设备侧通过 isula load 导入。文件名包含 docker 不代表只能由 Docker 运行。

最初默认入口为 standalone-probe.py。帮助命令测试只能证明二进制能够执行到帮助分支，不能证明业务主循环和硬件操作有效。

## 6. 离线引擎包的构建和修复

### 6.1 原包装配

原包使用 openEuler 22.03-LTS-SP4 ARM64 私有用户态，装入官方 iSulad 2.1.5 RPM 与依赖。iSulad 本身未从源码重编译；设备操作系统未替换。

当前收录路径：

```text
D:\isulad\build\engine\rootfs-arm64.tar.xz  # 冻结 RPM 用户态依赖
D:\isulad\build\engine\overlay\           # 修复后脚本及配置
D:\isulad\build\engine\Dockerfile.userland # 升级依赖时的官方安装配方参考
D:\isulad\build\rebuild.py                  # 当前装配入口
```

默认流程直接使用冻结用户态输入；iSulad 及共享库不是从源码编译。依赖的来源、版本和更新边界见 `build/engine/DEPENDENCIES.md`。

### 6.2 修复版构建

```text
D:\isulad\build\sources\runc-v1.5.2.tar
D:\isulad\build\engine\runc-exec-root.patch
D:\isulad\build\engine\offline-bundle.patch  # 历史变更证据；装配使用 overlay 最终文件
D:\isulad\build\engine\Dockerfile.builder
D:\isulad\build\engine\build-runc.sh
```

`rebuild.py` 每次解包原始 runc、应用补丁并编译，不复制旧 runc-real。装配恢复模式和符号链接，替换设备测试默认镜像版本，生成新的 SHA256SUMS 和 tar.xz。

runc 基于 v1.5.2。build-runc.sh 生成静态 amd64 和 ARM64 程序，保留 seccomp，选择上游 Go pathrs 实现以避免额外 Rust 共享库依赖。最终 ARM64 文件放入：

```text
rootfs/usr/libexec/runc-real
```

rootfs/usr/bin/runc 是兼容适配器，而非同一份 ELF。它处理 create/run 的 --no-pivot 和特定 /dev/shm 参数；exec 根目录修复在 runc-real 内部。

修复包还整合：私有 cgroup 标准布局、cpu/cpuacct 合并挂载、OverlayFS 前置检查、诊断和测试工具兼容。内核 overlay.ko 来自目标部署系统配套发行版，不由业务镜像或用户态共享库提供。

维护 runc 时必须重新应用或重新评估补丁，不能仅替换为另一版上游二进制。软件包记录中的 RPM runc 版本也不等于实际运行的 runc-real 版本。

最终交付为 isulad-offline-arm64-fix-20261106-r1.tar.xz。xz 仅改变归档压缩方式，不改变运行架构和业务能力。

## 7. 联调增量包及实际执行路径

历史 linklab 包（本次 D:\isulad 未单独归档）从 sonic-buildimage 的业务修复源码复制覆盖层，源码归档内有 source-manifest.json、prepare/build/compile/assemble 等脚本。具体执行顺序应结合归档 README 和脚本输入核对，不能把它视为主仓库自动构建产物。

D:\isulad\otn-system-20261110 中的 system 预览包进一步加入 pmon、北向接口、Python 依赖和 ARM64 网关，复用原三种镜像启动五个角色。

| 角色 | 当前关键执行来源 |
| --- | --- |
| database | 镜像中的 redis-server，由挂载的 core/runtime.py 组织启动 |
| OTSS | 挂载到 /lab/bin 的 orchagent、configsyncd |
| syncd | 镜像中的 /usr/bin/syncd，结合挂载 /lab/bin 中的虚拟库 |
| pmon | 增量包中的平台 API、原生平台守护脚本及依赖 |
| north | 增量包中的管理/协议适配程序及依赖 |

core/runtime.py 设置 LD_LIBRARY_PATH，使 /lab/bin 优先参与库查找。因此：更新镜像内同名库不一定改变运行库；更新增量包也不意味着镜像内 syncd 同时更新。

发布前应记录实际进程 cmdline、/proc/PID/exe、映射的库路径及哈希。容器主进程可能是 Python 监督器，不能仅检查主进程名称判断业务程序版本。

当前增量包属于虚拟后端联调预览版，不包含完整原生板卡生命周期、所有对象/北向模型或生产恢复机制。现有联调脚本不自动执行 sonic-buildimage 的所有 systemd/FEATURE/容器模板。

## 8. 按修改模块确定重建范围

| 修改类型 | 最小重建/更新范围 | 扩大重建的条件 | 验证重点 |
| --- | --- | --- | --- |
| orchagent/configsyncd 业务实现 | OTSS 产物及实际使用它的镜像或增量包 | 引入新库、修改公共接口/模型 | 配置回执、GET、非法输入、对象隔离、新功能 |
| syncd 实现 | syncd 镜像或明确的新二进制覆盖方案 | OTAI/otairedis ABI 改变 | 操作消费、后端调用、PM、告警、退出恢复 |
| swsscommon | database 基础镜像及下游镜像 | ABI/Python 绑定变更时重编译消费者 | Python 导入、表读写、通知及业务闭环 |
| OTAI 头文件/对象/属性/枚举 | 元数据、otairedis、syncd、后端及 OTSS | 上游消费者新增或序列化变化 | 端到端创建/设置/读取/删除、非法值和版本一致性 |
| libotairedis | 对应公共库及相关镜像/增量 | 通信协议或 ABI 改变 | 请求结果关联、失败、超时、通知 |
| libotaivs/模拟输入 | 虚拟库和联调数据 | 修改 OTAI 接口时关联重建 | PM 注入、告警、属性存储与回读 |
| 厂商 OTAI/硬件功能 | 厂商后端、设备配置及相关镜像 | 驱动或内核接口要求变化 | 真实设备访问、硬件效果、错误及恢复 |
| Redis 配置/数据库映射 | 镜像配置或实际启动配置 | 多实例拆分、客户端硬编码变化 | 全部客户端连接、逻辑库、通知、重启 |
| Python 平台/北向功能 | 增量包及依赖 | Python ABI/本地扩展变化 | 平台采集、认证、协议、模型与新功能 |
| systemd/FEATURE/容器模板 | 对应 SONiC 部署管理产物 | 由自定义部署切换到原生管理链 | 启停顺序、所有权、异常恢复与幂等性 |
| iSulad/runc/启动适配 | 离线引擎包 | 引擎数据格式或内核要求变化 | smoke、全部业务回归、内核兼容及回滚 |
| 仅测试脚本 | 测试补丁包/联调包 | 测试要求暴露实际业务缺陷 | 不降低断言，重跑受影响测试 |

“只更新一个包”以接口不变为前提。修改设备创建参数、挂载、权限或配置模型，即使没有改 C++，仍属于行为变更。

## 9. 后续更新的标准流程

以下为维护流程要求，不表示当前已经存在一键完成全部步骤的流水线。

### 9.1 建立发布输入

1. 以 sonic-buildimage/src/ 中对应模块为业务源码起点，确认选定分支，记录主仓库和子模块提交；联调覆盖层和引擎补丁分别记录。
2. 将本地未提交修改形成可追溯补丁或提交，保存构建选项。
3. 明确新功能的输入、输出、依赖和验收用例。
4. 选择镜像交付或增量挂载交付，明确启动路径。
5. 固定基础镜像 digest、编译器、软件包版本和依赖来源。

只读核对示例，在 WSL 中执行：

```sh
# 先将 SONIC_SRC 设为已克隆的 sonic-buildimage 绝对路径
: "${SONIC_SRC:?请设置 sonic-buildimage 源码目录的绝对路径}"
git -C "$SONIC_SRC" rev-parse HEAD
git -C "$SONIC_SRC" status --short
git -C "$SONIC_SRC" submodule status --recursive
# 以下为已收录的来源记录
cd /mnt/d/isulad/build
cat locks/provenance.json
cat locks/ot-source-lock.json
```

部分来源可能是源码快照而非完整 Git 仓库，此时以来源 manifest 和文件摘要记录，不能伪填提交号。

### 9.2 干净构建

每个发布版本使用独立输出目录和版本标签；从干净输入构建，避免混入旧 target 或复制暂存目录残留。历史脚本中的固定路径、日期和容器名称需参数化后再用于新发布。

典型逻辑顺序：

```text
固定源码与补丁
→ 准备 ARM64 构建环境
→ swsscommon
→ database 运行基础
→ OTAI/otairedis/syncd
→ OTSS
→ 组装镜像或联调覆盖层
→ 导出归档并生成 manifest
```

当前统一入口为 `python3 build/rebuild.py all --release r1 --work /var/tmp/isulad-build-r1`。更新源码使用 `build/scripts/import-sonic.py`，审核后执行 `build/scripts/inputs.py refresh`。新入口不依赖历史 finish-* 脚本或旧编译容器，具体前置条件见 build/README.md。

### 9.3 产物审计

- 检查 ELF 为 AArch64，排除误混 AMD64 文件。
- 检查动态链接器、NEEDED 库、SONAME 和符号兼容。
- 对 Python 原生扩展核对 Python 版本及 ABI。
- 对最终归档实际解包验证，不只检查 target 中间目录。
- 核对入口、LD_LIBRARY_PATH、挂载路径与实际二进制来源。
- 生成许可证、软件来源、版本和哈希清单。
- 保存压缩归档、镜像 digest、文件哈希；标签不是不可变身份。

输入校验使用 build/scripts/inputs.py verify，产物校验使用本次 dist/SHA256SUMS；本地测试及其覆盖范围见 docs/重建验证-20261116.md。

### 9.4 分层回归

| 层次 | 验证内容 | 通过含义 |
| --- | --- | --- |
| 静态检查 | 脚本语法、架构、依赖、清单 | 产物结构和基础依赖正确 |
| 本地运行 | Docker/QEMU 或 ARM64 环境导入、创建、exec | 提前发现运行问题，不替代设备验收 |
| 设备基础 | smoke、database、ot | 引擎及基础组件可用；ot 帮助检查不等于业务功能通过 |
| 软件业务 | linklab/system 的配置、PM、告警等 | 覆盖对象范围内的虚拟后端业务闭环 |
| 新功能 | 专用正向、非法输入、边界和故障用例 | 新增行为达到明确要求 |
| 恢复 | 容器/进程/数据库重启、配置恢复 | 仅对实际执行场景形成恢复结论 |
| 硬件 | 厂商 OTAI、平台驱动、真实对象效果 | 真实设备功能；不能由虚拟测试推导 |

设备基础测试，在引擎目录执行：

```sh
sh control.sh check
sh control.sh start
sh control.sh status
sh test.sh smoke
sh test.sh load-sonic
sh test.sh database
sh test.sh ot
```

旧发布包中的测试使用原始标签；本次 rebuild.py 生成的新引擎包会将测试标签更新为 --release 对应版本。引擎与镜像分开构建时应使用同一个 release。system 预览包的固定标签不会自动更新。

system 预览版测试示例：

```sh
sh evidence/otn-system/system.sh start
sh evidence/otn-system/system.sh test
sh evidence/otn-system/system.sh cli show services
```

新装 20261110 归档应按配套说明应用 20261111 温度恢复测试补丁。system.sh 也固定了镜像标签；版本更新必须一并调整。长时间 PM 历史、完整平台生命周期、生产持久化等不在短测试通过范围内。

### 9.5 设备发布与回退

1. 保存旧版发布清单、镜像及配置，按需要备份业务持久数据。
2. 新版本使用独立目录和镜像标签；修改实际部署入口的版本引用。
3. 按依赖顺序停止受影响业务，避免运行时覆盖二进制和共享库。
4. 完整性验证后启动；检查实际进程与加载库，执行回归。
5. 失败时停止新版、恢复旧版本组合和兼容配置，再验证旧服务。

正式交付前将本次归档、来源清单、测试报告收集到 D:\isulad 下独立版本子目录，再从该工作目录上传。不要覆盖旧版已验收归档。

镜像回退不自动回退数据库 schema 或持久数据。数据格式变化须单独设计前后兼容或迁移恢复步骤。不得让两套引擎同时使用同一个 data 目录。

## 10. 必须整理的历史构建限制

| 现状 | 风险 | 维护改造要求 |
| --- | --- | --- |
| 标签、编译容器和目录固定日期 | 新旧版本混淆、重跑冲突 | 参数化 release id、tag、输出目录 |
| compile-ot.sh 使用 config.status 和完成标记 | 新源码可能复用旧配置/旧编译结果 | 干净构建或由输入摘要控制缓存失效 |
| package-ot.py 合并复制目录 | 已删除的旧文件残留 | 每版使用新暂存目录 |
| apt 依赖未全部锁版本 | 同源码重建结果变化 | 记录/固定依赖版本与软件源快照 |
| 初期使用 -O0 -g0 | 大小和性能不代表发布优化水平 | 明确发布编译参数，改变优化后回归 |
| 业务文件同时位于镜像和增量包 | 更新了未使用副本 | 发布清单列出实际执行/加载路径 |
| 选取部分编译目标且禁用 YANG 模块 | 新功能未被装配 | 显式更新功能与文件清单 |
| 原引擎 packager 指向旧暂存目录 | 重新打包可能丢失运行时修复 | 为修复版提供独立、版本化装配入口 |
| system.sh 与原 SONiC 管理层并行存在 | 模板更新未作用于实际部署 | 明确部署控制方式，分别验收 |

本次已通过统一入口解决独立构建的版本参数化、全新工作树、装配残留和修复版入口问题。APT 软件源快照锁定、发布优化、联调系统完整源码整理仍未完成；重建成功不等于逐字节复现或完整系统交付。

## 11. 发布清单模板

每次发布建议附 release-manifest.yaml；以下是待填写结构，不是现有包的已验证配置。

```yaml
release_id: REPLACE_WITH_RELEASE_ID
source:
  sonic_buildimage: REPLACE_WITH_COMMIT
  swsscommon: REPLACE_WITH_COMMIT
  otss: REPLACE_WITH_COMMIT
  otairedis: REPLACE_WITH_COMMIT
  otai: REPLACE_WITH_COMMIT
  patches: []
build:
  target: linux/arm64
  base_image_digest: REPLACE_WITH_DIGEST
  compiler: REPLACE_WITH_VERSION
  package_lock: REPLACE_WITH_LOCK_FILE
engine:
  archive: REPLACE_WITH_ARCHIVE
  sha256: REPLACE_WITH_SHA256
  isulad: REPLACE_WITH_VERSION
  runc_real_sha256: REPLACE_WITH_SHA256
images:
  database: {tag: REPLACE, digest: REPLACE, archive_sha256: REPLACE}
  otss: {tag: REPLACE, digest: REPLACE, archive_sha256: REPLACE}
  syncd: {tag: REPLACE, digest: REPLACE, archive_sha256: REPLACE}
overlay:
  package: REPLACE_WITH_PACKAGE_OR_NONE
  sha256: REPLACE_WITH_SHA256_OR_NONE
  actual_orchagent_path: REPLACE
  actual_syncd_path: REPLACE
  actual_otai_library: REPLACE
config:
  schema_version: REPLACE
  startup_backup: REPLACE
tests:
  local_report: REPLACE
  device_report: REPLACE
  new_feature_cases: []
  unverified_scope: []
rollback:
  previous_release: REPLACE
  data_compatibility: REPLACE
```

验收记录至少列出设备内核/架构、引擎和 runtime 版本、实际镜像身份、增量包哈希、测试命令、退出码、报告路径及失败处理。不得将旧证据目录中的 PASS 作为新版本通过依据。

## 12. 常见更新示例

### 示例 A：增加一个放大器配置字段

先检查该字段是否已有 OTAI 定义。若已有，修改 OTSS 配置处理并确认北向允许该字段；重建实际使用的 orchagent/configsyncd 或相关组件。若需要新增 OTAI 定义，则关联重建元数据、otairedis、syncd、后端和 OTSS。

验证配置进入 CONFIG_DB，业务回执匹配 operation-id，OTAI GET 返回预期值，非法值被拒绝，其他对象不受影响，重启后的配置行为符合要求。只检查 Redis 中出现新字段不算完成。

### 示例 B：修复 syncd 性能采集

更新 sonic-otairedis 中相关实现，重建 syncd。当前 system 包仍运行镜像内 /usr/bin/syncd，因此仅更新 /lab/bin 的库不一定包含此修复，应发布新 syncd 镜像或明确更改覆盖/入口方案。

验证至少两个不同后端输入、COUNTERS_DB 更新、失效和恢复；涉及窗口统计则增加完整窗口测试，不以 instant 值变化代替历史验收。

### 示例 C：增加 Python 北向接口

修改增量包的协议/management 代码，锁定新增 Python 依赖及原生扩展架构。验证认证失败、合法请求、非法模型、业务回执、超时结果未知及保存策略。若修改只在宿主 sonic-buildimage 的管理栈中，而实际运行仍是增量适配器，则必须显式移植，不能假定自动生效。

### 示例 D：升级 runc 或迁移新设备

单独更新引擎包，重新评估 exec-root 补丁与 --no-pivot。先验证 root/namespace 一致性、cgroup、overlay 和 seccomp，再回归业务链。厂商 overlay.ko 必须与新设备内核匹配，不随用户态引擎升级任意替换。

## 13. 维护完成条件

一次更新只有在以下条件满足后才形成可交付版本：源码和补丁可追溯，构建输入明确，最终产物可核验，实际执行文件与版本一致，新功能和原功能回归通过，目标设备结果有记录，未验证范围与回退方式清楚。

原 SONiC 源码中的新功能不会自动进入当前独立构建或增量包；将其纳入编译、依赖、配置、入口、部署和测试全过程，才构成一次完整迁移。

## 14. 当前归档的设备安装对应关系

| D:\isulad 中的文件 | 设备上的用途/位置 |
| --- | --- |
| isulad-offline-arm64-fix-20261106 中的 tar.xz、BusyBox、XZ.sha256 | 上传至设备安装暂存目录，校验后解压为 ENGINE |
| 根目录三个 docker-*-standalone-arm64.tar.gz | 上传到 ENGINE/inbox，由 test.sh load-sonic 导入 |
| otn-system-20261110 中的 tar.gz 与摘要 | 校验并解压到 ENGINE/evidence，得到 otn-system |
| 根目录 test_north.py、test_pmon.py | 根据 docs/温度恢复测试补丁-20261111.md 覆盖 ENGINE/evidence/otn-system 中相应测试文件；保留原文件备份 |
| 各 json 报告和修复验证日志 | 既有证据，不能当作本次设备执行结果 |

docs/温度恢复测试补丁-20261111.md 中的补丁安装说明仍适用。测试补丁没有重新打入 20261110 压缩包，新部署需单独应用。软件包安装和源码更新属于不同流程，不能把归档目录中的二进制或旧测试报告当作代码维护入口。
