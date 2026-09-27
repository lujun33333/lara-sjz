# 三角洲绘制点对点对照（静态，2026-09-27）

## 样本与证据范围

- 目标：`三角洲行动-cn..ipa`，版本 `1.201.37117 (68)`；`Payload/DeltaForceClient.app/Frameworks/iTopDns_dylib` 的 ZIP CRC32 为 `0x6e4ce486`。
- 这份含附加模块的包只用于静态菜单/绘制对照。实际运行目标是官方下载的 `三角洲行动_1.201.37117（正版）.ipa`；独立悬浮 App 不依赖该附加模块。两包主程序 UUID、大小和主要段布局相同，但官方包 `cryptid=1`，其世界根、相机与骨骼值仍须在官方进程验证。
- 下表字符串地址均为该 Mach-O 的 VA，当前样本 `__TEXT` 文件偏移与 VA 相同。菜单段在 `0x384c2a` 起；`0x1f685c` 起的 ARM64 `ADRP/ADD` 将菜单标签及状态变量传给 UI。绘制路径的全局读取是独立证据，不等于运行效果。
- 外部链：`SJZCollector` 通过 `OwnMemoryReader` 读主程序数据，发布 `sjzesp_item_t`；`SJZHUDBridge` 把快照变为前台 Metal、后台 CoreAnimation 共用的绘制命令。所有字段和偏移仅按此主程序 UUID/版本使用。
- 真人 15 个 mesh 骨骼索引的来源仍是旧 `TCII 0906` 样本；当前官方版未独立确认各点的解剖位置。`READY` 或测试夹具通过不能替代官方进程的骨点投影验收。

## 1.201 目标位置链校正

- 当前样本 `iTopDns_dylib` SHA-256 为 `436fac659baefb574512bbda279d7ec3bce383cb58703ab18789ed3a00dc25d4`。其 `0x1d68c4` 从 `actor+0x180` 取得 component，`0x1d6860–0x1d687c` 从 `component+0x168` 读取三维位置；人物 `0x20579c` 和物资 `0x20fe80` 都调用该 helper。外部 `OwnReadActorPosition` 已改用 `+0x168`，旧 `+0x148` 不再驱动目标投影或目标米数。
- 人物 `0x2057d8–0x205814`、物资 `0x20fecc–0x20ff04` 将目标与本人三轴差值除以 100；人物 `0x2058dc–0x2058f4`、物资 `0x20ff68–0x20ff78` 把目标位置送入投影 `0x1d7030`。真人框 `0x1d72a8–0x1d72c0` 使用目标 z±88。物资 `0x210054–0x210090` 从 `actor+0x1200` 数据对象的 `+0x68` 取等级（1–6 有效，其余归 0），从 `+0xdc` 另取显示价格；旧版把 `+0xdc` 当等级会误拒物资。
- 参考 HUD `0x1fc810–0x1fc824` 按等级不低于滑条值过滤；`0x1fc924–0x1fc990` 以等级调色，在投影点居中绘制 22 点、四向 1 像素黑描边文本。普通物资有正价格时显示 `[%d$] [%.fm]`，否则只显示 `[%.fm]`；盒子显示 `Death Box [%.fm]`。普通物资不要求 FName 非空；参考 FName 的逐字节解码与当前通用 `objectName()` 尚未证明等价，特殊盒子分类仍有限。
- 本人位置在样本中的局部结构 `x25+0x188` 尚未追到稳定内存来源；`OwnReadLocalPawnPosition` 暂沿用 `component+0x148`，明确属于未完成的真机核验项。官方主程序同 UUID 但加密，以上目标链仍需官方游戏同帧日志和画面验收。

## 菜单 → 数据 → 消费

| IPA 菜单（VA） | 当前设置键 | 采集/输出 | HUD 消费与目标证据 | 状态 |
|---|---|---|---|---|
| Enable ESP `0x384c2e` | `esp` → `SJZ_SHOW_ESP` | 人物采集由总开关或自瞄需求启动 | 人物命令统一受总开关约束；目标 `0x1fa0ac/0x1fa0e0` 读取原开关。 | 源码与夹具通过；设备未验 |
| Box `0x384c39` | `box` | `OwnProjectBox` → `x/top/bottom/width` | 矩形；目标 `0x1fa1f4` 读取开关。 | 源码与投影夹具通过；像素未验 |
| Skeleton `0x384c3d` | `bones` | 真人 15 点世界骨与投影 → `boneMask/boneX/boneY` | 14 条连接线并裁剪；目标 `0x1fd2d0` 读取开关。另有 `BoneData` 打包链，但 AI 点位生产方式未证。 | 真人夹具通过；AI 未接 |
| Head `0x384c46` | `head` → `SJZ_SHOW_HEAD` | 真人头、颈两点；仅开 Head 也采骨 | 在头点画圆，半径由头颈投影距离估算并限 3–60 点；目标 `0x1fa214` 读取开关。 | 真人采集夹具通过；形状近似、AI 未接 |
| Armor Lv `0x384c4b` | `armor` | `OwnReadArmor` → `armor` | 文本。 | 源码接通；游戏内文字未验 |
| Name `0x384c54` | `name` | 真人固定 14 UTF-16 单元名 → `name`；AI 类名 → 显示名 | 人物上方文字；12 组 AI 类名/显示名见样本 `0x385087–0x3852ca` 相邻字符串。 | 中文/AI 一组夹具通过；设备未验 |
| Health `0x384c59` | `health` | `OwnReadHealth` → `health/maxHealth/knocked` | 左侧血条；倒地取对应字段。 | 采集夹具通过；设备未验 |
| Distance `0x384c60` | `distance` | 人物与本地位置相减，厘米换米 → `distance` | 人物下方米数。 | 采集夹具通过；单位待设备核 |
| Line `0x384c69` | `ray` | 人物投影位置 | 画面上边中点到人物顶部的线。 | 源码接通；原端点/色彩未验 |
| Alert `0x384c6e` | 无 | 目标 `0x1fa384` 读状态；邻近块 `0x1fa3c8` 调用圆形函数 `0x32cd54`（半径 36、16 段），但可达性与是否属于 Alert 未证；`0x1fbfc8` 为配置读回 | 圆心来源、触发对象与警示条件未恢复；未以距离猜测。 | 未接，首个失败阶段见下节 |
| Bot `0x384c74` | `ai` → `SJZ_SHOW_AI` | `actor+0xe5f` 三字节标志（失败时回退 state 指针）→ `bot`；类名 → 显示名 | 单独过滤 AI；目标 `0x1fa104/0x1fbbb4` 读取开关。 | 过滤及一组类名夹具通过；全类/设备未验 |
| Weapon Name `0x384c78` | `weapon` | 武器指针/ID → `OwnKnownWeaponName` → `weapon` | 人物下方文字，未知 ID 留空。 | 源码接通；设备未验 |
| Visible Check `0x384c84` | 无 | `lineofsightto` `0x385568` 于 `0x200c94` 比较；`linetracesingle` `0x38565d` 于 `0x20c7d8` 比较；`bonevischeck` 键 `0x38560b` 的直接映射未证 | 不把投影成功当无遮挡；自瞄的 `aim.visible` 独立且无提供者时拒写。 | 未接，首个失败阶段见下节 |
| Max Dist `0x384c98` | `distance.max` → `maxDistance` | 人物/物资按世界距离过滤 | 样本 `0x1f6a28–0x1f6a44` 滑条 50–300；外部菜单已按此限制。 | 过滤夹具通过；UI 静态核对 |
| Enable Loot `0x384cd2` | `loot` → `SJZ_SHOW_LOOT` | 拾取对象采集，物资等级、价格和距离；同时统辖盒子 | 物资/盒子命令受总开关约束；目标 `0x1fcf0c` 读取开关。 | 夹具通过；设备未验 |
| Death Box `0x384cde` | `container` → `SJZ_SHOW_CONTAINER` | `InventoryPickup_DeadBody` → `SJZ_CATEGORY_CONTAINER` | Loot 与 Death Box 同开才绘制统一盒子；目标 `0x1fc888` 读取子开关。 | 父子开关夹具通过；归属/内容未接 |
| Min Level `0x384ce8` | `loot.level` → `lootLevel` | `actor+0x1200` 数据对象 `+0x68` 等级，只过滤普通物资；`+0xdc` 是显示价格 | 盒子不受等级过滤；样本 `0x1f6b50–0x1f6b5c` 滑条 0–6。 | 夹具通过；设备未验 |

## 验证与缺口

- `bash tests/run_sjz_tests.sh`：通过；新增覆盖 Head 单独采骨、ESP 总开关、Loot/Death Box 父子开关及一组 AI 类名映射。测试使用模拟内存和 ASan/UBSan，证明本次源码数据流，不证明设备画面。
- 目标菜单的 `OP`、`WF`、`Watermark` 等并非上述人物/物资数据链，本轮未改。`Alert`、真实可见性、AI Socket 骨骼、死亡盒归属及原样本完整颜色/线宽/位置仍待对应数据或真机画面对照。
- 自瞄菜单第三项原文为 `Weapon`；外部 UI 已标明语义待核，不能将现有“开火”解释直接算作目标行为对齐。

### 三条未打通链的首个失败阶段

| 功能 | 目标样本能确认的生产/消费 | 外部链首先缺什么 | 当前处理 |
|---|---|---|---|
| Alert | `0xc38383` 菜单状态在 `0x1fa384` 被读取；邻近的 `0x1fa3c8` 以半径 36 调用圆形函数 `0x32cd54`，但不能确认是可达的 Alert 绘制。 | 该调用的圆心参数 `x1` 在 `0x1fa3c0` 从 `[x19+0x38]` 读取；当前函数 `0x1f03a4` 令 `x19=sp`，至调用点未找到此槽的直接写。仍需证明间接别名写入、混淆跳表分支可达性及其目标条件，才能判断是否只依赖现有 `x/top/bottom`。 | 不接入可点击开关，不据邻近指令臆造圆心或触发规则。 |
| 绘制 Visible Check | 样本有 `lineofsightto` / `linetracesingle` 的大小写不敏感字符串比较，分别见 `0x200c94–0x200ca4`、`0x20c7d8–0x20c7e8`；样本中还出现 `bonevischeck`。 | 逐对象 LOS 结果的生产者和稳定内存字段均未定位；`sjzmem` 只有跨进程读写，没有游戏函数调用。投影成功仅说明点在相机前方。 | 不输出伪可见状态、不据此过滤绘制。 |
| AI 骨骼 | 目标的 `{BoneData=...}` 类型字符串 `0x385483` 被 `0x20a198` 和 `0x20ae54` 两处打包路径引用；AI 类名比较在 `0x205a40` 等。 | 当前版本未证实 AI 骨名→索引表或可只读复原的 Socket 世界点布局。旧 0906 实现调用 `GetNumBones/GetBoneName` 后读取另一套 mesh 数据，不能直接移植到本版本或只读传输。 | AI `boneMask=0`，只绘制有证据的基础数据和类名。 |
