# OTN 告警上报下半段:DB 之后 → gNMI → 网管(实测验证)

---

## §1 整体数据流全景图

```
图例:🟢 原有(未改)   🔵 验证用操作(人手)     本段全程零代码改动
      上半段(注入→syncd→写 DB)见 doc 46;本篇从 CURALARM 落库之后接续

🔵(运维/仿真) 注入告警(上半段旋钮)
│   json → vslib → syncd → STATE_DB db6 CURALARM|<resource>#<type>(六字段)
│      ▼
🟢(redis) keyspace 通知(写入即推,µs 级)
│      ▼
🟢(gnmi 容器 telemetry 进程) gNMI native 订阅服务(:8080,noTLS)
│   sonic-db origin 路径 → redis db 映射,纯镜像零加工
│   ├─ 订阅建立 → 全表快照 + sync-response(重连对账能力)
│   ├─ 键新增/变更 → updates(带完整字段)
│   └─ 键删除    → deletes(⚠表级路径,不带键名)
│      ▼
🔵(网管 NMS) gnmic 订阅端(WSL 宿主机,经 SSH 隧道)
    实时收到事件流 —— "告警上报到客户侧"闭环成立,零代码
```

一句话:**注入是入场券,写 DB 是出厂行为,gnmi 容器是零改动的镜像通道,gnmic 看到什么网管就有什么——剩下的只是渲染。**

---

## §2 逐环解释(执行者 · 文件:行数 · 命令 · 实测证据 · 踩坑)

### ② gnmi 容器本体 🟢(执行者:telemetry 进程,开机自启)

实测进程参数(`ps aux`,VM host):

```
/usr/sbin/telemetry -logtostderr --noTLS --port 8080 --allow_no_client_auth -v=2
```

- 容器 `gnmi`,host 网络模式,监听 **:8080**(不是常见的 50051)
- `--noTLS --allow_no_client_auth`:无加密无认证——演示友好,生产要换证书(容器脚本 `gnmi-native.sh` 有证书分支,配 CONFIG_DB 即启用)

路径→DB 的解析,`src/sonic-gnmi/gnmi_server/server.go:383` 与 `sonic_data_client/mixed_db_client.go:361`:

```go
// origin 必须是 "sonic-db" 才走 native 镜像路;否则走 OpenConfig/translib 翻译路
func IsNativeOrigin(origin string) bool { return origin == "sonic-db" }

// native 路径结构:elems[0]=DB 名(STATE_DB),elems[1]=实例(localhost),其后=表/键
```

订阅分发,`gnmi_server/client_subscribe.go:163`:

```go
// 三条路:native 镜像(本篇)/openconfig 翻译(无告警模型)/EVENTS 事件路(见 §3)
} else if IsNativeOrigin(origin) {
    dc, err = sdc.NewMixedDbClient(...)
} else if target == "EVENTS" && mode == STREAM {
    dc, err = sdc.NewEventClient(...)   // eventd 事件的北向门,已存在未接线
}
```

**踩坑**:
- 端口按记忆找 50051 会扑空——本镜像实际 **8080**(实测 `ss -tlnp`)
- `openconfig` 路对告警**不可用**:树里无 openconfig-alarms.yang、translib 无 alarms 应用——native 是当前唯一能走的门

### ③ 订阅端命令 🔵(执行者:网管角色,WSL 宿主机)

```bash
# 隧道(WSL→VM)
sshpass -p YourPaSsWoRd ssh -f -N -L 8080:localhost:8080 -p 2222 admin@localhost

# 探路(确认 sonic-db 模型在)
gnmic -a localhost:8080 --insecure capabilities

# 挂订阅(前台,事件实时打印,Ctrl+C 结束)
gnmic -a localhost:8080 --insecure --encoding json_ietf \
  subscribe --path 'sonic-db:/STATE_DB/localhost/CURALARM' --stream-mode on-change
```

**踩坑**(三个都是实测换来的):
- origin 语法必须 `sonic-db:/...`——**冒号后必须跟斜杠**,否则整串被当成第一个路径元素,报 `Node sonic-db:STATE_DB not found`
- **必须显式 `--encoding json_ietf`**——gnmic 默认 JSON,服务器报 `Unknown encoding JSON`
- 表级路径 `CURALARM` 即可订阅全表,单键路径(带 `|`/`#`)GET 返回空对象——**订阅和查询都用表级**

### ④ 事件流实测(订阅端三幕)🔵🟢

| 幕 | 触发 | 订阅端收到 |
|:--|:--|:--|
| 快照 | 订阅建立 | 现存告警全量(每键六字段)+ `sync-response: true` |
| UPDATE | 注入 raise(`alarm_status:0`) | 新键 + 全字段:`severity: CRITICAL`、`text` 原文、新 `time-created` |
| DELETE | 注入 clear(`alarm_status:1`) | `deletes: ["sonic-db:/STATE_DB/localhost/CURALARM"]`——**只有表路径** |

时间戳语义:gnmic 外层 `"time"` 是**事件到达 gnmi 的时刻**;告警产生时刻用载荷里的 `time-created`(ns epoch,NMS 端负责换算)。

同键重报去重:与 doc 46 §2③「首报为准」一致——同键重复 raise 在流上**无任何事件**(实测)。

**踩坑**:
- DELETE 是表级的,不带键名——NMS 收到后必须 re-GET 才知道少了哪条;做告警台要按「delete→全量对账」设计
- demo 脚本 `scripts/gnmi_alarm_demo.sh` 结束时会清掉自建隧道;手动玩之前先确认 `ss -tln | grep 8080` 还在,否则 gnmic 静默退出(实测踩过)

### ⑤ 字段 ↔ 网管需求对照 🟢

| 需求(doc 46 §事项一) | CURALARM 字段 | 实测值示例 |
|:--|:--|:--|
| 告警 id | `id` | `linecard1#LOW_TEMPERATURE_ALARM` |
| 对象 | `resource` | `linecard1` |
| 时间戳 | `time-created` | ns epoch,NMS 换算 |
| 严重度 | `severity` | `CRITICAL` |
| (白送)类型 | `type-id` | `LOW_TEMPERATURE_ALARM` |
| (白送)原文 | `text` | `live CRITICAL raise - 20:xx:xx` |

六字段即 syncd 写库的完整契约(`NotificationProcessor.cpp`,doc 46 §2③)——native 模式下**这六个字段就是接口格式**,gnmi 零加工透传。

### ⑥ 边界与后续 ⚪

| 项 | 现状 |
|:--|:--|
| 本篇通道 | gNMI 状态路(拉/订阅 CURALARM)——已闭环,零代码 |
| 事件路 | syncd `events_publish` → eventd → gNMI Events(`target=="EVENTS"` 分支已在,待接)——正统化待办 |
| NetConf | mgmt-framework 在跑,translib 无 alarms 应用;且树里无 openconfig-alarms.yang |
| SNMP | snmp 容器未运行,无告警 MIB |
| 网管侧渲染 | gnmic JSON=原始协议事件;告警表格/配色/确认流程是 NMS 自己的活 |

一键复测:`./scripts/gnmi_alarm_demo.sh`(预检→隧道→订阅→pre-clear→raise→clear→事件摘要+DB 对账)。
