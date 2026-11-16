# iSulad ARM64 构建与离线部署

本分支基于 `otn-dev`，保留原 SONiC-OTN 源码。离线部署包、三个独立镜像的构建输入和文档位于 [isulad/](isulad/)。

- [仓库内容与部署入口](isulad/README.md)
- [源码重建及更新说明](isulad/build/README.md)
- [构建与迁移维护手册](isulad/ARM64_SONiC-OTN构建迁移与更新维护手册-20261116.md)
- [本地重建验证报告](isulad/docs/重建验证-20261116.md)

```sh
git clone --branch isulad --single-branch https://github.com/SONIC-OTN-G/sonic-buildimage.git sonic-buildimage
cd sonic-buildimage/isulad
# 完成构建说明中的开发机、Docker 和 ARM64 仿真环境准备后执行
python3 build/rebuild.py all --release r1 --work /var/tmp/isulad-build-r1
```

包内文档中的 `D:\isulad` 指本分支 `isulad/` 子目录的本地部署工作副本；原 SONiC 源码路径以 `sonic-buildimage/` 为根目录。首次按附带快照重建不需要另外导入 SONiC 源码；后续业务开发可按说明从主仓库模块导入新版本。

新开发机准备编译依赖需要网络或已有离线缓存。iSulad 本体使用官方 RPM 用户态，修复版 runc 和业务组件从源码编译。本地基础回归不替代目标设备验收或完整业务链验证。
