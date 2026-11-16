# 离线引擎与三个 ARM64 镜像的重建说明

## 1. 输入、输出与范围

仓库根目录为 `D:\isulad`（WSL `/mnt/d/isulad`）。本目录提供可执行构建入口 `rebuild.py`，不依赖旧编译容器中的 `/work`、旧 `target/` 或开发者个人路径。每次构建要求新的 Linux 工作目录。

| 仓库路径 | 内容 | 用途 |
| --- | --- | --- |
| build/sources/sonic-swss-common | 完整模块源码快照 | 编译公共库、Python 绑定和数据库工具 |
| build/sources/sonic-otairedis | 模块源码及 OTAI 子模块快照 | 编译元数据、otairedis、虚拟后端和 syncd |
| build/sources/sonic-otss | 模块源码快照 | 编译 orchagent、configsyncd 等 |
| build/sources/docker-database | 原 database 容器定义参考文件 | 装入参考目录，不执行原生 SONiC 启动流程 |
| build/sources/runc-v1.5.2.tar | 原始 runc 源码、Go vendor 和许可证 | 每次解包后应用补丁并编译 |
| build/images | Dockerfile、编译脚本和独立入口 | 组装三个 ARM64 镜像 |
| build/engine/rootfs-arm64.tar.xz | 冻结的官方 RPM 用户态依赖 | 离线引擎装配基底，约 71 MB |
| build/engine/overlay | 修复后的控制脚本、配置、测试和兼容文件 | 在基底上应用设备适配 |
| build/engine/runc-exec-root.patch | exec 根目录修复 | 应用于 runc 源码 |
| build/engine/overlay-modes.json、overlay-links.json | POSIX 模式和链接记录 | 还原 Windows 检出可能丢失的元数据 |
| build/engine/arm64-smoke.tar.gz | 固定的基础测试镜像 | 随引擎交付 |
| build/locks | 原始来源、编译环境身份、软件包版本 | 审计依赖及升级差异 |
| build/tests | database、OT 基础回归脚本 | 本地和设备测试参考 |
| build/SOURCE-SHA256SUMS | 所有构建文件和输入摘要 | 构建前强制校验 |

原始源码是无 `.git` 的快照，来源提交见 `sources/SOURCE-ORIGIN.json`、`locks/provenance.json` 和 `locks/ot-source-lock.json`；文件摘要才是本次快照内容的直接身份。上游 `.gitignore` 重命名为 `UPSTREAM.gitignore`，防止外层仓库提交时遗漏快照文件；业务代码未因此修改。没有把当前本地源码树擅自宣称为上游提交的逐字节副本。

离线指设备部署不需在线安装依赖。**在全新开发机上建立编译环境仍需网络**：拉取 Docker 基础镜像并安装 Debian 构建/运行依赖。已提供 Dockerfile 和软件包版本清单，但没有附带多 GB 的编译器镜像或完整 APT 镜像站。当前 APT 版本清单用于审计，未强制绑定历史快照源；因此可按流程重建，不承诺逐字节复现原历史镜像。外部镜像源不可达时需提供可访问的等价镜像/离线缓存。

## 2. 开发机准备

使用 Linux 或 WSL2，安装 Docker Engine（可访问 Linux daemon）、Python 3.12 及以上、Git、tar、xz。引擎用户态解包需要保留 uid/gid/模式，建议在 Linux root 的构建环境执行。目标设备无需安装这些构建工具。

在 x86_64 开发机上，需要已注册 ARM64 QEMU binfmt；Docker Desktop 或已有仿真开发环境通常已提供。先执行：

```sh
docker run --rm --platform linux/arm64 publicmirror.azurecr.io/debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 dpkg --print-architecture
# 预期 arm64；exec format error 表示仿真执行环境未就绪
```

本项目没有自动修改开发机 binfmt。按开发机发行版安装 QEMU user/binfmt 支持，或改用 ARM64 开发机。runc 的默认构建器采用 amd64→arm64 交叉编译方法，已验证主机为 x86_64；ARM64 主机使用时需调整 `build-runc.sh` 中 amd64 验证产物步骤。

源码放在 `D:\isulad`，构建临时目录放在 Linux 文件系统，例如 `/var/tmp/isulad-build-r1`，不要在 `/mnt/d` 上编译。构建涉及编译器、Docker 层和临时归档，开发机空间需求显著大于最终设备交付件。

## 3. 从仓库输入重建

在 WSL/Linux 中执行。`--work` 必须不存在，失败重试使用新目录；不会复用旧编译结果。

```sh
cd /mnt/d/isulad
python3 build/scripts/inputs.py verify
python3 build/rebuild.py all --release r1 --work /var/tmp/isulad-build-r1
```

可分别构建：

```sh
python3 build/rebuild.py images --release r2 --work /var/tmp/isulad-images-r2
python3 build/rebuild.py engine --release r2 --work /var/tmp/isulad-engine-r2
```

两次命令采用相同 release 时，重建引擎中的业务测试默认选择该 release 的镜像；只重建引擎时也必须提供对应标签的镜像，或显式修改测试配置。镜像标签为 `sonic-database-standalone:arm64-r1`、`sonic-otss-standalone:arm64-r1`、`sonic-syncd-standalone:arm64-r1`。

构建顺序：swsscommon → OTAI/otairedis/syncd → OTSS → database 基础镜像 → OTSS/syncd 镜像 → docker save 压缩导出；引擎部分解包 runc 源码 → 打补丁 → 静态编译 → 装配用户态和兼容脚本 → 更新测试标签 → 清单 → tar.xz。

输出位于 `--work` 下的 `dist/`：

```text
docker-database-standalone-arm64.tar.gz
docker-otss-standalone-arm64.tar.gz
docker-syncd-standalone-arm64.tar.gz
isulad-offline-arm64-r1.tar.xz
release.json
SHA256SUMS
```

单独目标只输出对应产物。`release.json` 记录输入清单摘要、编译器选择及镜像 ID；测试结果另外保存，不把 build complete 当作运行验收。

若已有明确版本的编译/运行依赖镜像，可指定 `--ot-builder TAG --runc-builder TAG --runtime-base TAG`。这些选项只跳过依赖环境准备，业务模块与 runc 仍重新编译。标签应由发布者提供并核对 `docker image inspect` 的 ID，不能依赖本机碰巧存在的同名镜像。默认命令不要求已有历史命名的编译器镜像。

## 4. 源码更新

正常修改在 `sonic-buildimage/` 进行并提交。初始化子模块后导入：

```sh
# SONIC_SRC 设为 sonic-buildimage 的实际绝对路径
: "${SONIC_SRC:?请设置 sonic-buildimage 源码根目录}"
cd /mnt/d/isulad
python3 build/scripts/import-sonic.py --sonic-src "$SONIC_SRC" --replace-snapshots
# 审核 Git diff、来源版本和构建选项后更新摘要
python3 build/scripts/inputs.py refresh
python3 build/scripts/inputs.py verify
python3 build/rebuild.py all --release r2 --work /var/tmp/isulad-build-r2
```

导入脚本拒绝未提交修改，替换四个快照目录，并单独展开 OTAI 子模块。它不导入构建输出，也不自动适配新 API。新增其他嵌套子模块、依赖、YANG 功能或编译目标时，需同步扩展导入与编译脚本。当前构建禁用 YANG 模块，使用虚拟 OTAI，并保留 `-O0 -g0` 历史选项。

直接修订仓库中的设备适配脚本、runc 补丁或源码快照后，同样先审核差异，再执行 `inputs.py refresh`。校验失败不是应当绕过的异常；它用于提示输入已改变。引擎源代码修复不要求重编译三种镜像；公共 OTAI/swsscommon ABI 改动则应关联重建相关消费者。

## 5. 验证与发布

构建后先核对 `dist/SHA256SUMS`。本地检查三种新镜像的 ARM64 身份、库导入、Redis/swsscommon 读写、OT 实际二进制帮助分支、exec 和 stop/start；本地回归入口如下（x86_64 仿真需显式启用标记）：

```sh
python3 build/tests/verify-images.py --release r1 --output /var/tmp/isulad-test-r1 --allow-emulation
```

原生 ARM64 测试省略 `--allow-emulation`。结果见输出目录 `report.json`、`test.log` 和各容器的日志；测试清理自己创建的容器，保留报告。详细覆盖范围见本次验证记录。

设备部署应使用新目录，按引擎说明解包后将本次三个 `.tar.gz` 放入 `inbox`，无需先把镜像解成 rootfs：

```sh
sh verify.sh
sh control.sh check
sh control.sh start
sh control.sh status
sh test.sh smoke
sh test.sh load-sonic
sh test.sh database
sh test.sh ot
```

新包测试选择本次 release 标签；不能将旧目录中的 PASS 或旧镜像测试报告作为新版验收。设备仍须具备适配的 cgroup/OverlayFS、权限和空间。业务库在镜像内，引擎依赖库在离线用户态内，二者不能随意互换。

OT 默认探针仅验证库装载、帮助命令和容器基础行为，不证明 CONFIG_DB→OTSS→ASIC_DB→syncd→硬件完整链路。本次源码收录范围不包括 `otn-system` 预览包的全部联调开发源码。发布完整业务系统时仍需独立整理这条路线并验证。

## 6. 依赖与许可证

业务编译器和运行库清单见 `locks/builder-packages.tsv`、`runtime-packages.tsv`；runc 见 `locks/runc-builder-packages.tsv`。基础镜像 digest 在各 Dockerfile，原编译器 image ID 在 `locks/build-environments.json`。引擎依赖及升级方法见 [engine/DEPENDENCIES.md](engine/DEPENDENCIES.md)。源码保留各模块许可证，离线用户态保留原包许可证资料。

外层仓库使用普通 Git，新增文件均须小于 GitHub 单文件限制；不要启用 `*.tar.*` 的全局 LFS 规则。既有引擎交付目录包含嵌套 `.git`；外层 `git add` 前应在发布工作副本中排除该元数据，避免将其作为子模块/gitlink 提交。不要直接递归删除已验证交付目录。构建脚本以 LF 保存；仓库 `.gitattributes` 对 build 输入禁用检出时的换行转换，保证跨系统的摘要一致，并将 tar/xz/gz 作为二进制处理。


## 7. runc 修复回归

在 x86_64 Linux 的 root 构建环境中，安装原生静态 BusyBox 后，可使用本次工作目录生成的 amd64 runc 验证同一份补丁：

```sh
python3 build/tests/runc-exec-regression.py /var/tmp/isulad-build-r1/engine/bin/runc-fixed.amd64 /bin/busybox
```

测试在新 mount namespace 中构造外层 chroot 和容器 rootfs，检查 exec 与主进程的根目录一致性、非 root 用户、cwd、seccomp、文件描述符、停止后 exec 拒绝及重建。它是补丁逻辑的本地回归，不是目标 ARM64 内核兼容验收；目标仍需运行 smoke/database/ot。
