# ARM64 iSulad 独立交付目录（无需 Git LFS）

推荐安装包：isulad-offline-arm64-fix-20261106-r1.tar.xz，68,560,564 字节（68.56 MB）。
此目录所有文件均小于 100 MB；安装包、源码包和 BusyBox 已明确设置不使用 LFS。
xz 仅更改压缩格式，展开后的 tar 数据与通过 ARM64/Linux 5.10 仿真测试的原包完全一致。
不需要旧版本、分卷或历史里的 RAR。

## 文件用途

- isulad-offline-arm64-fix-20261106-r1.tar.xz：完整引擎、所需库、工具和 smoke 镜像。
- busybox-arm64：设备解压及校验工具。
- XZ.sha256：仅校验安装包。
- SHA256SUMS：校验本目录所有交付文件。
- 使用说明.md：完整部署、业务测试和回滚步骤。
- 修复与验证报告.md、验证日志-20261106.tar.gz：修复范围和实际测试证据。
- isulad-exec-fix-source-20261106.tar.gz、两个 .patch：源码、构建脚本与带注释的修改。
- .gitattributes：务必一起提交，明确禁止这些文件走 LFS。

三个业务镜像是另外的交付文件，继续使用此前的 tar.gz，放入解压包 inbox/。

## 设备解压

将 xz 安装包、busybox-arm64、XZ.sha256 复制到新目录，先停止仍运行的旧引擎，再执行：

```sh
chmod 755 busybox-arm64
./busybox-arm64 sha256sum -c XZ.sha256
./busybox-arm64 xz -dc isulad-offline-arm64-fix-20261106-r1.tar.xz | ./busybox-arm64 tar -xf -
cd isulad-offline-arm64
sh verify.sh
sh control.sh start
sh test.sh smoke
```

设备的 devices、OverlayFS 等环境仍须具备。需要完整流程时看使用说明。

## 自行提交

在 D:\sonic-isulad 仓库根目录执行（清理结果尚未提交）：

```sh
git add -A -- .gitattributes isulad-offline-arm64-fix-20261106
git diff --cached --stat
git commit -m "Deliver standalone ARM64 iSulad fix without LFS"
git push origin main
```

要同时提交旧 gzip 文件的删除，不能只提交新 xz 而留下旧 LFS 路径。
清理时没有重写远端；旧文件和两条未推送提交已备份到：
D:\sonic-kvm-doc\gitcode-cleanup-backup-20261106
本地 main 回到最后确认的远端基线，文件改动留在工作区供你提交。
纯 LFS pre-push 钩子已备份并改名停用；没有修改全局 Git 设置。
历史中的 LFS 对象仍可能在 clone 时被旧客户端尝试下载；新交付文件本身不依赖这些对象。
