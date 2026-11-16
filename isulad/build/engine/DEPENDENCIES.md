# 离线引擎依赖来源

默认构建输入 `rootfs-arm64.tar.xz` 是冻结的 ARM64 openEuler 用户态及 RPM 依赖集合，不是最终离线安装包。完整性由 `../SOURCE-SHA256SUMS` 覆盖。它包含 iSulad 2.1.5-10.oe2203sp4、glibc、libseccomp、libwebsockets 等依赖；完整 RPM 版本见 `overlay/VERSIONS.txt`。包内许可证见 `overlay/LICENSES` 及 rootfs 中的软件包版权文件。

iSulad 没有在本项目中修改或从源码重新编译。重建离线交付件的实际流程是：解开冻结用户态 → 应用仓库内兼容脚本及配置 → 从 vendored runc 源码应用补丁并编译 → 安装 runc-real → 重新生成清单并打包。这里的“重建”不意味着所有第三方依赖从源码 bootstrap。

`Dockerfile.userland` 记录原官方 RPM 用户态的安装方式，供将来升级依赖时参考；软件源为 https://repo.openeuler.org/openEuler-22.03-LTS-SP4/OS/aarch64/ 。该刷新路线依赖源站保留旧版本，未承诺重建出逐字节相同的 rootfs，也未作为本次已验证路径。更新用户态需重新审计 RPM 清单、动态库、补丁适配和设备运行结果，不能直接覆盖冻结输入并跳过回归。

`overlay/bin/busybox` 是设备端独立启动工具；`arm64-smoke.tar.gz` 是既有基础测试镜像。它们是随仓库固定提供的二进制依赖，不是业务源码的编译结果。其摘要纳入输入清单，版权说明随原包保留。引擎构建不下载设备内核模块；OverlayFS、cgroup 和对应的厂商驱动仍由设备内核提供。

runc 源码在 `../sources/runc-v1.5.2.tar`，保留上游许可证和 vendor Go 依赖。构建 Go 镜像固定 digest；APT 依赖见 Dockerfile.builder，原编译环境版本见 `../locks/runc-builder-packages.tsv`。新建编译器环境需要网络，编译 runc 本身使用 `--network none`。
