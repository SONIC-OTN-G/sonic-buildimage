# PMON 硬件数据采集与设置(初学者版)

## §0 新手地图(先读这页)

### 容器各管一摊

```
┌───────────── VM(QEMU,ssh localhost:2222)─────────────┐
│                                                        │
│  pmon 容器              database 容器        syncd 容器 │
│  硬件管家                redis 群(分库)      业务采集   │
│  ├ psud(电源)          ├ db1 ASIC_DB       └ syncd 进程 │
│  ├ thermalctld(温度)   ├ db2 COUNTERS_DB      (C++)     │
│  └ stormond            ├ db5 FLEX_COUNTER_DB              │
│                        ├ db10 GB_COUNTERS_DB              │
│                        └ db15 HISTORY_DB(桩旧账)          │
└────────────────────────────────────────────────────────┘
```

本篇只走 pmon 容器这条线;syncd 容器的业务采集(场景三)见姊妹篇《45-syncd数据采集》。

### 两个场景,两条数据链

| 场景 | 一句话 | 数据链 | 看点 |
|:--|:--|:--|:--|
| 一 | 硬件读数自动进库 | daemon 轮询 → STATE_DB | timestamp 3 秒一跳 |
| 二 | 命令改硬件 → 框架自己反应 | thermalctl → 硬件 → thermalctld → STATE_DB 告警 | ≤3 秒告警翻转 |

### 名词小词典(够用就好)

| 名词 | 白话 |
|:--|:--|
| STATE_DB | 「现在硬件什么状态」的展示库(温度/电源/风扇/告警) |
| daemon | 常驻后台进程(pmon 容器里由 supervisor 管着) |
| thermalctl | 写硬件门限的运维命令(命令 → sonic_platform API → 硬件) |

### 登入与演示前快检(30 秒)

```bash
ssh -p 2222 admin@localhost        # 密码 YourPaSsWoRd(VM 内 sudo 同)
```

```bash
sudo docker ps --format "{{.Names}}: {{.Status}}" | head -5
sudo docker exec pmon supervisorctl status
sonic-db-cli STATE_DB HGET 'TEMPERATURE_INFO|System Board' temperature
```

| 预期 | 说明 |
|:--|:--|
| database/swss/syncd/pmon … Up | 容器都在 |
| psud/thermalctld/stormond 都 RUNNING | 硬件管家三件套活着 |
| `'37.0'` | PMON 数据在流(场景一能用) |

### 一键验证脚本(内建于 VM `/usr/local/bin`)

两场景各一支脚本,ssh 进 VM 后直接跑:命令回顾 + 预期输出 + ✓/✗ 判定 + PASS/FAIL 总结:

| 脚本 | 内容 | 时长 |
|:--|:--|:--|
| `s1_pmon_poll.sh` | 场景一全流程;自愈 psud FATAL / thermalctld 写循环冻结(重启后已知漂移) | ~30 秒 |
| `s2_thermalctl.sh` | 场景二全流程;门限自动还原,中途失败也不留 35 | ~40 秒 |

脚本源码在 sonic-utilities fork `scripts/`(构建时经 sonic-utilities deb 落到镜像 `/usr/local/bin`,thermalctl 同款通道)。

---

## §1 场景一:PMON 轮询(daemon → STATE_DB)

### 1.1 这是什么

PMON 容器里的 daemon 定时读硬件(仿真环境=HalPlatformApiServer 模拟件),把读数写进 STATE_DB。**硬件没变,值就不动;但 timestamp 每轮都刷新——timestamp 跳,就是轮询管道活着的证据。**

```
psud ──3 秒──▶ PSU_INFO│PSU0          (电压/电流/功率)
thermalctld ──3 秒──▶ TEMPERATURE_INFO|System Board   (温度+门限+告警+timestamp)
```

### 1.2 演示步骤(3 分钟)

```bash
# ① 谁在跑
sudo docker exec pmon supervisorctl status          # 预期:psud/thermalctld/stormond RUNNING

# ② 看轮询成果
sonic-db-cli STATE_DB HGETALL 'TEMPERATURE_INFO|System Board'
#    预期:temperature=37.0, high_threshold=71.0, warning_status=False, timestamp=<当前时间>

# ③ 证明「在轮询」
watch -n 1 "date +%T; sonic-db-cli STATE_DB HGET 'TEMPERATURE_INFO|System Board' timestamp"
#    预期:timestamp 每 3 秒跳。Ctrl+C 退出。
```

**讲点**:值不动=硬件没变化;timestamp 3 秒跳=daemon 本轮重写了整行。想让「值」动?用命令动硬件——就是场景二。

### 1.3 相关改动

| 文件 | 改动 | 原因 |
|:--|:--|:--|
| pmon 容器 `/etc/supervisor/conf.d/*.conf`(thermalctld 段) | 启动参数加 `--thermal-monitor-update-interval 3`(原厂预设 60 秒) | 60 秒一跳现场等不起;3 秒节奏肉眼可见。容器层改动,重开机后要重套(§3) |

代码零改动——场景一纯框架内建能力。

---

## §2 场景二:PMON 设硬件数值(thermalctl 改门限 → 越限告警)

### 2.1 这是什么

`thermalctl` 是照 `psuutil`/`fanshow` 家族模式写的运维命令(命令 → sonic_platform 平台 API → 硬件)。这个家族原本全是唯读,thermalctl 补上「写入」:把高温门限从 71°C 降到 35°C(低于现值 37°C,制造越限)→ thermalctld 3 秒内轮询到 → STATE_DB 告警翻 True → 还原恢复。

```
thermalctl set-high-threshold ──▶ 模拟硬件(门限=35)
        thermalctld 3 秒轮询 ──▶ 37 > 35 越限 ──▶ warning_status=True
```

**诚实定位**(现场被问到再讲):这是平台层运维命令,不是 CONFIG_DB 管理面下发;风扇转速不会变(无策略引擎,thermalctld 只跑监控告警)。

### 2.2 演示步骤(3 分钟)

```bash
# ① 记基准
sudo thermalctl show                 # 预期:Board = 37 / 71 / 10.4 / False

# ② 命令下发:71 → 35
sudo thermalctl set-high-threshold "System Board" 35      # 预期:-> 35.0: OK

# ③ 看反应(≤3 秒)
watch -n 1 "sudo thermalctl show"    # 预期:HighThr 变 35、Warning 变 True。Ctrl+C。

# ④ 还原
sudo thermalctl set-high-threshold "System Board" 71
sudo thermalctl show                 # 预期:Warning 回 False
```

### 2.3 相关改动

| 文件 | 改动 | 原因 |
|:--|:--|:--|
| `src/sonic-utilities/scripts/thermalctl`(fork,gitlink b4802d16) | **新 CLI**:show / set-high-threshold / set-low-threshold | 现有 psuutil/fanshow 家族全唯读,演示需要「命令写硬件」;照家族正统模式写,即「加命令」参考范例 |
| `src/sonic-utilities/setup.py` | scripts 清单注册一行 | 重构 sonic-utilities deb 后命令自动进 image |
| `src/sonic-utilities/doc/Command-Reference.md` | thermalctl 用法 + 加命令三步骤说明 | 留档:scripts/ → setup.py → 文档,就是 SONiC 加命令的正统流程 |
| 主 repo `.gitmodules` + gitlink | sonic-utilities 的 url 改指 hoholee8991 fork、钉 b4802d16(commit 404bede33) | thermalctl 只存在于我们的 fork;地址簿和钉单必须成对换 |
| VM host `/usr/local/bin/thermalctl` | repo 版脚本拷贝上机 | host 直接可敲,演示不用进容器;删档即还原 |
| `docs/06-数据采集/attachments/thermalctl` | 脚本仓内参考快照 | 想看代码不用切 submodule,打开这份就是 |

---

## §3 重开机后重套(照抄)

### 3.1 存活矩阵

| 资产 | VM 重启 | 容器重启 | 容器重建 | 备注 |
|:--|:--|:--|:--|:--|
| thermalctl(host `/usr/local/bin`) | ✅ | — | — | root 档案系统持久 |
| thermalctld 3 秒参数 | ❌ | ✅ | ❌ | pmon conf 开机被重渲染,每次 VM 重启后重套 |

场景三相关资产(syncd 三件套/二进制/种子)的重套在姊妹篇 §4。

### 3.2 重套命令(只列必做)

```bash
# thermalctld 3 秒参数(必须 reread+update;restart 不重读 conf!)
sudo docker exec pmon sh -c "CONF=\$(grep -l 'program:thermalctld' /etc/supervisor/conf.d/*.conf); \
  grep -q 'update-interval 3' \$CONF || \
  sed -i 's|^command=python3 /usr/local/bin/thermalctld\$|& --thermal-monitor-update-interval 3|' \$CONF"
sudo docker exec pmon supervisorctl reread
sudo docker exec pmon supervisorctl update
sudo docker exec pmon supervisorctl start thermalctld   # update 后 autostart=false 不自启

# 验证:场景一 watch timestamp 3 秒跳;场景二跑 s2_thermalctl.sh
```

---

## §4 卡点排解

| 症状 | 处理 |
|:--|:--|
| ssh 连不上 | WSL 查 `pgrep -a qemu-system`;没有就按《40-採集演示操作指南》§0 启动 QEMU |
| 场景一 timestamp 不跳 | 查 thermalctld 进程参数有无 `--thermal-monitor-update-interval 3`(§3.2 的 reread/update 教训) |
| psud FATAL 或 thermalctld 写循环冻结 | 跑 `s1_pmon_poll.sh`,脚本自动重启自愈(重启后已知漂移) |
| 场景二 warning 没翻 | 同上:查 thermalctld 3 秒参数;还没翻就跑 `s2_thermalctl.sh` 分步定位 |

---

## 修订历史

| 日期 | 内容 |
|:--|:--|
| 2026-11-10 | 由《45-三场景演示总览(初学者版)》拆分:本篇承接场景一/二(PMON 硬件数据采集与设置),场景三移至姊妹篇;命令与预期输出实机验证基准 2026-10-21~26;卡点表补「timestamp 不跳」「psud FATAL 自愈」两行,ssh 卡点改指《40》§0。 |
| 2026-10-26 | 源篇建篇与 s1/s2 一键脚本收编(继承自《45-三场景演示总览》)。 |
