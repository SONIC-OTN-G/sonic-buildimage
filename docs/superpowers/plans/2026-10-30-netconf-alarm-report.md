# NETCONF 告警上报(方案一:Go 自研协议壳)Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** OTAI 告警可经 NETCONF(SSH :830)以 openconfig-alarms XML 上报网管:GET 对账 + 订阅实时推送,宿主机 ncclient 一键演示。

**Architecture:** alarm_app(Go,translib 层)翻译 CURALARM→openconfig-alarms 结构,先经现有 rest_server(:443)验证;新组件 netconfd(Go,方案一协议壳:SSH + RFC 6241/5277 子集)与 rest_server 同构地 import translib 复用同一 app;XML 序列化用 ygot 生成的 ocbinds;全链零 C 代码新增。

**Tech Stack:** Go(x/crypto/ssh、ygot/ocbinds)、translib app 框架、swsscommon Go bindings、Redis keyspace、ncclient(测试侧)。

**Spec:** `docs/06-數據採集/49-告警上报NETCONF三大方案对比与推荐.md`(方案一,§7 落地顺序)+ 背景数据流 `docs/06-數據採集/46/47`。

## Global Constraints

- 仓库纪律:改动分别落在 `src/sonic-mgmt-common` 与 `src/sonic-mgmt-framework`,各自从 `otn_pre_202411` 开分支 `tom-wsl`;父仓库不动。
- 方案一原则:**零 C 代码新增**——不引入 libyang/libnetconf2/sysrepo,只用 Go 生态现成件。
- syncd 零改动;CURALARM 六字段契约不变(doc 46 §2③)。
- NETCONF 只做子集:hello/capabilities、get、get-config、create-subscription、close-session、kill-session、notification(RFC 5277);其余 operation 返回 `operation-not-supported` rpc-error。
- 演示级认证:netconfd 密码认证,user=`admin`,密码 env `NETCONF_PASSWORD`(缺省 `YourPaSsWoRd`);不新增明文以外的证书负担。
- eventStream 名固定 `openconfig-alarms`;severity 映射:OTAI `WARNING/MINOR/MAJOR/CRITICAL` 直通 OC 同名枚举,其余(UNKNOWN/NOT_ALARMED/CLEARED/NOT_REPORT)→ `UNKNOWN`。
- 长构建/部署按 AGENTS.md 铁律后台化(`setsid nohup` + 轮询)。

## Review Focus

1. **CURALARM 键内特殊字符**(`#`、`|`)进 OC `resource`/`alarm-type-id` 字段——期望原样保留、XML 无转义错误。测试:Task 4 翻译单测含 `#`/`|` 用例。
2. **severity 越界值**(OTAI 1–3 与 8+ 的枚举)——期望落 `UNKNOWN` 不 panic。测试:Task 4 单测越界用例。
3. **time-created ns epoch**——期望转成 OC `time-created`(RFC 3339)且不丢精度到秒。测试:Task 4 单测固定 epoch 用例。
4. **syncd flush 风暴**(线卡掉线清空整表 CURALARM,短时大量 delete)——期望每会话通知不互相阻塞、断开的会话不拖垮推送循环。测试:Task 8 推送循环带 per-session 非阻塞写 + 断线摘除(代码评审项)。
5. **多会话并发订阅**——期望第二会话订阅时第一会话不受影响。测试:Task 10 demo 脚本双会话步骤。

---

### Task 1: 分支落位

**Files:** 无新文件(git 操作)。

**Interfaces:**
- Produces: `src/sonic-mgmt-common` 与 `src/sonic-mgmt-framework` 各自的本地分支 `tom-wsl`(基线 `otn_pre_202411`);后续所有 Task 在这两个分支上工作。

- [ ] **Step 1: mgmt-common 落分支**

```bash
cd src/sonic-mgmt-common
git checkout -b tom-wsl otn_pre_202411
git log --oneline -1   # 记录 base sha
```

- [ ] **Step 2: mgmt-framework 落分支**

```bash
cd src/sonic-mgmt-framework
git checkout -b tom-wsl otn_pre_202411
git log --oneline -1
```

- [ ] **Step 3: 验证两树干净**

Run: `git -C src/sonic-mgmt-common status --short && git -C src/sonic-mgmt-framework status --short`
Expected: 空输出。

### Task 2: openconfig-alarms.yang 引入与代码生成验证

**Files:**
- Create: `src/sonic-mgmt-common/models/yang/openconfig-alarms.yang`
- Create: `src/sonic-mgmt-common/models/yang/openconfig-alarm-types.yang`(OC 官方伴生 types 模型,若主模型 import 它)

**Interfaces:**
- Produces: `openconfig-alarms.yang` 注册进代码生成(ocbinds 出现 `openconfig-alarms` 包),供 Task 3 的 alarm_app import;路径根 `/openconfig-alarms:alarms`。
- 模型来源:OpenConfig 公开版(github.com/openconfig/public/release/models/alarm/openconfig-alarms.yang + openconfig-alarm-types.yang),取与本仓库其他 OC 模型同期版本。

- [ ] **Step 1: 下载两个官方 yang 到 `models/yang/`**

Run: `curl -o .../openconfig-alarms.yang https://raw.githubusercontent.com/openconfig/public/master/release/models/alarm/openconfig-alarms.yang`(同 types)。
Expected: 文件存在且以 `module openconfig-alarms` / `module openconfig-alarm-types` 开头。

- [ ] **Step 2: 跑 models 代码生成**

Run: 在 `src/sonic-mgmt-framework`(其 Makefile 驱动 mgmt-common 的 codegen)`make models 2>&1 | tail -20`
Expected: 无 error;`translib/ocbinds/` 下出现 openconfig-alarms 相关生成物(若生成物在 mgmt-common 的 build 输出目录,记录实际路径)。

- [ ] **Step 3: 若 codegen 未自动纳入(决策点)**

做法(二选一,按 Step 2 结果):a) 把模型登记进 codegen 清单(查 `models/yang/allyangs*` 或 build/yang 的扫描规则,照 openconfig-lldp.yang 的先例补登记);b) fallback——app 不用 typed binding,直接构造 `map[string]interface{}` JSON 响应(Task 4 按此调整,接口不变)。把所选路径写进本 plan 的执行备注。

- [ ] **Step 4: Commit**

```bash
git -C src/sonic-mgmt-common add models/yang/openconfig-alarms*.yang
git -C src/sonic-mgmt-common commit -m "[netconf] 引入 openconfig-alarms 模型(告警上报,方案一)"
```

### Task 3: alarm_app 骨架与注册(GET 路径)

**Files:**
- Create: `src/sonic-mgmt-common/translib/alarm_app.go`
- Test: `src/sonic-mgmt-common/translib/alarm_app_test.go`

**Interfaces:**
- Consumes: Task 2 的模型/binding;照 `translib/lldp_app.go` 的 app 结构与注册方式(该文件是本仓库 app 框架的现行先例:app 名、路径注册、translate/process 分离)。
- Produces: app 路径 `/openconfig-alarms:alarms`;导出函数 `func translateCuralarmEntry(fields map[string]string, key string) (map[string]interface{}, error)` ——输入 CURALARM hash 字段(doc 46 §2③ 六字段:`id/time-created/text/severity/type-id/resource`)与键名(格式 `resource#type-id`),输出 OC alarm 节点 map:`resource`、`alarm-type-id`、`severity`、`text`、`time-created`(RFC 3339,由 ns epoch 转换)、`status`(raise→"RAISED");severity 映射按 Global Constraints。Task 4 测它,Task 6 复用。

- [ ] **Step 1: 写 `translateCuralarmEntry` 的失败测试**(含 Review Focus 1/2/3 用例:`#`/`|` 键、severity=99、固定 ns epoch `1792843160178746605` → RFC 3339 `2026-10-24T19:39:20+08:00`(按本地时区断言,用固定 `time.Local` 设定))

- [ ] **Step 2: 跑测试确认失败**

Run: `go test ./translib/ -run TestTranslateCuralarm -v`
Expected: FAIL(函数未定义)。

- [ ] **Step 3: 实现 `translateCuralarmEntry`(纯函数)+ app 骨架注册**

app 骨架照 lldp_app.go:name=`/openconfig-alarms:alarms`;GET 的 translate/process 本步先返回空列表(占位),Task 4 接通数据。

- [ ] **Step 4: 跑测试通过**

Run: 同 Step 2。Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git -C src/sonic-mgmt-common add translib/alarm_app.go translib/alarm_app_test.go
git -C src/sonic-mgmt-common commit -m "[netconf] alarm_app 骨架与 CURALARM 翻译纯函数"
```

### Task 4: alarm_app GET 接通 CURALARM

**Files:**
- Modify: `src/sonic-mgmt-common/translib/alarm_app.go`(translateGet/processGet 实现)
- Test: `src/sonic-mgmt-common/translib/alarm_app_test.go`

**Interfaces:**
- Consumes: Task 3 的 `translateCuralarmEntry`;`translib/db` 的 STATE_DB 访问(`db.ConfigDBConnect` 类似先例在 lldp_app/acl_app 中,STATE_DB 用 `db.NewDB(db.Options{DBNo: db.StateDB})` 类接口,以 db 包实际签名为准)。
- Produces: GET `/openconfig-alarms:alarms/alarm` 返回 `{"openconfig-alarms:alarms":{"alarm":[...]}}`(数组每元素 = translateCuralarmEntry 输出);按 `time-created` 降序。

- [ ] **Step 1: 失败测试**:mock/真 redis(照 acl_app_test 的 db 测试基建,若其依赖真实 redis 则按其 Makefile 的 test 目标起法)预置两条 CURALARM hash,断言 processGet 输出数组长度 2、字段值、排序。

- [ ] **Step 2: 跑测试确认失败**(GET 未实现,返回空)。

- [ ] **Step 3: 实现 processGet**:keys 扫描(`CURALARM*`)→ 逐键 HGETALL → translateCuralarmEntry → 排序组装。

- [ ] **Step 4: 跑测试通过**。

- [ ] **Step 5: Commit** `git -C src/sonic-mgmt-common commit -m "[netconf] alarm_app GET 接通 STATE_DB CURALARM"`(add 对应文件)。

### Task 5: REST 对账验证(app 的第一出口)

**Files:**
- Create: `scripts/rest_alarm_check.sh`(WSL 侧,一次性验证脚本,留存)

**Interfaces:**
- Consumes: Task 4 的 alarm_app(经 rest_server :443);VM 已部署的 mgmt-framework 容器在跑旧版——本步要出新 deb 并部署。
- Produces: REST GET 验证通过的证据;后续 Task 8/9 复用同一部署流程。

- [ ] **Step 1: 重编 mgmt-framework deb**(长构建后台化)

```bash
cd ~/sonic-buildimage
setsid nohup make -j$(nproc) target/debs/bookworm/sonic-mgmt-framework_1.0-01_amd64.deb > /tmp/opencode/mgmtfw-build.log 2>&1 < /dev/null &
```
轮询 log 直到产出 deb(照 doc 42 §2.3:先 `rm` 旧 deb 再 make,防假 up-to-date)。

- [ ] **Step 2: 部署到 VM**(照 doc 42 §3.2 流程:deb → docker cp → 安装 → 重启 rest-server;rest_server 与 netconfd 无关但同容器)

- [ ] **Step 3: WSL 验证 REST GET**

Run: `scripts/rest_alarm_check.sh`(内含:curl -k https://localhost:443/restconf/... 或 rest_server 实际 API 形态——以 rest_server 的路由文档/实测为准;对照 `ssh VM "sonic-db-cli STATE_DB HGETALL 'CURALARM|linecard1#...'"`)
Expected: REST 返回的 alarm 列表与 db 内容一致(字段/条数)。

- [ ] **Step 4: 记录证据到执行备注**(响应样例);Commit 脚本到父仓库 `scripts/`。

### Task 6: netconfd 骨架(SSH :830 + hello/capabilities)

**Files:**
- Create: `src/sonic-mgmt-framework/netconf/main.go`(进程入口,flag:`-port 830`、`-logtostderr`)
- Create: `src/sonic-mgmt-framework/netconf/ssh.go`(SSH listener + subsystem "netconf" + 密码认证)
- Create: `src/sonic-mgmt-framework/netconf/rpc.go`(RFC 6241 殼:hello/capabilities/rpc 分发;close/kill-session)
- Test: `src/sonic-mgmt-framework/netconf/rpc_test.go`

**Interfaces:**
- Consumes: 无(本 task 不接 alarm_app,能力列表先静态)。
- Produces:
  - `type Session struct { ID uint32; Write func([]byte) error }` 与 `func HandleRPC(s *Session, xmlIn []byte) error`(rpc_test 测它);
  - capabilities 列表常量含 `urn:ietf:params:netconf:base:1.0/1.1`、`urn:ietf:params:netconf:capability:notification:1.0`、`http://openconfig.net/yang/alarms?module=openconfig-alarms&revision=<ver>`。

- [ ] **Step 1: rpc.go 失败测试**:输入 hello XML → 期望 capabilities 消息包含上述 URI;输入 `<rpc><get-config>` → 期望 `operation-not-supported` 或占位空数据(本 task 允许占位,Task 7 替换);断言 rpc-error 帧 XML 良构。

- [ ] **Step 2: 跑测试确认失败** → **Step 3: 实现 rpc.go 最小集** → **Step 4: 跑测试通过**。

- [ ] **Step 5: VM host 冒烟(netconfd 可脱离容器先跑)**

Run: 在 VM 编出 netconfd 二进制(容器构建链或本地 go build 带交叉),VM host 直接跑 `netconfd -port 830`(host 网络可达 redis 与网管);WSL:
```bash
pip3 install --user ncclient
python3 - <<'EOF'
from ncclient import manager
m = manager.connect(host="localhost", port=830, username="admin",
                    password="YourPaSsWoRd", hostkey_verify=False)
print([c for c in m.server_capabilities if "alarms" in c])
EOF
```
Expected: 打印含 openconfig-alarms capability。(WSL→VM 需 `ssh -L 830:localhost:830` 隧道,照 doc 47 模式。)

- [ ] **Step 6: Commit** `git -C src/sonic-mgmt-framework commit -m "[netconf] netconfd 骨架:SSH :830 与 RFC6241 子集壳"`。

### Task 7: netconfd 接 translib(GET)

**Files:**
- Modify: `src/sonic-mgmt-framework/netconf/rpc.go`(get/get-config → translib)

**Interfaces:**
- Consumes: translib 公开 API(照 sonic-gnmi `transl_data_client/transl_subscriber.go` 的调用形态:`translib.Get` / path `/openconfig-alarms:alarms`);Task 4 的 alarm_app(被注册后,Get 自动路由)。
- Produces: get/get-config 返回 OC alarm 列表 XML(ygot 序列化);netconfd 进程启动时完成 translib 初始化(照 rest_server main 的 init 顺序)。

- [ ] **Step 1: 失败测试**:`HandleRPC(get-config)` 输入含 filter xpath `/openconfig-alarms:alarms` → 期望 rpc-reply data 节点含 `alarm` 元素(mock translib 响应或注入内存 app——以 translib 是否可测注入为准,不行则此步用真 redis 置数)。
- [ ] **Step 2–4: 实现 → 测试通过**(模式同 Task 6)。
- [ ] **Step 5: VM host 冒烟**:先注入一条告警(json 旋钮,doc 46),ncclient `m.get(filter=('xpath', '/openconfig-alarms:alarms'))` → 对账 `sonic-db-cli`。Expected: 数据一致。
- [ ] **Step 6: Commit** `"[netconf] netconfd GET 接通 translib alarm_app"`。

### Task 8: 订阅与 notification 推送

**Files:**
- Create: `src/sonic-mgmt-framework/netconf/subscription.go`(订阅会话表 + 推送循环)
- Modify: `src/sonic-mgmt-common/translib/alarm_app.go`(暴露 keyspace 订阅回调)
- Test: `src/sonic-mgmt-framework/netconf/subscription_test.go`

**Interfaces:**
- Consumes: translib 订阅机制(照 `translib/db/db_subscribe_sa.go` 与 sonic-gnmi transl_subscriber 的用法:注册 CURALARM 前缀 keyspace,回调收 add/del);Task 3 的 `translateCuralarmEntry`(del 事件时用缓存的上次条目 + `status="CLEARED"`)。
- Produces:
  - `func (s *SubManager) AddSession(sess *Session, stream string)` / `RemoveSession(id uint32)`;
  - 每会话独立发送队列,写失败即摘除会话(Review Focus 4);
  - 通知帧:`<notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>RFC3339</eventTime><alarms xmlns="http://openconfig.net/yang/alarms">...</alarms></notification>`。

- [ ] **Step 1: 失败测试**:SubManager 挂两个假 Session → 注入一条事件 → 断言两会话各收到一帧合法 XML;其一写错误 → 断言其被摘除且另一会话后续仍收到。
- [ ] **Step 2–4: 实现 → 通过**(模式同前)。
- [ ] **Step 5: VM host 冒烟**:ncclient `m.create_subscription()` 挂着 → VM 注入 raise → 10 秒内 ncclient `take_notification()` 收到帧,text/severity 正确 → 注入 clear → 收到 CLEARED 帧。
- [ ] **Step 6: Commit** `"[netconf] 订阅与 notification 推送(多会话,断线摘除)"`。

### Task 9: 容器接线与部署

**Files:**
- Modify: `src/sonic-mgmt-framework/Makefile`(netconf 构建目标,进 deb)
- Modify: `src/sonic-mgmt-framework/debian/sonic-mgmt-framework.install`(加 `build/netconf/netconfd usr/sbin`)
- Modify: `dockers/docker-sonic-mgmt-framework/supervisord.conf`(加 `[program:netconfd]`,command `/usr/sbin/netconfd -port 830 -logtostderr`,autostart 沿用 dependent_startup 模式,`critical_processes` 视容器清单惯例加)
- Test: VM 部署后 `ss -tlnp | grep 830`

**Interfaces:**
- Consumes: Task 6–8 的 netconfd(容器内二进制);Task 5 的部署流程(doc 42 §3.2)。
- Produces: 容器内常驻 netconfd;演示以容器内进程为准(不再是 VM host 裸跑)。

- [ ] **Step 1: Makefile/install/supervisord 三处接线**(照 rest-server 的 program 写法)。
- [ ] **Step 2: 重编容器**(后台化):`setsid nohup make target/docker-sonic-mgmt-framework.gz ...`
- [ ] **Step 3: VM 部署新容器镜像**(照既有容器更新流程)→ `ss -tlnp | grep 830` 见 netconfd。
- [ ] **Step 4: ncclient capabilities + GET 冒烟(容器内路径全链)**。
- [ ] **Step 5: Commit** mgmt-framework 与父仓库(docker 配置改动)分别提交,message `[netconf] netconfd 进容器:`...`。

### Task 10: netconf_alarm_demo.sh 端到端演示

**Files:**
- Create: `scripts/netconf_alarm_demo.sh`(WSL 侧;风格对齐 `scripts/gnmi_alarm_demo.sh`)

**Interfaces:**
- Consumes: Task 9 的容器化 netconfd(:830,SSH 隧道 `ssh -L 830`);doc 46 注入旋钮;ncclient。
- Produces: 一键演示:预检 → 隧道 → 双会话订阅 → 注入 pre-clear/raise/clear → 打印收到的 `<notification>` 摘要 + GET 对账表(Review Focus 5 的双会话在此覆盖)。

- [ ] **Step 1: 写脚本**(结构照 gnmi 版:预检/隧道复用/注入序列/事件摘要/DB 对账;ncclient 脚本用 `python3 - <<EOF` 内嵌,订阅用 `m.notification_handler` + 阻塞收集)。
- [ ] **Step 2: 跑通一遍**,预期输出形态(对齐 gnmi demo):

```
SNAPSHOT   get 对账:N 条
NOTIF      RAISED   linecard1#LOW_TEMPERATURE_ALARM  WARNING  ...
NOTIF      CLEARED  ...
DB 终态:   CURALARM / HISALARM 对账
```

- [ ] **Step 3: 双会话验证**:脚本内开两个 ncclient 会话,两会话都收到通知。
- [ ] **Step 4: Commit** `git add scripts/netconf_alarm_demo.sh && git commit -m "[netconf] NETCONF 告警一键演示脚本"`。
- [ ] **Step 5: 归档**:doc 47 同款实测记录(新 doc 50 或并入 47,按用户指示)+ tmp.md/待办状态更新**仅按用户单独指示**。

---

## Execution 备注(执行期填写)

- Task 2 codegen 实际路径/fallback 选择:**自动纳入确认**——`models/yang/Makefile` 将顶层 `*.yang` 同步至 `build/yang`,`translib/Makefile` 的 `YANG_FILES = find build/yang -name "*.yang"` 令 ygot 全量重生成 ocbinds;`codegen.config` 默认全纳入(openapi 侧同理)。模型取自 openconfig/public `release/models/system/`(0.3.2,import 四项依赖树内齐备);ocbinds 实际重生与含 alarm structs 的验证顺延至 Task 5 deb 构建(Go 工具链仅在构建容器内,WSL 无 vendor)。commit:bda6b1e(mgmt-common tom-wsl)。
- Task 5 rest_server GET 的实际路由形态:**REST 出口受阻,验证车辆切换(按 plan 预留 fallback)**——根因:openapi 插件 `walk_child`(openapi.py:793)对「config true 根容器 + 仅 config false 子节点」的模型,root payload 为空提前 return → 0 路径 → 不产 yaml → 无路由;openconfig-alarms 正是此形态(lldp 根下有 config true 子树幸存)。实证:yang_to_openapi 日志处理了 alarms 但 build/yaml 无 alarms.yaml;rest_server 二进制含模型串但路由 404(lldp 200 对照)。**净收获**:两 deb 构建链打通(Go 守卫/go mod tidy/retag base 912b8227828),rest_server 新二进制部署运行正常,alarm_app 胶水经真实 vendor 路径编译通过。alarm_app 的 GET 验证改由 Task 7 netconfd 对真 redis 完成;openapi 插件补丁列为可选后续(REST UI 出口)。
- 其他偏离 plan 的实测决策:**Task 9 补充**——(1) VM 落位改用 doc 42 同款 cp 技巧而非重编镜像:netconfd+libyang deb 进容器 /usr/local/bin,容器 supervisord 加 netconfd.conf;(2) 踩坑:容器 host 网络,裸进程占 :830 导致容器 netconfd bind 失败进 FATAL(exit 255)——先 pkill 裸进程再 supervisorctl start;(3) 容器内依赖:libyang.so.1 经 libyang_1.0.73 deb 安装(dpkg -i 进容器);(4) repo 落源:mgmt-framework Makefile 加 netconf 目标+install 打包(4b39a0e),父仓 supervisord 模板加 netconfd(3df592006),下次镜像构建自动生效;critical_processes 为空文件,未动。**Task 7 补充**——(1) ocbinds 未含 alarms 结构(T5 构建 重生成未接线,0 命中)→ netconfd 不走 ygot binding,直接把 alarm_app JSON 组装成模型 XML(alarms_xml.go;形态经模型核对:alarm{id}/state/{id,resource,text,time-created,severity,type-id});ygot binding 迁移列为后续;(2) time-created 在 XML 输出为 ns 整数(模型 timeticks64),由 RFC3339 回转,亚秒精度丢失(已知,暂受);(3) netconfd 编译管线定型:容器内 go mod vendor → patches/apply.sh vendor(antchfx 补丁:ParseJsonMap/SetDepDataClbk 等不在上游任何版本,系 sonic-otn libyang3 移植自带)→ CGO 构建;运行时依赖 libyang.so.1(VM host FRR 自带 ✓);(4) 单测需 database_config.json 于 /var/run/redis/sonic-db/(tools/test 副本)。commit:82ff7fd。**构建环境三坑**(1) 顶层 Makefile 逐环境尝试,buster 失败即中止 → 需 NOJESSIE/NOSTRETCH/NOBUSTER/NOBULLSEYE=1 直达 bookworm;(2) slave 容器 Go 1.19 读不懂 go.mod 1.24.4 → 两仓 debian/rules 加 Go 1.24.4 sudo 升级守卫(容器非 root 但 sudoers NOPASSWD);(3) slave BASE_TAG 随 versions 文件漂移(912b8227828)而本地只有旧 base → `docker tag` 旧打新。(4) 容器 /tmp 是 tmpfs,docker cp 的 deb 要放 /usr/local/bin(doc 42 §3.1 同款陷阱)。(5) mgmt-framework go.mod 缺依赖 → 容器内 go mod tidy(go-redis v7→7.4.1 等)。**Task 3 重构**——翻译纯函数放独立子包 `translib/alarm`(导出名 `TranslateCuralarmEntry`,零重依赖),原因是 translib 主包传递依赖 cvl→antchfx(版本漂移,独立编译即红);translib 包内 `alarm_app.go` 为薄胶水(注册 isNative + 占位),其编译验证随 T5 mgmt-framework 构建路径(该路径才是 translib 的真实编译环境)。测试容器配方:slave-bookworm + 容器内临时 Go 1.24.4 + dpkg 装 tree 内 `libyang(_dev)_1.0.73` deb。commit:58d536c。
