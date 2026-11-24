# sonic-cli-gen:YANG 模型转 CLI 的机制与用法

## §1 完整流程

一句话定位:SONiC 的 CLI 自动生成器——**吃一个 YANG 模型,长出一套 show/config 命令**(读 CONFIG_DB 展示 / 校验后写 CONFIG_DB),零手写 CLI 代码。

```
🟠(使用者) 敲 sonic-cli-gen generate show|config <module>
│   └─ 输入 = module 名(非文件名);YANG 必须已在 /usr/local/yang-models/
│      ▼
🔵(sonic-cli-gen·解析器) 解析定形(What)
│   ├─ yang_parser.py:pyang 回调链 on_table→on_object→on_leaf
│   └─ 产出 yang_dict(表/物件/属性元数据)
│      ▼
🔵(jinja 渲染) 模板定型(How)
│   ├─ show → show.py.j2;config → config.py.j2(/usr/share/sonic/templates/sonic-cli-gen/)
│   └─ 渲染 yang_dict → click 命令树源码
│      ▼
🔵(落盘) plugins/auto/<model>_yang.py(dist-packages)
│      ▼
🔵(show / config CLI) 运行时加载(一次性,重启 CLI 即生效)
│   ├─ pkgutil 递归扫描 plugins 目录 → importlib 逐个 import
│   └─ register(cli) → 命令组挂上根命令
│      ▼
🟠(使用者) 敲生成的命令
│   ├─ show …→ 读 CONFIG_DB 展示(tabulate 表格)
│   └─ config …→ YANG 全量校验 → 写 CONFIG_DB
│      ▼
🔵(查询结果 / 写入生效)
```

一句话导航:**生成是唯一的人工动作(一条命令),之后的解析/渲染/落盘/加载全自动;命令上线后,show 是 CONFIG_DB 的镜子,config 是带校验闸门的写入器。**

## §2 读/写命令生成(generate)

### 2.1 yang 文件准备

**YANG 文件目录(两种)**:

- **配置目录(用户把文件放哪)**:仓库 `src/sonic-yang-models/yang-models/`
- **运行目录(运行时从哪读)**:`/usr/local/yang-models/`——前者经 deb 打包安装落入;解析器把该目录下**全部**模型解析进池再按名查找(main.py docstring;yang_parser.py:107);目录不对=进不了搜索池

节选(真文件 `src/sonic-yang-models/yang-models/sonic-interface.yang`):

```yang
module sonic-interface {                       // ← module 名 = generate 命令的参数(约定 2)
	yang-version 1.1;
	namespace "http://github.com/sonic-net/sonic-interface";
	prefix intf;

	import sonic-types {
		prefix stypes;                         // ← SONiC 共享类型(union 的 ip-prefix 用它)
	}

	import sonic-port {
		prefix port;                           // ← 跨表 leafref 的目标(PORT 表)
		revision-date 2019-07-01;
	}

	container sonic-interface {                // ← 顶层壳(与 module 同名)——纯入口标记,不进键

		container INTERFACE {                  // ← 表 → CONFIG_DB 表名 = 键第一段(大写约定)

			description "INTERFACE part of config_db.json";

			list INTERFACE_LIST {              // ← list 物件 1:1 个 key leaf → 键 2 段
				key "name";                    // ← key 声明:1 个 key leaf → 键 = INTERFACE|<name值>

				leaf name {                    // ← key leaf:值(Ethernet0)= 键第 2 段
					type leafref {             // ← type = config 写入时校验:值须在 PORT 表存在
						path /port:sonic-port/port:PORT/port:PORT_LIST/port:name;
					}                          // ← show 对 type 零效果;语法强制必带
				}
			}

			list INTERFACE_IPPREFIX_LIST {     // ← list 物件 2:2 个 key leaf → 键 3 段

				description "INTERFACE part of config_db.json with ip-prefix";

				key "name ip-prefix";          // ← key 声明:2 个 key leaf → 键 = INTERFACE|<name>|<prefix>

				leaf name {                    // ← key leaf 1:值段 = Ethernet0
					/* This node must be present in INTERFACE_LIST */
					must "(current() = ../../INTERFACE_LIST[name=current()]/name)"   // ← 跨 list 一致性断言
					{
						error-message "Must condition not satisfied, Try adding PORT: {}, Example: 'Ethernet0': {}";
					}

					type leafref {
						path /port:sonic-port/port:PORT/port:PORT_LIST/port:name;
					}
				}

				leaf ip-prefix {               // ← key leaf 2:值段 = 10.0.0.1/24
					type union {               // ← 联合类型:v4/v6 前缀二选一放行
						type stypes:sonic-ip4-prefix;
						type stypes:sonic-ip6-prefix;
					}
				}

				leaf scope {                   // ← 非 key leaf:显示列 + config --scope 选项
					type enumeration {
						enum global;
						enum local;
					}
				}

				leaf family {                  // ← 非 key leaf:跨字段 must 断言示范
					must "(contains(../ip-prefix, ':') and current()='IPv6') or
						(contains(../ip-prefix, '.') and current()='IPv4')";
					type stypes:ip-family;
				}
			}
		}
	}
}
```

### 2.2 生成/删除(读/写)命令

命令两条:

```bash
sonic-cli-gen generate show sonic-interface     # 读侧:读 CONFIG_DB 展示插件
sonic-cli-gen generate config sonic-interface   # 写侧:先 YANG 全量校验 → 再写 CONFIG_DB
```

| 项 | 说明 |
|:--|:--|
| `show` / `config` | CLI 组名(click.Choice 限定) |
| `<yang_model_name>` | **module 名**,不是文件名 |

产物:

- show → `/usr/local/lib/python3.13/dist-packages/show/plugins/auto/sonic-interface_yang.py`
- config → `/usr/local/lib/python3.13/dist-packages/config/plugins/auto/sonic-interface_yang.py`

生成期四步(show/config 共用主干,只差第 2 步选的模板):

| 步 | 动作 |
|:--|:--|
| ① 解析定形 | yang_parser.py 读 §2.1 的 yang → 回调链提取表/物件/属性元数据(yang_dict) |
| ② 模板定型 | 按 CLI 组选 show.py.j2 或 config.py.j2 → jinja 渲染成 click 命令树源码 |
| ③ 落盘 | 写 `plugins/auto/<model>_yang.py` |
| ④ 运行时加载 | show/config CLI 每次启动重新扫描 plugins → register 挂命令(重启 CLI 即生效) |

写侧与读侧的生成期**完全同构**——唯一差别=模板换 config.py.j2(570 行)。

反向操作 remove(删生成产物):

```bash
sonic-cli-gen remove {show|config} sonic-interface
```

- 只删 `plugins/auto/<model>_yang.py` 一个文件(generator.py:62-75),**四不动**:YANG 模型文件 / 手写插件(plugins 非 auto 子目录)/ CONFIG_DB 已写入数据 / 对侧产物(show、config 各管各的)
- 删后**下一次**敲旧命令即报 `No such command`(CLI 每次启动重新扫描,无需重启服务);文件不存在时只 warning 不报错(幂等)

##### 输出(实测,VM 2026-11-19)

```text
$ sudo sonic-cli-gen generate show sonic-passwh
INFO:sonic-cli-gen: Auto-generation successful! Location: /usr/local/lib/python3.13/dist-packages/show/plugins/auto/sonic-passwh_yang.py
$ sudo sonic-cli-gen generate config sonic-passwh
INFO:sonic-cli-gen: Auto-generation successful! Location: /usr/local/lib/python3.13/dist-packages/config/plugins/auto/sonic-passwh_yang.py
$ sudo sonic-cli-gen generate show sonic-interface
INFO:sonic-cli-gen: Auto-generation successful! Location: /usr/local/lib/python3.13/dist-packages/show/plugins/auto/sonic-interface_yang.py
$ sudo sonic-cli-gen generate config sonic-interface
INFO:sonic-cli-gen: Auto-generation successful! Location: /usr/local/lib/python3.13/dist-packages/config/plugins/auto/sonic-interface_yang.py
```

实测踩坑两则(OTN 环境):

- **生成要 sudo**:插件目录(dist-packages/…plugins/auto/)root 属主,非 sudo 报 `PermissionError: [Errno 13] Permission denied`
- **生成也会被「连坐」**:generate 初始化要过 ConfigMgmt 的整份配置加载——VM 的 CONFIG_DB 含 OTN 自定义值(`LOGGER|OTAI_*` 的 `LOGLEVEL=OTAI_LOG_LEVEL_WARN`,不在上游 sonic-logger.yang 枚举),全量加载失败 → 四条 generate 全体 rc=1;解法=临时改成合法枚举(`WARN`)实测,测毕还原。这正是 §2.5 写闸门「整份配置全量、一个字段非法连坐」的活体案例——**连读侧生成器都逃不掉**

### 2.3 读命令(单个对象)

container 物件(静态;sonic-passwh 生成物)——全表仅此一条的「固定键」表。

##### 命令

```
show passw-hardening policies
```

- 精确匹配才查得到:`get_table(PASSW_HARDENING).get(POLICIES)`——匹配不到就空
- **输出一条记录、只含值**:header 只有 attrs、不含 key 列——键段写死在命令路径里不显示

##### 文件

sonic-passwh 节选(真文件;**没有 list**,键 = 两段 container;由 module 行开始——看清第 1 个 container 没用、第 2 个才是表名):

```yang
module sonic-passwh {                      // ← module 名 = generate 命令的参数
    container sonic-passwh {               // ← 第 1 个 container:壳——纯入口标记,【不进键】
        container PASSW_HARDENING {        // ★ 第 2 个 container:表名(键第一段)
            container POLICIES {           // ★ 第 3 个 container:物件名(键第二段)
                leaf state { type feature_state; }              // ← 字段:config 写入只放行 enabled/disabled
                leaf len_min { type uint8 { range 1..32; } }    // ← 字段:config 写入超界即拒
                ...                        // 其余 leaf 同构(expiration/history_cnt/四个布尔策略字段)
            }
        }
    }
}
```

- 两个 container 全大写 = 键的两段;没有 list——静态物件形态(yang 文件不支持多对象查找)
- 字段命名:leaf 名经 `cli_name` 宏(小写+下划线转连字符)进命令;type(枚举/uint8 range)= config 写侧校验,show 零效果

##### 限制

- 键固定 2 段(表+物件),写死在命令路径——**不支持多对象查找**;全表仅此一条,不增长
- **键段多于 2 个怎么办**:静态物件 2 段封顶——第 4 层 container 静默丢弃(yang_parser.py:194),不报错直接没有;要多段键 = 改用 list 物件(§2.4,段数 = key leaf 数 + 表名段)
- 表名大写:`get_table` 内部 `table.upper()` 扫描(configdb.py:277)——库内惯例大写;yang 写小写读路径能被归一,写路径行为未验证,照惯例大写最稳

##### 输出(实测,VM 2026-11-19)

```text
$ show passw-hardening policies
STATE       EXPIRATION    EXPIRATION WARNING    HISTORY CNT    LEN MIN  REJECT USER PASSW MATCH    LOWER CLASS    UPPER CLASS    DIGITS CLASS    SPECIAL CLASS
--------  ------------  --------------------  -------------  ---------  -------------------------  -------------  -------------  --------------  ---------------
disabled           180                    15             10          8  true                       true           true           true            true
```

- 与规则对齐:10 列 = 10 个非 key leaf,**无 key 列**;单行 = 全表仅此一条

### 2.4 读命令(多个对象)

list 物件(sonic-interface 生成物)——键后段来自 list 的 key 声明,记录可增长。

##### 命令

```
show interface interface-ipprefix-list      # 零参数!枚举全表
```

- **零参数**:dynamic 分支不收任何 key 参数——想查特定一行 = 全表出来人眼滤(或手写插件/redis 直查)
- **输出 N 行**:key 值列在前 + 非 key attrs 在后(key leaf 的值既是键后段又是显示首列)
- 命令名不按 yang 大小写敲:yang 是 `INTERFACE`/`INTERFACE_IPPREFIX_LIST`(大写下划线),敲的是 `interface`/`interface-ipprefix-list`——生成时经 `cli_name` 宏转小写连字符,命令行没有 yang 的大小写
- 特例:表下只有一个 list 时中间层省略,命令 = `show <表名>`;INTERFACE 表下有两个 list(INTERFACE_LIST/INTERFACE_IPPREFIX_LIST),所以命令路径带 list 名

##### 文件

由 module 行开始的 container 骨架(完整 leaf 见 §2.1;本视图只标「谁当键」):

```yang
module sonic-interface {                    // ← module 名 = generate 命令的参数

    container sonic-interface {             // ← 第 1 个 container:壳——纯入口标记,【不进键】

        container INTERFACE {               // ← 第 2 个 container:【表名 = 键第一段】

            list INTERFACE_LIST {           // ← list 1:1 个 key leaf → 键 2 段
                key "name";                 // ← 键 = INTERFACE|<name值>
                leaf name {…}               // ← 键段 2:Ethernet0(运行时数据)
            }

            list INTERFACE_IPPREFIX_LIST {  // ← list 2:2 个 key leaf → 键 3 段
                key "name ip-prefix";       // ← 键 = INTERFACE|<name>|<prefix>
                leaf name {…}               // ← 键段 2:Ethernet0
                leaf ip-prefix {…}          // ← 键段 3:10.0.0.1/24
                leaf scope {…}              // ← 非 key leaf:显示列 + config 修改对象
                leaf family {…}
            }
        }
    }
}
```

- 键后段 = list 的 `key` 声明(段数 = key leaf 数);list 名不进键,但进命令路径
- **不同 list = 同一张表里的不同对象列表**:两张 list 共用 CONFIG_DB 表 INTERFACE——`INTERFACE_LIST` 产出 `INTERFACE|<NAME>` **一堆**记录,`INTERFACE_IPPREFIX_LIST` 产出 `INTERFACE|<NAME>|<IP_PREFIX>` **一堆**记录;同表共存,靠键段数区分

##### 限制

- **零参数枚举**:想查单行只能全表人眼滤——「写精确、读全表」不对称的读侧
- **tuple 陷阱**:多段键在 get_table 返回 **tuple**,单键字符串 get 对不上(configdb.py:261)
- 表名大写 upper() 归一(configdb.py:277),同 §2.3

单/多 key 读命令总对照:

| | 单 key(container 物件,§2.3) | 多 key(list 物件,§2.4) |
|:--|:--|:--|
| 命令形态 | `show <表> <物件>`(两段固定) | `show <表> <list名>`(**零参数**) |
| 查找方式 | 精确 get 一条,匹配不到即空 | 全表枚举 N 行 |
| 输出 | **1 行,只含值(无 key 列)** | **N 行,key 值列在前+字段在后** |
| 固定键段 | 2 段(表+物件,写死) | 1 段(表),后段=运行时值 |
| 记录增长 | 不增长(全表仅此一条) | 可增长(config add 新键) |

##### 输出(实测,VM 2026-11-19)

```text
$ show interface interface-ipprefix-list
NAME    IP PREFIX    SCOPE    FAMILY
------  -----------  -------  --------
```

- 空表(仅表头)——而且**有记录也是空**:实测 redis 直插 `INTERFACE|Ethernet0|10.0.0.1/24`(scope/family 齐全)后照旧空
- **实测发现(生成物表名错位)**:VM 版生成物查的是 **list 名表**——`get_db_table(db, '', 'INTERFACE_LIST'/'INTERFACE_IPPREFIX_LIST')`;而 CONFIG_DB 的记录在 **container 名表**(`INTERFACE|…`)下 → 对原生形态记录**恒空**。仓库版 show.py.j2 渲染 `table.name`(container),VM 版渲染 object.name——两版行为不一致(版本差异,未深究)
- 结论:sonic-interface 的生成 show 在本 VM 对「原生形态记录」不可用;单 key 静态物件(passwh,§2.3)不受此错位影响(表名 = container 名,恰好一致)

### 2.5 写命令(单个对象——yang 文件不支持多对象查找)

container 物件(静态;sonic-passwh 生成物)。

##### 命令

```
config passw-hardening policies len-min 16      # 写 len_min=16
```

##### 文件

基于同一份 **§2.3 章节**的文件内容(sonic-passwh;同一 yang 生成的读写命令天然对同一个键):命令第 1-2 段 = 两个 container(表+物件),第 3 段 = leaf 名(`len_min`→`len-min`,cli_name),第 4 段 = 值;键段不收参数(单 key 表全表只有这一条,写哪条由命令路径决定)。

##### 限制

- **一 leaf 一命令**——static 物件每个 leaf 各一条独立命令;想改 N 个 = 敲 N 条
- **底层合并式更新,改 A 不动 B/C**:`update_entry_validated`(config.py.j2:74)读旧记录→合并→整条重写,逐条敲=累积修改
- 记录不存在自动建(`create_if_not_exists=True`)= 隐式增;值传 None = 删除该字段
- **写闸门**:读旧配置 → **YANG 全量校验**(`validate_config_or_raise`)→ 通过才 `db.set_entry`;校验是**整份配置全量**——一个字段非法连坐整次写入被拒(设计而非 bug);校验标尺 = sonic-yang-models 本身(type 的 uint8 range 1..32 / typedef 枚举白名单 / leafref 跨表存在性 / must 跨字段断言——family 与 ip-prefix 互相锁)

##### 输出(实测,VM 2026-11-19)

**写命令需要 root**(非 sudo 直接拒):

```text
$ config passw-hardening policies len-min 16
Root privileges are required for this operation
```

**成功写入**(sudo;静默 rc=0,环境 Note 噪音行略):

```text
$ sudo config passw-hardening policies len-min 16
$ show passw-hardening policies
STATE       EXPIRATION    EXPIRATION WARNING    HISTORY CNT    LEN MIN  REJECT USER PASSW MATCH    LOWER CLASS    UPPER CLASS    DIGITS CLASS    SPECIAL CLASS
--------  ------------  --------------------  -------------  ---------  -------------------------  -------------  -------------  --------------  ---------------
disabled           180                    15             10         16  true                       true           true           true            true
```

**闸门拒绝**(超 range,红字 Aborted,值不动):

```text
$ sudo config passw-hardening policies len-min 999
libyang[0]: Invalid value "999" in "len_min" element. (path: /sonic-passwh:sonic-passwh/PASSW_HARDENING/POLICIES/len_min)
Error: Failed to validate configuration: Data Loading Failed
Invalid value "999" in "len_min" element.
Aborted!
```

- rc=1;show 确认 len_min 仍 16——拒绝未落库;libyang 报错路径正是 yang 树路径(与 §2.4 的 URL=树路径同款)

### 2.6 写命令(多个对象——yang 文件支持多对象查找)

list 物件(sonic-interface 生成物)。

##### 命令

```
config interface interface-ipprefix-list add Ethernet0 10.0.0.1/24 --scope global
config interface interface-ipprefix-list update Ethernet0 10.0.0.1/24 --scope local
config interface interface-ipprefix-list delete Ethernet0 10.0.0.1/24
```

参数两种,以 add 行为例:

- `Ethernet0 10.0.0.1/24` = **键值**:key leaf 的值,顺序按 yang `key` 声明——拼出键 `INTERFACE|Ethernet0|10.0.0.1/24`,定位「写哪条记录」
- `--scope global` = `--<attr> <value>`:给**一个非键字段**赋值;一条命令可以连写多个(`--scope global --family IPv4`,每个 `--<attr> <value>` 一个字段),省略的属性不动

##### 文件

基于同一份 **§2.4 章节**的文件内容(sonic-interface):key 值 = **位置参数**(顺序按 yang `key` 声明——命令里 Ethernet0/10.0.0.1/24 就是两个 key leaf 的值);`--option` 对应非 key leaf(scope 等);list 名 = 命令路径(不进键)。

##### 限制

- 四操作落库:**add**=`add_entry_validated`(新键,记录可增长)/ **update**=`update_entry_validated`(合并改)/ **delete**=`del_entry_validated`(删整条)/ **clear**=删某属性(仅 leaf-list)
- **key 不可经 update 改**——key 是记录的身份,想改键 = delete 旧键 + add 新键;key 全给齐(缺一段 redis 键拼不完整);`--attr` 可省(省略的属性原样不动)
- 写闸门同 §2.5(整份配置全量校验,连坐)
- key 值拼接 = redis 键后段——与 show(§2.4)枚举出的键一一同源;**写精确、读全表**是固定不对称

四命令对照(多 key 表;show 行见 §2.3/§2.4):

| 命令 | key 部位 | 栏位部位 |
|:--|:--|:--|
| show …(枚举) | 零参数(全表 dump) | 键值只是输出列 |
| … add <keys> [--attrs] | 全给齐(新建) | --attr 可选 |
| … update <keys> [--attrs] | 全给齐(定位) | --attr 可选、省略不动 |
| … delete <keys> | 全给齐(删哪条) | 无 |

##### 输出(实测,VM 2026-11-19)

**① interface 生成物在本 VM 撞名,加载失败(实录)**:

```text
failed to import plugin config.plugins.auto.sonic-interface_yang: interface already exists in CLI
```

- 原因:生成的组名 `interface`(cli_name)与内置 `config interface` 命令组撞名 → 插件 import 失败 → 本节的 interface 写命令**不可用**(每次 config 调用都刷此警告)
- 教训:yang 顶层表名**避开通用名**(interface/vlan/system 这类与内置 CLI 组撞车的名字)——命令名命名空间风险的真实案例(该撞名插件已从 VM 移除,还 config CLI 干净)

**② 同为 list 物件的可用替代:otn-attenuator(OTN 生成插件,加载无冲突)实测**:

```text
$ sudo config otn-attenuator update VOA0-1 --attenuation 3.5     # rc=0,静默
$ show otn-attenuator
NAME    ATTENUATION MODE      TARGET OUTPUT POWER      ATTENUATION  ENABLED
------  --------------------  ---------------------  -------------  ---------
VOA0-0  CONSTANT_ATTENUATION  N/A                              4.2  true
VOA0-1  CONSTANT_ATTENUATION  N/A                              3.5  true
```

- 键值 = 位置参数(VOA0-1),`--attr` 赋非键字段(--attenuation 3.5);静默成功 + show 可见——与 §2.5 模式完全一致;测毕已还原 3.2

## §3 与 redis-cli 相比的额外优势

一句话定位:**redis-cli = 开发者逃生舱**(快、直、无约束,调试/演示用它——我们整个战役全程用它);**生成 CLI = 生产契约**(校验、持久化生命周期、跨工具一致性)。优势逐条:

### 3.1 写入闸门:类型/范围/枚举/必填校验

- redis-cli **任何字符串都收**:`HSET ... len_min abc`(uint8 字段塞字母)→ 照收,脏数据进库零报警——下游守护进程读到会行为异常,且延迟引爆
- 生成 CLI 每条写入先过**整份配置 YANG 全量校验**(类型/范围/枚举/必填/跨表 leafref)→ 不合规当场拒绝,**键不诞生**

### 3.2 防错字:命令树即白名单

redis-cli 的键 = **自由字符串参数**,三种手滑三种「静默成功」(回复还很正面):

```bash
redis-cli -n 4 HSET "PASSW_HARDEN|POLICIES" len_min 16    # 表名少两个字母 → 静默创建孤儿键
redis-cli -n 4 HSET "PASSW_HARDENING|POLICIES" lenmin 16  # 字段拼错 → 真键里多出无人认识的字段
redis-cli -n 5 HSET "PASSW_HARDENING|POLICIES" len_min 16 # 库号打错 → 数据写进 FLEX_COUNTER_DB
```

危害延迟引爆:孤儿键污染 config dump,某次有人做 config 写入时**全量校验因未知表验不过**;而生成 CLI 的路径三段(表/物件/leaf)都是命令树注册节点,打错 = `No such command/option` 当场炸不落库——自由变量只剩 value,还受 type 校验(§2.4)。

### 3.3 持久化生命周期:两者都要 config save,差别在固化的质量

- **用 sonic-cli-gen 敲 config 同样要 `config save`**——不 save 照样任何重启(冷启/warm)蒸发;这一点与 redis-cli **无差别**,不是优势
- 差别在 save 固化的**内容质量**:redis-cli 直写的状态可能含脏数据/孤儿键——save = 把垃圾一起固化进 config_db.json,重启跟着回来
- 生成 CLI 的写入先过 YANG 全量校验——save 固化的是**过验的干净状态**;优势是「save 之前有闸门」,不是「不用 save」

### 3.4 YANG 单一事实源

- 一份模型同时喂:**CLI 生成、CONFIG_DB 校验、REST/OpenAPI(mgmt-framework 线)、NETCONF**——字段改动 → 改模型 → 重跑生成 → 命令/校验/文档全同步
- 手写 CLI 无此机制:每个 feature 各写各的,风格漂移、测试重复、字段人工同步
- **CONFIG_DB 唯一写入点**:上述所有北向入口(klish sonic-cli / sonic-cli-gen / RESTCONF 外部 / NETCONF)写的是同一个库(db4)——模型是源头,库是汇点
- **OTN 断链注记**:OTN 对象(optical-amplifier 等)在上游这套体系**双零命中**(命令树 XML 与 YANG 都没有,doc 58 §2.3)——OTN 线卡/采集器对象不走这条北向链;补链方向 = sonic-yanggen(§4,otn-dev)

### 3.5 受众契约:补全与文档自带

- redis-cli 用户须知:库号、键拼写惯例、值类型对应的命令(HGETALL/HSET)、字段名——**全是内部实现细节**,敲错没有任何提示
- 生成 CLI 用户只需业务语义(`show passw-hardening policies`),tab 补全 + `--help` 自带文档;网管/自动化脚本要的是稳定契约,不是原始数据面

### 3.6 诚实定位

| 场景 | 用什么 |
|:--|:--|
| 调试/演示/快速实验/我们整个战役 | redis-cli(逃生舱——快、直、无约束) |
| 交付给别人的命令面/生产配置写入 | 生成 CLI(或手写插件)——把「schema 这步」从可选变必选,换取之后所有操作的校验与契约 |

## §4 sonic-yanggen

### 4.1 现状

| 项 | 实录 |
|:--|:--|
| 分支归属 | **仅 otn-dev**(本地+org+origin);当前 **tom 分支没有** |
| 时间线 | 2025-12-03 `af8ff43da` Add otn-kvm platform(平台主体先落,**此时没有它**)→ 2026-01-14 `7924b5111` Add sonic-yanggen package(**单独一个 commit**,otn-dev 全史 12545 个 commit 中排第 8) |
| 包位置 | `platform/otn-kvm/sonic-yanggen/`(+ `device/virtual/x86_64-otn-kvm_x86_64-r0/yang_auto_cli` 配置) |
| 包内容 | sonic_yanggen.py(626 行,build 期工具)/ yang_auto_cli.sh(116 行,开机脚本)/ debian 打包全套 + sonic-yanggen.service(systemd) |

### 4.2 定位

**为什么有了 sonic-cli 还要它**:sonic-cli(klish)的命令是手写资产(XML+.py 逐条造),对象铺开时边际成本爆炸;sonic-cli-gen 能自动出命令,但**只吃 sonic-* 直映形状的 yang**——OTN 对象的模型是 openconfig 形状(业界共识,北向丢不得)。sonic-yanggen 补的正是中间那道「形状翻译」:

- **公式一**:sonic-cli-gen = sonic yang → show/config 命令直读写 CONFIG_DB
- **公式二**:sonic-cli-gen + sonic-yanggen = openconfig + annot →(build 期烤出 sonic yang)→ 接公式一

**与 annot+transformer 的关系**(上游 rest 线,doc 58 §4.2):**问题等价,手段相反**——都是解决「openconfig→CONFIG_DB 映射」,但上游在 runtime 挂 annot 便签+手写 Go 回调边跑边翻;sonic-yanggen 在 **build 期把标注烤成 sonic-\*.yang**,落库走 1:1 约定直通,transformer 层整个被绕开(sonic 家族零 annot、models_list 零条目)。准确定位:**annot 体系的 build 期编译器/替代者**,不是「又一个 transformer」。

### 4.3 用法

配置入口:device/<vendor>/<platform>/`yang_auto_cli` 文件,一行一对:

```text
# 格式:source-yang annotation-yang(示例为 OTN 实档)
openconfig-optical-attenuator.yang openconfig-optical-attenuator-annot.yang
```

流水线:

```
🟠(使用者) 写配置入口:device/<vendor>/<platform>/yang_auto_cli
│   └─ 一行:openconfig-xxx.yang openconfig-xxx-annot.yang(源模型+标注模型)
│      ▼
🔵(build 期)sonic_yanggen.py(libyang 解析,sonic_yanggen.py:169-172)
│   ├─ 逐 deviation 读标注:table-name / key-name / field-name / db-name
│   └─ 烤出 sonic-*.yang → 打包 deb → /usr/share/sonic/device-yang/<platform>/
│      ▼
🔵(开机期)sonic-yanggen.service → yang_auto_cli.sh
│   ├─ 按 ONIE 平台拷 yang → /usr/local/yang-models/
│   └─ 逐个跑 sonic-cli-gen generate show + config(失败仅记日志,不阻塞开机)
│      ▼
🔵(sonic-cli-gen)生成插件 → show/config 命令自动上线
│      ▼
🟠(使用者)敲命令 → 直读写 CONFIG_DB(不经 RESTCONF/translib)
```

声明的标注(sonic-yanggen 只吃**名字层**四类,实测用量为全部 annot 文件统计):

| 标注 | 全 annot 用量 | yanggen 处理 |
|:--|:--|:--|
| `table-name` | 11 | ✅ 烤成 container 表名 |
| `key-name` | 1 | ✅ 静态键名 |
| `field-name` | 36 | ✅ leaf 改名 |
| `db-name` | 10 | ⚠️ 仅 CONFIG/STATE,其余 raise |

### 4.4 限制

**不是 transformer**——没有进阶写入规则,只做到「单条的键值+字段」写入。三道硬边界(sonic_yanggen.py 实录):

| 边界 | 实录 |
|:--|:--|
| 双轨机制 | 每个标注带 True/False 标记(:128-142):`-name` 型=True(静态,可采用);`-transformer` 型=False(动态,无法静态化) |
| False 轨的下场 | 生成时**退回原 yang 的名字** + 打 warning(gen_key:386-405 / gen_leaf:422-424)——Go 回调逻辑(值换算/业务规则)**不继承** |
| 解析器只有三个 | parse() 只调 table_name/key_name/field_name(:169-172);subtree/path/table-transformer、pre/post、get-validate、rpc-callback **整个文件零出现**(直接无视) |

覆盖对照表(annot 全部 15 词 → yanggen 实际继承 4.5 个):

| annot 词汇 | 用量 | yanggen |
|:--|:--|:--|
| field-name | 36 | ✅ |
| field-transformer | 41 | ⚠️ 退回原名+warning |
| table-name | 11 | ✅ |
| db-name | 10 | ⚠️ 仅 CONFIG/STATE |
| key-transformer | 9 | ⚠️ 退回原名+warning |
| path-transformer / subtree-transformer / table-transformer | 8/20/4 | ❌ 零解析 |
| pre/post-transformer、get-validate、subscribe-on-change、rpc-callback、virtual-table、key-name | 各 1-2 | key-name ✅;其余 ❌ |

**工程含义**:走 sonic-yanggen 线的对象**必须设计在约定内**(名实一致、纯 CONFIG/STATE、无跨表联动)——annot 里写 transformer 型标注是静默蒸发(日志才见 warning)。要值换算/联动/钩子的对象,要么改设计,要么回上游 rest 线手写 transformer——这就是两条线并存的分工:批量标准对象走生成线,精细复杂对象走手写线。

## §5 实操备忘(一行一坑)

- 生成产物放 `plugins/auto/`(自动生成位),手写插件放 `plugins/`——加载器递归扫两者,命名冲突会在 register 时显式报 `already exists`
- 命令名来自 `cli_name` 宏(YANG 名小写+下划线转连字符),YANG 里命名即 CLI 命名,起名时想好
- show 命令读的是 CONFIG_DB——想展示 STATE_DB/ASIC_DB 运行态,默认模板不行,fork show.py.j2 改数据源宏 + 代码调用传自定义 `templates_path`(generator.py:29 的正式参数)
- config 命令的校验是**整份配置全量校验**(非单字段)——一个字段非法会连坐整次写入被拒,这是设计而非 bug
- 记录增长判据:**container 物件=固定单条**(物件层下不递归,全表仅此一条);**list 物件=可增长记录**(config add 新键,键段数=key leaf 数)
- 仓库内可对照的完整三态例子:`sonic-passwh`(YANG 源 → show.py.j2 模板 → `show/plugins/sonic-passwh_yang.py` 产物);本文统一例:`sonic-interface.yang`(141 行,doc 58 映射实例的源)
