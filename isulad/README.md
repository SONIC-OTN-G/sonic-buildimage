# SONiC-OTN ARM64 iSulad 离线部署与构建仓库

仓库工作目录约定为 `D:\isulad`；WSL 中为 `/mnt/d/isulad`。SONiC 源码根目录统一称为 `sonic-buildimage/`。既有归档用于部署；`build/` 提供重建离线引擎和 database、OTSS、syncd 三个独立镜像所需的源码快照、补丁、构建入口及依赖记录。

- [从源码重建与更新说明](build/README.md)
- [构建、迁移与维护手册](ARM64_SONiC-OTN构建迁移与更新维护手册-20261116.md)
- [引擎安装说明](isulad-offline-arm64-fix-20261106/使用说明.md)
- [本次重建验证报告](docs/重建验证-20261116.md)
- [原温度恢复测试补丁说明](docs/温度恢复测试补丁-20261111.md)

已发布归档保持原样。新产物先在 Linux 独立工作目录生成，完成本地与设备验证后，再归档到 `D:\isulad` 的新版本目录。不要覆盖旧版已验收包。

## 构建范围

三个独立镜像的 SONiC 组件从仓库内源码编译。Redis 和 Debian 运行库通过软件包安装。离线引擎从冻结的官方 openEuler/iSulad 用户态依赖集合重新装配，并从源码编译带 exec-root 修复的 runc；不是从源码编译 iSulad 及所有第三方库。

OTSS、syncd 默认入口是独立检查进程，保留业务可执行文件及虚拟 OTAI 库。这条构建路线不自动完成完整 SONiC-OTN 编排、厂商硬件适配或 `otn-system` 预览包的源码重建。

详细命令、依赖准备、验证范围及源码更新方式见构建说明。


## 上传为普通 Git 仓库

原引擎交付目录包含嵌套 Git 元数据。使用以下方式创建发布副本，保留所有交付文件和构建输入，但不携带任何旧 `.git`。原目录不受影响：

```sh
cd /mnt/d/isulad
python3 prepare-git-export.py --output /var/tmp/isulad-github-export
cd /var/tmp/isulad-github-export
python3 build/scripts/inputs.py verify
git init
git add .
# 审核暂存文件，再配置自己的 GitHub remote、提交并推送
```

导出目标必须是仓库外尚不存在的目录。新增及既有归档均低于 100 MB，无需 Git LFS；构建输出和编译器缓存不应提交。
