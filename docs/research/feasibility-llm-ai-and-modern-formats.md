# 调研报告：LLM 驱动 AI 与 现代资源格式

> 本文只做可行性调研，**未修改任何代码**。
> 结论基于 `gamemd.exe` 1.11 的反汇编（183,858 条指令）、`Ares.dll` 3.0 分析、
> YRpp 源码，以及游戏目录的**实际数据**（`aimd.ini` 已从 `ra2md.mix` 提取）。
> 每条关键判断都给出证据（地址 / 文件 / 行号）；**推断与实证分开标注**。

---

## 摘要

| # | 设想的形态 | 可行性 | 卡点 |
|---|---|---|---|
| **1a** | **LLM 离线生成 `aimd.ini` 内容**（新增 TeamType / ScriptType / AITriggerType） | ✅ **完全可行，价值最高** | 无技术卡点，是内容工程 |
| **1b** | **加载期生成"本局 AI 配置"**（确定性选择） | ✅ 可行 | 必须所有客户端完全一致，且在首帧 AI 之前注入 |
| **1c** | **对局内实时调用 LLM** | ❌ **不可行** | 三条硬约束同时命中，其中"RNG 流错位"这一条**连"输出完全相同"都救不了** |
| **2a** | 运行时 PNG 直接当 SHP 帧 | ⚠️ 工作量大 | 必须替换 blitter 本体（89 个变体都假定源是 8 位索引） |
| **2b** | 运行时 GIF 当 SHP 动画 | ⚠️ 比 2a 更差 | SHP 架构里**没有"每帧延时"这个概念**可放 GIF 的时序 |
| **2c** | 运行时 GLB 渲染替代 VXL | ❌ **工程上不可行** | 引擎没有 3D 管线：只有软件 span 光栅器 + 光照 LUT，输出 8 位索引 |
| **2d** | 离线用 PNG/GIF 作画 → 构建期转 SHP | ✅ **已是社区标准做法** | 无 |
| **2e** | 离线 GLB → VXL 转换 | ⚠️ 可行但有硬上限 | 需自写转换器；每段尺寸与 HVA 帧数有硬上限 |

**一句话结论**：
第 1 项的正确形态是"**LLM 当内容生成器 / 数据表生成器**"，而不是"LLM 当实时决策器" ——
后者在 YR 的锁步同步模型下**不是难，而是根本不成立**。
第 2 项的正确形态是"**现代格式当创作格式、构建期转换**"，而不是"引擎直接读现代格式"。

---

# 第一部分：用 LLM 取代 / 增强 `aimd.ini`

## 1.1 `aimd.ini` 的真实结构（实测）

从 `ra2md.mix` 提取到 `D:\Games\Ra2\aimd.ini`：**138,538 字节 / 7,400 行 / 388 个 section**。

| section | 条目数 | 说明 |
|---|---|---|
| `[TaskForces]` | 132 | 编队构成清单 |
| `[ScriptTypes]` | 88 | 脚本清单 |
| `[TeamTypes]` | 163 | 队伍类型清单 |
| `[AITriggerTypes]` | **165** | AI 触发条件（**十六进制 ID 键**，不是数字序号） |
| 各对象的定义 section | **383** | = 132+163+88，**与被列出的 ID 一一对应，0 缺失 0 多余** |
| `[Digest]` | 1 | SHA-1（base64） |

### 四种条目的真实格式

```ini
; ── TaskForce：编队构成（上限 Entries[6] = 6 条）
[05C60C4C-G]
Name=3 GIs, 3 Guards
0=3,E1                ; <数量>,<单位类型ID>
1=3,GGI
Group=-1

; ── ScriptType：动作序列（上限 ScriptActions[50] = 50 条）
[0C9C878C-G]
Name=Allied Refinery Guard
0=58,1                ; <动作号>,<参数>
1=5,10
2=58,196609

; ── TeamType：31 行具名键（这就是我最初按 "数字序号" 统计时误判为 0 条的原因）
[06175EFC-G]
Name=Allied Base Guard - Refine
VeteranLevel=1
Max=2
TechLevel=0
Aggressive=yes
Reinforce=yes
IsBaseDefense=yes
Script=0C9C878C-G
TaskForce=05C60C4C-G

; ── AITriggerType：单行 18 字段内联
0CAD0DCC-G=Allied Anti-Nuke 1 ,08DA125C-G,<all>,9,0,NAMISL,0100000003000000000000000000000000000000000000000000000000000000,70.000000,10.000000,70.000000,1,0,1,0,0CB246CC-G,0,1,1
```

**AITriggerType 的 18 个字段**（与 `YRpp/AITriggerTypeClass.h:135-155` 的
`FormatForSaving()` 格式串逐字段对应）：

```
ID = Name, Team1, House, TechLevel, ConditionType, ConditionObject,
     ConditionString(64 hex), Weight_Current, Weight_Minimum, Weight_Maximum,
     IsForSkirmish, 0, SideIndex, IsForBaseDefense, Team2,
     Enabled_Easy, Enabled_Normal, Enabled_Hard
```

`ConditionString` = 4 组 `{int ComparatorOperand; int ComparatorType;}`（各 8 B，共 32 B → 64 个十六进制字符）。
比较符：`Less=0, LessOrEqual=1, Equal=2, GreaterOrEqual=3, Greater=4, NotEqual=5`。

### AI 动作的**实际使用情况**（重要）

`[ScriptTypes]` 里出现的动作号只有 **20 个不同值，范围 0..63**，频次：

| 动作号 | 次数 | | 动作号 | 次数 |
|---|---|---|---|---|
| 0 | 136 | | 11 | 16 |
| 49 | 54 | | 14 | 9 |
| 58 | 49 | | 43 | 9 |
| 46 | 45 | | 8 | 6 |
| 54 | 34 | | 63 | 4 |
| 5 | 30 | | 9 | 3 |
| 47 | 24 | | 55 | 3 |
| 53 | 22 | | 21/57/61/62 | 各 1 |

→ 原版 AI 的动作词表是**很小的数值空间（≈0..64）**，而**实际只用了 20 个**。
这直接说明：**AI 多样性的瓶颈在"内容量"，不在"决策算法"**。
（这也解释了为什么 Phobos 的新动作从 **10000** 起分段 —— 为了避开这个空间。）

### 加载方式（已核实）

```asm
0052D357  PUSH 0x82621C          ; "AIMD.INI"
0052D36F  MOV  ECX,0x887128      ; CCINIClass::INI_AI
0052D375  PUSH 0x1               ; bDigest = 1  → 解析 [Digest]
0052D378  CALL 0x4741F0          ; CCINIClass::ReadCCFile(file, bDigest, bLoadComments)
```

> ⚠️ **未确定**：`[Digest]` 是否被"计算并比对"强制校验，还是仅被解析。
> 注意 `rulesmd.ini` / `artmd.ini`（游戏目录的松散文件）**都没有** `[Digest]`，
> 只有从 MIX 出来的 `aimd.ini` 有 —— 更像原始发行文件自带的标记。
> **这一项不影响结论**，因为无论是否校验，多客户端加载的都是同一个文件。

## 1.2 AI 的决策算法（反汇编）

**整个 YR AI 的"战术选择"就是一次加权随机抽签**，收口在**一个函数**里。

```
HouseClass::Update  区段 0x4F8xxx（每逻辑帧每 house 一次）
  ├─ 0x4F8A6A  CALL 0x6F0AB0      ← 唯一的 AI 触发决策函数
  │     ├─ 概率门控：RandomRanged(1,100) vs HouseClass+0x565C
  │     ├─ 遍历 AITriggerTypeClass::Array
  │     │     └─ 0x6F0C58 CALL 0x41E720   AITriggerTypeClass::ConditionMet
  │     ├─ 命中者按 Weight_Current 累加成加权表
  │     └─ 加权抽签：RandomRanged(1, totalWeight) → 选中触发器
  └─ 0x4F8AAD  CALL 0x6F09C0      TeamTypeClass::CreateTeam
```

**关键证据（实测指令）**：

```asm
006f0af2  MOV  EAX,[0x00A8B230]    ; EAX = ScenarioClass::Instance
006f0afb  LEA  ECX,[EAX+0x218]     ; ECX = &ScenarioClass::Instance->Random   ← 偏移 0x218
006f0b01  CALL 0x0065C7E0          ; Randomizer::RandomRanged(1, 100)
006f0b06  CMP  EAX,[EDI+0x565C]    ; 与 HouseClass+0x565C 比较（0..100 的阈值）
006f0b0c  JG   0x006F0DE5          ; 掷点 > 阈值 → 本帧完全跳过 AI
```

- `0x218` 与 `YRpp/ScenarioClass.h:137` 的 `Randomizer Random; //218` **完全吻合**。
- `AITriggerTypeClass::ConditionMet`（`0x41E720`）**全程序只有一个调用者** —— 即上面那处
  → **它是插桩/改写的唯一收口**。
- `TeamTypeClass::CreateTeam`（`0x6F09C0`）只有两个调用者：`HouseClass::Update` 内 `0x4F8AAD`，
  以及脚本动作 `CreateTeam`（`TActionClass::Execute` 内 `0x6DEB6E`）。

**这意味着 AI 有天然的可调旋钮**：

| 旋钮 | 位置 | 效果 |
|---|---|---|
| 评估概率阈值 | `HouseClass+0x565C`（0..100） | 越低 → AI 越少做决策 |
| 触发器权重 | `AITriggerTypeClass::Weight_Current` | 加权抽签的实际概率 |
| 权重动态调整 | `RegisterSuccess()` `0x41FD60` / `RegisterFailure()` `0x41FE20` | AI 的"经验" |
| 队伍构成 / 脚本动作 | `aimd.ini` 数据 | 行为的实际内容 |

> ⚠️ **推断（非实证）**：`HouseClass+0x565C` 的字段名未确认 —— Ares/Phobos 全源码 grep 零命中。
> 它按 `0..100` 使用，语义上极可能是"AI 触发器评估概率"。
> ⚠️ **推断（非实证）**："每帧每 house 一次"是由 Ares 的 hook 名聚簇
> （`HouseClass_Update_*` 全落在 `0x4F8xxx`）推断出来的；Ghidra 拒绝在 `0x4F8A6A`
> 定位函数，所以未直接观测到 `HouseClass::Update` 的入口与调用者。**没有独立的"AI 帧"**。

## 1.3 决定性约束 A：**AI 消耗同步 RNG，而 RNG 进帧 CRC** ⚠️ 最致命

这一条比"输出不一致"更根本，**连"LLM 输出完全相同"都救不了**。

### 事实 1：AI 每次评估都消耗同步 RNG（两次）

上面 `0x6F0AF2`–`0x6F0B01` 是**第一次**（概率门控），加权抽签是**第二次**。
两者都走 `Randomizer::RandomRanged`（`0x65C7E0`），作用在
`ScenarioClass::Instance->Random`（**局内同步 RNG**）。

`YRpp/Randomizer.h:10` 明确区分了两个 RNG：

| RNG | 地址 | 官方注释 |
|---|---|---|
| `ScenarioClass::Instance->Random` | 偏移 `0x218` | "for any randomization happening **inside a match** … use the ScenarioClass::Random object instead!" |
| `Randomizer::Global` | `0x886B88` | "**only** … for RMG and other randomness **outside a match**" |

两者都是**表驱动有状态生成器**（`Next1`/`Next2` 索引 + `Table[0xFA]`，`Randomizer.h:34-37`）。

### 事实 2：RNG 的抽样值被直接折进帧 CRC

**Ares**（`Ares/src/Misc/Checksum.cpp:46`，hook `64DAC3 Networking_CalculateFrameCRC`）：

```cpp
Checksum = ((Checksum >> 31) + 2 * Checksum) + ScenarioClass::Instance->Random.Random();
Networking::CurrentFrameCRC = Checksum;
```

**Phobos**（`Phobos/src/Misc/SyncLogging.cpp:675`）：

```cpp
AddCRC(&EventClass::CurrentFrameCRC, ScenarioClass::Instance->Random.Random());
Game::LogFrameCRC(Unsorted::CurrentFrame % 256);
```

**Phobos 还把 RNG 的内部索引本身当作同步状态来比对**：
`SyncLogging.h:146-178` 的 `RNGCalls` 记录 `Randomizer` 的 **`Next1`/`Next2` 索引**、caller、frame、
min/max，`SyncLogging.cpp:39-40` 只在 `pRandomizer == &ScenarioClass::Instance->Random` 时
标记为 critical。

### 结论

> **AI 决策的"随机性"是从同步流里取的；这个流的每一次消耗都会改变后续所有随机数，
> 并被折进逐帧 CRC。**
>
> 因此，在 AI 决策路径上引入 LLM，**即使各客户端拿到完全相同的 LLM 输出**，
> 只要它改变了"调用了几次 RNG"（例如新增一次采样、或让概率门控少掷一次、
> 或异步重试导致帧号偏移），**后续所有随机数全部错位 → 必然 desync**。

这也是为什么"主机做决策、把结果广播出去"这种看起来能解决非确定性的方案**依然不够** ——
还得保证**RNG 消耗的次数与时机在所有客户端完全一致**。

## 1.4 决定性约束 B：锁步帧同步与帧预算

`YRpp/EventClass.h:24-34` 把这个机制写得很清楚：

```cpp
// this points to CRCs from 0x100 last frames
DEFINE_ARRAY_REFERENCE(DWORD, [256], LatestFramesCRC, 0x00B04474)
DEFINE_REFERENCE(DWORD, CurrentFrameCRC, 0x00AC51FC)

// The engine's out-of-sync sync dump, called from Execute_DoList (0x64CC68)
// when a FRAMEINFO CRC mismatch is found: writes SYNC*.TXT for the offending event
```

配套字符串证据：

| 字符串 | 地址 |
|---|---|
| `FRAMESYNC` | `0x820AF0` |
| `Frame sync stalls: %d\n` | `0x81D88C` |
| `TXT_OUT_OF_SYNC` | `0x837DE8` |
| `SYNC%01d.TXT` / `SYNC%01d_%03d.TXT` | `0x838EB0` / `0x839194` |
| `Received FRAMESYNC packet from %s. Frame = %d` | `0x8377F8` |

网络/帧同步参数（`YRpp/Unsorted.h:80-93`）：`FrameSendRate 0xA8B554`、`MaxAhead 0xA8B550`、
`MaxMaxAhead 0xA8B568`、`PreCalcMaxAhead 0xA8B56C`、`RequestedFPS 0xA8B558`、
`LatencyFudge 0xA8DB9C`、`OutOfSync 0xA8B8C2`。

`MaxAhead` / `PreCalcMaxAhead` 说明引擎会**预先把逻辑帧算到网络前面若干帧**并缓冲 ——
主线程不能随便停下来。一次 0.5–5 秒的**同步** LLM 调用会直接触发 `Frame sync stalls`，进而超时/掉线。

即：**每一逻辑帧的完整状态都被哈希并在玩家间比对；AI 是所有客户端都在模拟的。**

## 1.5 决定性约束 C：AI 状态进存档

- 引擎侧：`AITriggerTypeClass`、`TeamTypeClass`、`TaskForceClass`、`ScriptTypeClass`、
  `ScriptClass`、`TeamClass` **都声明了 `IPersistStream` 的 `Load(IStream*)` / `Save(IStream*, BOOL)`**
  → 存档流会写出这些对象。
- **动态状态确实存在**：`AITriggerTypeClass.h:183-196` 的
  `Weight_Current` / `Weight_Minimum` / `Weight_Maximum` / `TimesExecuted` / `TimesCompleted`。
  其中 `Weight_Current` **正是 §1.2 加权抽签实际用的值**，且被
  `RegisterSuccess()` / `RegisterFailure()` 动态修改 → **AI 的"经验"是跨帧持久状态**。
- **Phobos 直接为此做扩展序列化**（`Phobos/src/Ext/Team/Body.cpp:9-37`）：
  `WaitNoTargetAttempts`、`NextSuccessWeightAward`、`Countdown_RegroupAtLeader`、
  `ForceJump_Countdown`、`TeamLeader`、`PreviousScriptList` … 全部 `.Process()` 进流。
  → **队伍脚本的中间状态（倒计时、等待计数、跳转状态）必须跨存档保持**，否则读档后 AI 行为会变。
- 另有旁证：`AITriggerTypeClass::SaveToINIList(CCINIClass*, bool Global)` 的注释说明
  **`aimd.ini` 同时是 AI 状态的持久化目标之一**（`Global` = 存进 aimd.ini；否则连
  `[AITriggerTypesEnable]` 一起存，那是地图内保存才出现的节，本机 `aimd.ini` 里**没有**该节）。

> ⚠️ **未确定**：引擎 `AITriggerTypeClass::Save/Load` 的实体地址，以及存档字节里
> `Weight_Current` 的直接观测。"权重确实写盘"是基于**字段存在 + IPersistStream 声明**的强推断。

**含义**：如果 AI 决策来自一次外部调用，读档时要么把决策结果一起存下来，
要么能完全复现它 —— 后者对一个非确定性服务是不可能的。

## 1.6 表达力上限与扩展机制

这是决定"LLM 到底能表达多少东西"的一节。

| 维度 | 现状 | 能否扩展 |
|---|---|---|
| **AI 脚本动作**（`[ScriptTypes]` 的 `<动作号>`） | 原版小数值空间，实际只用 20 个 | ✅ **能**。Phobos 已把它扩到 `10000`–`18999` 分段（`Phobos/src/Ext/Script/Body.h`），新增了 `MoveToEnemyCloser=10050`、`TimedAreaGuard=10100`、`LoadIntoTransports=10103`、**`IncreaseCurrentAITriggerWeight=14001`**、`RandomSkipNextAction=16003`、`LocalVariableSet=18000` 等 |
| **队伍构成 / 参数** | `TaskForce` 上限 6 条 / `ScriptType` 上限 50 条 | ⚠️ 上限是结构体数组尺寸（`Entries[6]`、`ScriptActions[50]`），扩上限要改结构体布局 |
| **AITrigger 条件类型** | `AITriggerCondition` 是**封闭的 8 值枚举**（`AIowns=0` … `NeutralOwns=7`，加 `Pool=-1`） | ❌ **无先例**。Ares 只修了 `SideIndex`（`0x41E893`），Phobos 只修了 powersup 识别；要新增必须 hook `ConditionMet`（`0x41E720`）并把枚举往后扩 |
| **触发器分发/动作种类**（`TActionClass`/`TEventClass`，地图触发器用） | 原版跳表到 **145** 个 | ✅ **能**。Ares 示范了机制：hook `GetFlags 0x6E3EE0` / `GetMode 0x6E3B60` / `Execute 0x6DD8B7` 三处认自己的数值 |
| **AI 权重（运行时调整）** | — | ✅ **已打通**。Phobos 的 `14001 IncreaseCurrentAITriggerWeight` / `14002 DecreaseCurrentAITriggerWeight`（`Phobos/src/Ext/Script/Body.cpp:526-595`，用 `AITriggerFailureWeightDelta`/`AITriggerSuccessWeightDelta`）**在运行时改 `Weight_Current`** |

**两个关键含义**：

1. **表达力的天花板可以抬高** —— 通过新增脚本动作（Phobos 模式）与新增触发器条件（无先例，工作量大）。
2. **但"AI 想做什么"的词汇量瓶颈在"条件"而非"动作"**：
   现有 8 个条件只能表达"拥有某建筑/敌人电少/敌人钱多/铁幕充能完成"这类**极粗的局势判断**。
   想让 LLM 表达"发现敌方分基地在左翼且防空薄弱 → 派空军骚扰"，
   **现有条件词表里根本没有对应词汇** —— 这不是 LLM 的问题，是引擎语义的缺口。

## 1.7 三种形态的可行性

### (a) LLM 离线生成 `aimd.ini` 内容 —— ✅ 强烈推荐

**LLM 当"内容作者"，产出就是 INI 文本。**

- **可行性：无技术障碍。** 产出物是标准 INI，用现有工具链即可验收入库。
- 正好对应"**让 LLM 参考前者输出行动**"与"**创建更多符合 aimd.ini 格式的行动**"。
- **收益直接且可观**：§1.1 显示原版只用了 20 个动作、163 个 TeamType，
  而这些**手工编写极其枯燥** —— 这正是 LLM 最能帮上忙的地方。

**建议的工程形态**：

1. **输入**：现有 `aimd.ini` 片段（few-shot 范例）+ 该 mod 的 `rulesmd.ini` 单位清单
   + 动作词表（含参数语义）+ 目标风格（"苏军重装甲推进" / "盟军空军骚扰"）。
2. **输出**：**只新增** `[TaskForces]` / `[ScriptTypes]` / `[TeamTypes]` / `[AITriggerTypes]` 条目，
   **不改动原有条目**（避免破坏平衡与兼容）。
3. **必须有不变量校验器**（比 LLM 本身更重要）：
   - 所有引用 ID 可解析（TeamType→TaskForce/ScriptType，AITriggerType→TeamType）；
   - **`TaskForce` ≤ 6 条、`ScriptType` ≤ 50 条**（结构体硬上限，超了会读不进去）；
   - 动作号在合法域内且参数符合该动作语义；
   - 单位名存在于 `rulesmd.ini`；
   - `ConditionString` 恰好 64 个十六进制字符（4 组 × 8 字节）；
   - `AITriggerType` 恰好 18 个字段、3 个权重满足 `Min ≤ Current ≤ Max`；
   - 生成后整体过一遍 INI 解析（我们已掌握全部格式信息）。
4. **落盘为独立文件**便于 A/B 与回滚。

> **最大优势：完全不碰运行时** → 零 desync 风险、零帧预算压力、对多人完全安全。

### (b) 加载期生成"本局 AI 配置" —— ✅ 可行（推荐作为 (a) 的延伸）

在 rules/AI 加载完成后、首帧 AI 之前，注入 LLM 生成的条目。

**必须满足**：

- **一致性**：所有客户端得到**完全相同**的数据。做法只能是
  (i) 主机生成一次并随对局设置下发；或
  (ii) 用**确定的**种子（地图名 + 设置哈希）去查一个**预先缓存好的**结果集 ——
  即"**LLM 在赛前/赛外生成，对局内只做确定性选择**"。
- **时机**：必须在首帧 AI 之前，且不破坏 `INI_AI` 既有内容。
- **存档**：注入后的配置需随存档保存或可确定性重建。
- **不得改变 RNG 消耗节奏**（§1.3）—— 只改数据表、不改决策流程，这一点自然满足。

> **最省事的等价做法**：用 (a) 离线生成 **N 套"AI 人格"配置**，对局内只做一次确定性选择。
> 玩家每局感受到的 AI 不同 —— **这就是"增加对局多样性"的目标，却完全不引入任何非确定性。**

### (c) 对局内实时调用 LLM —— ❌ 不可行（不是"难"，是不成立）

需要同时完成：

1. **主机权威**：只有一台机器决策，其余等它；
2. **自定义网络事件**：YR 的 `EventType` 是固定枚举，广播"AI 决策"必须新增事件类型，
   并要求**所有客户端都装了同一份 HAres**；
3. **不阻塞逻辑线程**：异步并在消费帧之前就位；
4. **RNG 节奏完全一致**（§1.3）—— 即使 1–3 全做到，这一条仍然是**独立的、必须单独解决**的难题；
5. **存档/重连**：决策结果需可恢复。

**收益分析**：即使全部做到，AI 的表达力上限仍是
"20 个实际用过的动作 × 8 个封闭的触发条件"，LLM 只能在**离散且很小**的空间里重新组合
（§1.6）。而成本是**整个项目里最高的一类**（跨机协议 + 同步 + 存档兼容），
并且**天然与"多人对局"这个主要场景冲突**。

**结论**：除非有特殊玩法需求（例如单机战役里"活的 AI 指挥官"，且能接受单机专用），
否则不值得。

## 1.8 第一部分的结论

1. **值得做的是 (a)：把 LLM 做成 `aimd.ini` 的生成器 + 不变量校验器。**
   唯一"零运行时风险、收益直接、且正好落在 LLM 擅长区间"的形态。
2. **(b) 是 (a) 的自然延伸**：离线生成多套人格，对局内确定性选择。
3. **(c) 不建议**：与锁步 + RNG 同步模型根本冲突。
   **特别强调 §1.3**：这不是"LLM 不确定性"的问题，
   而是"**AI 的随机性取自同步流，而该流进帧 CRC**" —— 任何改变 RNG 消耗节奏的改动都会 desync。
4. **AI 多样性瓶颈在内容量，不在算法**：原版只用了 20 个脚本动作、163 个 TeamType，
   §1.2 显示决策本身只是"加权随机抽签"。**写更多内容才是提升多样性的直接手段**，
   而 LLM 在这里的角色是**规模化内容生产**。
5. **若要突破表达力天花板**，两件事按性价比排序：
   - **新增 AI 脚本动作**（有 Phobos 先例，从 10000 起分段，安全）；
   - **新增 AITrigger 条件**（无先例，需 hook `ConditionMet 0x41E720` 并扩封闭枚举，工作量大）。
6. **一个已经存在的、最省力的落点**：Phobos 的 `IncreaseCurrentAITriggerWeight` /
   `DecreaseCurrentAITriggerWeight`（14001/14002）已经证明"**运行时调整 AI 权重**"是安全可行的。
   如果想让 LLM 的选择"在局内生效"，**改权重**比"改决策"安全得多。

---

# 第二部分：用现代格式取代 SHP / VXL

## 2.1 引擎的渲染管线（这决定了后面的一切）

**结论：这是一套纯软件、`8 位调色板索引 → 16 位 RGB565` 的光栅管线，没有任何硬件 3D 路径。**

三条互相叠加的硬约束：

| 编号 | 约束 | 证据 |
|---|---|---|
| **C1** | 所有 blitter 的源指针是 `byte*`，即**8 位调色板索引** | `YRpp/Blitters/BlitTransXlat.h:22-27`（`*dest = PaletteData[*src++]`）；89 个变体（`ConvertClass.h:60-61` 的 `Blitters[50]` + `RLEBlitters[39]`）全是这个契约 |
| **C2** | 调色板**恰好 256 项**，且 **#16–#31 被阵营 remap 占用** | `YRpp/BasicStructures.h:64-74`（`ColorStruct Entries[256]`）；`YRpp/ColorScheme.h:90` 注释明写 `remap - indices #16-#31 are changed to ... shades of BaseColor` |
| **C3** | 目标面固定 **16 位 RGB565**，`BytesPerPixel == 2` 硬编码 | `YRpp/Surface.h:149-150`（`Buffer{width*height*2}`、`BytesPerPixel = 2`）；`Drawing.h:267-276` 的 `COLOR_RED 0xF800` / `COLOR_GREEN 0x07E0` / `COLOR_BLUE 0x001F` |

**索引 0 = 透明键**（`BlitTransXlat.h:24` 的 `if (byte idx = *src++)`，0 被跳过）。

**blitter 是手写汇编**：Phobos 直接给 `gamemd.exe` 里的半透明 blitter 打补丁
（`Phobos/src/Misc/BlittersFix.cpp:8,12,23,53`，地址 `0x4987F7`/`0x498817`/`0x4985FE`/`0x4989EE`），
汇编里逐像素操作 `WORD PTR`，用 `0xF7DE` 作 RGB565 半亮掩码 —— **没有任何硬件 blit**。

## 2.2 SHP 的结构与绘制方式

`YRpp/FileFormats/SHP.h`：

- **文件头 8 字节**：`Type/Width/Height/Frames`（`Frames` 是 `short`）
- **每帧 24 字节帧头**：`Left/Top/Width/Height/Flags/Color/Offset`
- **压缩标志** = 帧头 `Flags & 2`（`SHPStruct::HasCompression`，行 55-56）
- SHP **文件里没有调色板**；调色板由绘制时传入的 `ConvertClass*` 决定

**关键点：RLE 从不被完整解成位图 —— 解码与绘制是同一趟。**
`Blitter.h:77-118` 的 `RLEBlitter::Process_Pixel_Datas` 就是 SHP 的 RLE 状态机：
`0x00 <count>` = 透明跳过，非零字节 = 一个字面索引。

绘制入口：`CC_Draw_Shape`（`0x4AED70`，`Surface.h:165-174`），
参数含 `Remap`、`Brightness`、`TintColor`、`ZShape` —— 全都在 16 位面上以软件方式施加。

## 2.3 VXL 的结构与渲染方式

`YRpp/FileFormats/VXL.h`：

- 文件头 `VoxFileHeader`（`filename[16]` + 3 个 int）
- 每段（limb）= 段头（已核实 **12 字节**）+ 段尾（已核实 **164 字节**：
  span 偏移、`Matrix3D`、`Bounds[8]`、`size_X/Y/Z`、`NormalsMode`）
- **`size_X/Y/Z` 声明为 `char`**（有符号 → 上限 127）
- 动画是 **HVA**：`Matrixes[layer + LayerCount * (frame % FrameCount)]`，
  **每（层,帧）一个刚性 4×3 矩阵**，无蒙皮、无插值（`FileFormats/HVA.h:33-37`）

**渲染器是 CPU 软件光栅化器**（证据链完整）：

- **逐法线着色查找表**：`0x753C80`/`0x753D00` 转调 `0x758670`，做 `N·L` 点积生成
  **每法线 1 字节**的着色表 `0xB45990`（背面写 0）
- **span 解码 + 逐字节写像素**：`0x757640`–`0x757714` 读 span run，
  查着色表 `0xB45990`、查调色板表 `0xB41178`，然后
  `MOV byte ptr [ECX + 0xB2FF78], DL` —— **写 8 位索引到 CPU 缓冲**
- **不涉及 D3D**：`Unsorted::bAllowDirect3D`（`0x8A0DEF`）的 xref 只在图形初始化
  （`0x6BB9A0`/`0x4BA5A0`/`0x6BB8E0`），**没有一处**在体素渲染区（`0x73Bxxx`/`0x757xxx`）
- **结果被缓存成"已光栅化的精灵"**：`VoxelCacheStruct{X,Y,Width,Height,void* Buffer}`，
  `ObjectTypeClass` 有 4 个这样的缓存

**缓存键的位宽限制**（`YRpp/Drawing.h:463-495`，已核实）：

```cpp
unsigned FrameIndex : 5;   // ← HVA 帧索引只有 5 位 = 最多 32 帧
unsigned BodyFacing : 5;   // 32 个朝向
```

`Drawing.h` 的注释逐条给出 `0x73B607` 处 key 的构建方式，含显式 `& 0x1F`（32 帧硬上限）。

## 2.4 Ares / Phobos 已有的图形能力

**对 PNG / JPG / GIF / TGA / DDS 的支持：零。** 全仓库 grep 无真实命中。

已有的能力**全部在 8 位框架内**：

| 能力 | 实现方式 | 证据 |
|---|---|---|
| PCX 替换 SHP 侧边栏 cameo | 绕过 SHP blitter，走 `PCX::Instance->BlitToSurface` | `Ares/src/Misc/Cameos.cpp:66-98`（钩 `6A980A`/`6A99F3`/`6A9A43`，固定矩形 `{TLX,TLY,60,48}`） |
| PCX 载入画面替换 SHP | 文件名含 `.pcx` 则改走 PCX | `Phobos/src/Misc/Hooks.PCX.cpp:10-53` |
| 每类型自定义 cameo 调色板 | `CameoPalette=` → `CustomPalette` → `ConvertClass` | `Ares/src/Ext/TechnoType/Body.cpp:203` |
| 解除"每类型一个额外调色板"限制 | 跳过 `0x715857` 的检查 | `Ares/src/Misc/Bugfixes.cpp:76-79` |
| AlphaImage / Insignia | 引擎固有的 `AlphaShapeClass` + 自定义 SHP | `Phobos/src/Misc/Hooks.AlphaImage.cpp`；`Ares/src/Ext/Techno/Hooks.Insignia.cpp` |
| 修 SHP blitter 精度 / RLE 缓冲溢出 | 直接改写汇编 / 换可增长缓冲 | `Phobos/src/Misc/BlittersFix.cpp`；`Ares/src/Misc/Surfaces.cpp:200-217` |

**模式很清晰**：Ares/Phobos 只在"选哪个 SHP / 哪个调色板 / 哪个 remap"上做扩展，
**从没有人尝试引入一种新的像素格式**。

## 2.5 逐项可行性

### (a) 运行时 PNG 直接当 SHP 帧 —— ⚠️ 工作量大

**不是"加个 loader"能解决的**，必须动 blitter 层：

- **路线 1（新增真彩 blitter）**：在 `ConvertClass` 里加新槽位，让 `Select*Blitter`
  认识新 flag。但 `CC_Draw_Shape` 有 15 个参数（`Remap`、`ZAdjust`、`ZGradient`、
  `Brightness 0~2000`、`TintColor`、`ZShape`、偏移…），**每一个在现有 blitter 里都是针对
  8 位索引表实现的**，真彩版本要独立实现，且 Z 读/写 × Alpha × 25/50/75 混合 × Warp × ZRemap
  的组合会爆炸。
- **路线 2（运行时量化成 256 色索引，复用现有 blitter）**：**唯一工作量可控的形态**。
  代价是每次加载都要量化，并要管理 `ConvertClass` 的生命周期（`ConvertClass::Array` 是全局数组，
  `0x89ECF8`）。
- **参照先例的局限**：Ares 的 PCX cameo 之所以能"绕过 blitter 直接贴"，是因为 cameo 是
  **固定 60×48 矩形、无 Z、无 remap、无 tint**。战场上的 SHP 帧不具备这些简化条件。
- **remap 的干扰**：真彩图像没有"索引"可供 remap。要实现阵营配色，
  必须在量化阶段**把图像的某些颜色强制映射到索引 #16–#31** ——
  这把问题从"技术"变成了"**美术约定**"（画师必须按阵营色通道作画）。

### (b) 运行时 GIF —— ⚠️ 比 (a) 更差

在 (a) 的全部问题之上：

- **SHP 没有"全局帧延迟"概念**。SHP 只是静态帧序列，播放节奏完全由使用它的游戏逻辑决定
  （`WalkedFramesSoFar % HVA->FrameCount`、建筑按 `Frames/2` 算 buildup 阶段、
  伊文炸弹按剩余寿命算帧号）。**GIF 的每帧 delay 在这套架构里无处存放** ——
  要尊重它就得改掉每个消费 SHP 的类，而不是改加载器。
- **帧数与语义强耦合**：建筑用 `Frames/2`、步兵按 `facings × 每 facing 帧数`、
  动画类有自己的 `End`/`Loop`。作为对照，体素路径把帧数明确钉死在 5 bit
  （`Drawing.h:468`）—— 说明"帧数位宽"在这套引擎里是**被到处硬编码的设计决策**。

### (c) 运行时 GLB 渲染 —— ❌ 工程上不可行

**这不是工作量问题，而是"引擎里不存在对应子系统"的问题。**

| 需要的组件 | 引擎现状 | 差距 |
|---|---|---|
| 三角面光栅化 | 只有体素 span 光栅器（`0x757640+`，写 8 位索引到 `0xB2FF78`） | 要从零实现 + 深度测试 + 透视校正 |
| 纹理/材质采样 | 只有逐法线单字节着色表 `0xB45990` | 无 UV、无纹理、无 mipmap |
| 骨骼动画 | 只有 HVA：每（层,帧）一个刚性矩阵 | 无蒙皮权重、无层级、无插值 |
| 输出格式 | 8 位索引 → `ConvertClass` → RGB565（`BytesPerPixel=2` 硬编码） | 真彩结果无论如何要降到 256 色 + 565 |
| 缓存键 | 32 位打包，帧索引仅 **5 bit** | 任意网格/帧数装不进键空间 |
| 图形 API | DirectDraw7；D3D **只被探测不用于渲染** | 没有可用的硬件 3D 管线 |

唯一的理论出路是"离屏 3D + 每帧量化"：另开真彩离屏面渲染 GLB，再降到 8 位贴进游戏面。
但这需要在 DDraw7 之上自建 D3D 设备，并且**每帧、每单位、每朝向**做一次
真彩→256 色量化，还要把结果塞进 32 位 `VoxelIndexKey` 缓存。

**这也解释了为什么 20 年来社区只做 VXL 编辑器和更高分辨率的 VXL，从没人做外部网格格式。**

### (d) 离线 PNG/GIF → SHP —— ✅ 已是社区标准做法

- 转换器只需产出 §2.2 的结构：8 字节头 + N×24 字节帧头 + RLE 数据。
- 必须处理：量化到 ≤256 色；**保留 #16–#31 给 remap**；索引 0 = 透明；最终落 RGB565。
- GIF 的每帧 delay **必须由调用方的 INI 参数还原**（社区工具事实上就是这么要求作者手填 rate 的）。
- ⚠️ **未确定**：本机没有社区转换工具（XCC Mixer / OS SHP Builder 等），
  未能实测其量化/抖动算法。

### (e) 离线 GLB → VXL —— ⚠️ 可行但有硬上限

- 目标格式已完整可见（段头/段尾/span 编码 + HVA 矩阵）。
- **必须自写转换器**（社区无现成工具）：网格体素化、每段法线生成、span 压缩、
  以及把骨骼动画烘成每帧每层的刚性矩阵 —— **必然丢失平滑蒙皮**。
- 硬上限：**每段尺寸 `char` → 上限 127（⚠️ 未确认引擎是否按无符号解释，可能 255）**；
  **HVA 帧数 ≤ 32**（5 bit 缓存键）；朝向 ≤ 32。
- 渲染端限制仍在：最终走软件 span 光栅器 → 8 位索引 → 16 位。
  **GLB 的材质、法线贴图、UV 纹理全部无效。**

## 2.6 第二部分的结论

1. **"引擎直接读现代格式"这条路，(c) 不可行，(a)(b) 代价远大于收益。**
   根因是引擎的**输出契约是 8 位索引 + 256 色调色板 + 16 位 RGB565**，
   而不是"缺少一个解码器"。
2. **"现代格式当创作格式、构建期转换"（(d)/(e)）才是正确姿势**，
   而且 (d) 已经是社区标准做法。
3. **如果目标是"让美术工作流现代化"**，性价比最高的投入是：
   **做一个可靠的 PNG 序列 → SHP 转换器（带 remap 感知的量化）**，
   而不是改引擎。这能立刻把画师从 SHP 编辑器里解放出来。
4. **如果目标是"更高精度的载具模型"**，现实可行的仍是
   **提高 VXL 分辨率**（注意每段尺寸上限）与面对 **HVA 32 帧上限**，
   而不是引入网格格式。

---

## 附录 A：证据索引

### 第一部分

| 事实 | 证据 |
|---|---|
| `aimd.ini` 138,538 B / 7,400 行 / 388 section；TaskForces 132 / TeamTypes 163 / ScriptTypes 88 / AITriggerTypes 165 / 定义 section 383 | `D:\Games\Ra2\aimd.ini` 实测统计 |
| AIMD.INI 读入 `CCINIClass::INI_AI`，`bDigest=1` | `gamemd.exe` `0x52D357`–`0x52D378`；`YRpp/CCINIClass.h` `INI_AI = 0x887128` |
| AI 决策唯一收口 `FUN_006f0ab0`；`ConditionMet 0x41E720` 只有它一个调用者；`CreateTeam 0x6F09C0` | 反汇编 + Ghidra xref |
| 概率门控用 `ScenarioClass::Random`（偏移 0x218） | `0x6F0AF2`–`0x6F0B01` 指令；`YRpp/ScenarioClass.h:137` |
| AI 每次评估消耗 2 次同步 RNG | 同上（门控 + 加权抽签各一次），`RandomRanged = 0x65C7E0` |
| **RNG 抽样值折进帧 CRC** | `Ares/src/Misc/Checksum.cpp:46`；`Phobos/src/Misc/SyncLogging.cpp:675` |
| **RNG 内部索引被当作同步状态比对** | `Phobos/src/Misc/SyncLogging.h:146-178`、`SyncLogging.cpp:39-40` |
| 逐帧 CRC 锁步 | `YRpp/EventClass.h:24-34`；`CurrentFrameCRC 0xAC51FC`、`LatestFramesCRC[256] 0xB04474`、`Execute_DoList 0x64CC68` |
| out-of-sync 字符串 | `TXT_OUT_OF_SYNC 0x837DE8`、`FRAMESYNC 0x820AF0`、`SYNC%01d.TXT 0x838EB0`、`Frame sync stalls 0x81D88C` |
| 网络时序全局 | `YRpp/Unsorted.h:80-93` |
| AI 状态动态字段 | `YRpp/AITriggerTypeClass.h:183-196`（`Weight_Current` 等）；`RegisterSuccess 0x41FD60` / `RegisterFailure 0x41FE20` |
| AI 状态进存档 | 六个类均声明 `IPersistStream`；`Phobos/src/Ext/Team/Body.cpp:9-37` 扩展序列化 |
| AITrigger 18 字段格式 / ConditionString 64 hex | `YRpp/AITriggerTypeClass.h:126-155,16-24` |
| 结构上限 `Entries[6]` / `ScriptActions[50]` | `YRpp/TaskForceClass.h` / `ScriptTypeClass.h` |
| AI 脚本动作**可扩展** | `Phobos/src/Ext/Script/Body.h`（`PhobosScripts` 10000–18999 分段） |
| AITrigger **条件封闭在 8 值** | `YRpp/GeneralDefinitions.h:570-588`（`AITriggerCondition` / `AITriggerHouseType`） |
| TAction/TEvent 分发可扩展（跳表到 145） | `Ares/src/Ext/TAction/Hooks.cpp:27-29`；`Ares.dll.inj` 的 `6E3EE0`/`6E3B60`/`6DD8B7` |
| 运行时改 AI 权重已打通 | `Phobos/src/Ext/Script/Body.cpp:526-595`（动作 14001/14002） |
| 数组全局地址 | `AITriggerTypeClass::Array 0xA8B200`、`TeamTypeClass::Array 0xA8ECA0`、`TaskForceClass::Array 0xA8E8D0`、`ScriptTypeClass::Array 0x8B41C8`、`TeamClass::Array 0x8B40E8` |

### 第二部分

| 事实 | 证据 |
|---|---|
| 目标面 16 位、`BytesPerPixel=2` 硬编码 | `YRpp/Surface.h:149-150` |
| blitter 源是 8 位索引 | `YRpp/Blitters/BlitTransXlat.h:22-27`、`BlitPlainXlat.h:22-23`、`BlitTransRemapXlat.h:23-28` |
| 89 个 blitter 变体 | `YRpp/ConvertClass.h:60-61` |
| 调色板 256 项 | `YRpp/BasicStructures.h:64-74` |
| **remap = #16–#31** | `YRpp/ColorScheme.h:90` |
| RGB565 常量 | `YRpp/Drawing.h:267-276` |
| SHP 结构 + RLE 状态机 | `YRpp/FileFormats/SHP.h:70-110`；`YRpp/Blitters/Blitter.h:45-118` |
| SHP 绘制入口 | `CC_Draw_Shape 0x4AED70`（`YRpp/Surface.h:165-174`） |
| VXL 段头 12 B / 段尾 164 B | `VoxLib::leaSectionTailer 0x7564B0` 反编译；`YRpp/FileFormats/VXL.h:55-96` |
| VXL 每段尺寸是 `char` | `YRpp/FileFormats/VXL.h:78-80,92-94` |
| HVA = 每（层,帧）刚性矩阵 | `YRpp/FileFormats/HVA.h:33-37` |
| 体素着色 LUT `0xB45990` | `0x758670` 反编译（`N·L` 点积，背面写 0） |
| 体素 span 写 8 位索引 | `0x757640`–`0x757714` 反汇编 |
| D3D 不参与渲染 | `bAllowDirect3D 0x8A0DEF` 的 xref 全在初始化 |
| 体素帧索引 5 bit = 32 帧上限 | `YRpp/Drawing.h:463-495` |
| blitter 是手写汇编 | `Phobos/src/Misc/BlittersFix.cpp:8,12,23,53` |
| Ares/Phobos 无 PNG/GIF/TGA/DDS 支持 | 全仓库 grep 无真实命中 |

## 附录 B：未确定事项

**第一部分**

1. **`aimd.ini` 的 `[Digest]` 是否被强制校验**（计算并比对），还是仅解析。
   已确认读取时 `bDigest=1`；未确认后续比对与失败后果。
2. **`HouseClass+0x565C` 的字段名与确切语义**（按 0..100 使用；Ares/Phobos 源码零引用）。
3. **`HouseClass::Update` 的入口地址与调用者** —— Ghidra 拒绝在 `0x4F8A6A` 定位函数，
   故"每帧每 house 一次"由 hook 名聚簇推断，未直接观测。
4. **引擎 `AITriggerTypeClass::Save/Load` 实体与存档字节中 `Weight_Current` 的直接观测**。
5. **`ra2md.mix` 内 `aimd.ini` 的字节级内容**（MIX 索引为加密形式，无明文名）。

**第二部分**

6. **`VoxelSectionTailer::size_X/Y/Z` 的有符号性**（127 还是 255 上限），需实测大尺寸 VXL。
7. **社区 PNG→SHP / GLB→VXL 工具的具体量化算法**（本机无这些工具，未实测）。
8. **SHP 帧数是否存在引擎级硬上限**（`Frames` 是 `short`，未找到显式检查；
   实际限制来自各消费类的逻辑假设）。
9. **`ConvertClass::FullColorData` 的确切布局**（源码注释 `ShadeCount * 8 * BytesPerPixel`
   在 `ShadeCount=53`/`BytesPerPixel=2` 下算得 848 字节，不足以容纳 256 项 × 多档，
   **注释可能有误**；未进一步反汇编 `0x48E740` 定论）。
10. **调色板 #0–#15 是否有保留语义**（只核实了 #0 是透明键、#16–#31 是 remap）。
