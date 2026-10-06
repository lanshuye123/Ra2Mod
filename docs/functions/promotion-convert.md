# 功能：升级换单位（Promote.VeteranType / Promote.EliteType）

> 通用方法见 [`../DEVELOPMENT.md`](../DEVELOPMENT.md) 的 §5.9（调用约定）、§5.10（与 Ares 共存：为什么改 Ares 内部的调用点）、§5.11（定位 Ares 内部函数）。

| 项目 | 值 |
|---|---|
| 状态 | ✅ **实机验证通过**：一星/三星换单位、血量与星级策略均在自动对局与玩家实测中生效，见 §5 |
| 依赖 | **Ares 3.0**（换单位本体就是 Ares 的 `ConvertTypeTo`；PE TimeDateStamp `0x5fc37ef6`） |
| 源码 | `src\Misc\PromoteConvert.h`、`src\Misc\PromoteConvert.cpp`、`src\Misc\AresHelpers.h/.cpp` |
| 引入版本 | HAres v0.1.0.0 |

---

## 0. 先说清楚：Ares 3.0 已经自带这个功能的一半

这是本次开发最重要的发现，直接决定了实现方式。**Ares 3.0 里已经存在"升级换单位"**，键名和语义如下（在 Ares.dll 的字符串表里可以直接看到：`1009dca0 Promote.VeteranType`、`1009dcb4 Promote.EliteType`、`1009dcc8 Promote.VeteranExperience`、`1009dce4 Promote.EliteExperience`，以及 `Promote.VeteranSound/EliteSound/VeteranFlash/EliteFlash`、`EVA.VeteranPromoted/ElitePromoted`、`Promote.IncludePassengers`）：

```ini
[MTNK]
Promote.VeteranType=MTNK_V        ; 到一星（老兵）时变成 MTNK_V
Promote.EliteType=MTNK_E          ; 到三星（精英）时变成 MTNK_E
Promote.VeteranExperience=0.25    ; 换完之后再加这么多经验
```

Ares 的实现就在它自己的 `TechnoClass_Update_Veterancy` 钩子里：Ares 把引擎那段"等级变化"整段替换掉了，然后调用自己的

```
void __stdcall AresRankUpdate(TechnoClass*, bool, bool)   ; Ares.dll+0x46AF0
```

该函数用 `Veterancy`（TechnoClass+0x150，浮点：0=新兵 1=老兵 2=精英）算出当前等级，与缓存的 `CurrentRanking`（TechnoClass+0x13C）比较，**变了就执行升级该做的事**，其中就包括：若该类型配了 `Promote.VeteranType` / `Promote.EliteType`，调用 `ConvertTypeTo`（Ares.dll+0x43650）把单位换成新类型，然后再按 `Promote.*Experience` 调整经验。

所以 HAres 这个功能**不重复实现一星/三星的换型**，而是：

1. 读**同样的键**（`Promote.VeteranType` / `Promote.EliteType`），modder 已有的配置继续有效；
2. 在 Ares 换完之后**再套一层 HAres 的策略**：血量是否重置、星级是否继承（Ares 只会"保百分比血量 + 保留经验"）；
3. 用"改 Ares 内部调用点"的方式拿到**准确的升级瞬间**（§2.3）。

---

## 1. 功能说明

### 1.1 全部键

```ini
[MTNK]
; ---- 触发时机与目标单位（前两个是 Ares 原生键，后两个是 HAres 新增）----
Promote.VeteranType=MTNK_V        ; 到一星（老兵）时 → MTNK_V
Promote.EliteType=MTNK_E          ; 到三星（精英）时 → MTNK_E

; ---- 换完之后的状态（HAres 新增）----
Promote.KeepHealth=yes            ; yes=保留当前血量百分比（与 Ares 行为一致）；no=按新单位满血
Promote.KeepVeterancy=yes         ; yes=保留触发时的星级；no=换完回到新兵
```

### 1.2 两个触发时机

| 触发 | 条件 | 目标键 |
|---|---|---|
| 一星 | 星级从低变到 `老兵(1.0)` | `Promote.VeteranType` |
| 三星 | 星级从低变到 `精英(2.0)` | `Promote.EliteType` |

两个键互相独立，也可以只配一个。判断用的是引擎/Ares 自己那个"已生效等级"缓存（`CurrentRanking`），所以**任何**让等级上升的原因都会触发：打怪升级、Ares 的 `DropPod.Veterancy`、以及本工程的[范围升星超武](promote-aura-superweapon.md)。

### 1.3 换完之后的状态

| `Promote.KeepHealth` | 结果 |
|---|---|
| `yes`（默认） | 按**当前血量百分比**换算到新单位（Ares `ConvertTypeTo` 的原生行为，也是 modder 最熟悉的） |
| `no` | 换完立刻满血：`SetHealthPercentage(1.0)`，并同步 `EstimatedHealth` |

| `Promote.KeepVeterancy` | 结果 |
|---|---|
| `yes`（默认） | 保持触发时的星级（一星触发就还是一星，三星触发就还是三星），Ares 的 `Promote.*Experience` 也不会被覆盖 |
| `no` | 换完回到新兵（`Veterancy = 0.0`），同时把 `CurrentRanking` 同步成新兵，避免下一帧被当成"降级"再处理一次 |

日志会把这次换算的两个数都写出来，便于核对：

```
[PromoteConvert] E2 -> BORIS (KeepHealth=1 KeepVeterancy=0: health 62% -> 62%, rank 0)
[PromoteConvert] HTNK -> MTNK (KeepHealth=0 KeepVeterancy=0: health 40% -> 100%, rank 0)
```

### 1.4 适用类型

只支持 **步兵 / 车辆 / 飞机**（`InfantryType` / `UnitType` / `AircraftType`），且源类型与目标类型必须**同类**（步兵→步兵、车辆→车辆、飞机→飞机）。这是引擎与 Ares `ConvertTypeTo` 的能力边界：换型只改类型指针，游戏对象本身（`UnitClass` / `InfantryClass` / `AircraftClass`）不变。配错会在 `HAres.log` 里报出来并被忽略：

```
[PromoteConvert] MTNK: Promote.VeteranType names unknown type "HVR"
```

另外，单位在运输工具里（`TechnoClass::Transporter != nullptr`）或离场（`InLimbo`）时会跳过换型，日志会记录原因。

### 1.5 曾经考虑、最终砍掉：三星之后再升级（ElitePlus）

最初的设计里还有第三个键 `Promote.ElitePlusType`（"三星之后再满足升级条件就换"）+ `Promote.ElitePlusExperience`（再攒多少经验）。实机验证时发现**这条路在当前引擎/Ares 上不可能可靠成立**，用户决定砍掉，代码与文档已移除。结论留在这里，免得以后有人重新踩：

* 引擎把 `Veterancy` 夹在 `[General] VeteranCap` 上，而**默认值正好是 2.0**（`rulesmd.ini` 里那句注释就是 "maximum veteran level that can be obtained"）。
* 实测把 `VeteranCap` 改成 `4.0`（日志确认游戏里读到的就是 `4.00`）后，**精英单位的经验仍然钉在 2.00**：Ares 在击杀给经验的路径上不会让已经是精英的单位继续涨（`Hooks.Veterancy.cpp` 里多处 `!Veterancy.IsElite()` 分流，经验会转给乘客/炮手之类）。
  ```
  [PromoteConvert] E2 elite experience is now 2.00 (threshold 2.25, [General] VeteranCap 4.00)
  ```
* 于是"等经验涨到 2.0+X"这条判据在默认配置下永远不成立。改成"精英后再击杀一次就换"（钩 `0x702FF5`，Ares 替换 veterancy 代码后的返回落点）技术上可行，但那已经不是"再攒 X 点经验"的语义，而且需要额外改动；**需求已砍**，代码里不再包含。

---

## 2. 原理

### 2.1 为什么不能像原计划那样 hook `0x6FA07A`

引擎自己的"等级变化"分支在 `0x6FA054`（Ares 的 hook 点）到 `0x6FA149` 之间，Phobos 在没有 Ares 时会 hook `0x6FA07A`（`TechnoClass_AI_PromoteAnim`，注释原文 "in cases where Ares is not available"）。但实测反编译 **Ares 3.0 的 `TechnoClass_Update_Veterancy` 处理函数**：

```c
// Ares.dll 导出 TechnoClass_Update_Veterancy (0x1004F2E0)
FUN_10046af0(*(int **)(param_1 + 0xc), '\0', '\x01');   // param_1 是 REGISTERS*，+0xC 就是 ESI
return 0x6fa14b;
```

它**返回非零地址**，即直接跳到 `0x6FA14B` 继续。Syringe 的规则是"非零返回值立刻截断链"，所以：

* 引擎那段 `0x6FA054..0x6FA149` 在装了 Ares 时**根本不会执行**；
* 挂在 `0x6FA054` 或 `0x6FA07A` 的 HAres hook **永远不会被调用**。

这正是 `DEVELOPMENT.md` §5.10 表格里 Ares "整体替换"的那一类。

### 2.2 降级方案：hook `0x6FA14B`（本功能最终没有用它）

`0x6FA14B` 是 Ares 跳回来的落点，也是引擎原路径的公共汇合点（等级没变时 `JZ 0x6FA14B` 直接过来），所在函数是 `TechnoClass::AI`（入口 `0x6F9E50`）。它适合做"每帧每单位"的检查（[心控屏蔽区间](mind-control-shield.md) 就挂在这里），但**不适合做"等级刚刚变化"的判定**：Ares 已经在这一帧把 `CurrentRanking` 更新成新值了，到这里前后状态分不出来。

### 2.3 换型瞬间的观测点：改 Ares 的内部调用点

`AresRankUpdate`（Ares+0x46AF0）在 Ares 里只有三个调用点（Ghidra 交叉引用，已对照本机 Ares.dll 核实）：

| 调用点 RVA | 谁在调 | 场景 |
|---|---|---|
| `0x0004F2CF` | `TechnoClass_RegisterDestruction_Veterancy` | 击杀给经验后的即时升级（例如炮手/乘客） |
| `0x0004F2EB` | `TechnoClass_Update_Veterancy` | 每帧每单位的常规升级 |
| `0x00046B94` | `AresRankUpdate` 自己 | 载具升级时递归提升乘客 |

把这三处 `E8 rel32` 改成指向 HAres 的包装函数：

```cpp
void __stdcall RankUpdate_Hook(TechnoClass* pThis, bool silent, bool playEffects)
{
    const Rank before = pThis->CurrentRanking;                     // 引擎缓存的"上一等级"，不是 Veterancy！
    TechnoTypeClass* const pTypeBefore = pThis->GetTechnoType();   // Ares 可能会换掉类型

    AresRankUpdate(pThis, silent, playEffects);                    // 原函数照跑

    const Rank after = pThis->Veterancy.GetRemainingLevel();
    if (before != Rank::Invalid && after != before)
        OnRankChanged(pThis, pTypeBefore, after);                  // 规则查的是"换之前"的类型
}
```

> ⚠️ **"升级前等级"必须取 `CurrentRanking`，不能用 `Veterancy.GetRemainingLevel()`。** 这是实测踩出来的坑：`Veterancy` 是个浮点经验值，任何外部逻辑都可能在本帧提前把它改掉 —— 最典型的就是本工程的[范围升星超武](promote-aura-superweapon.md)，它直接写 `Veterancy`，等 Ares 处理时"前后"就已经相同了，于是换单位静默失效（实机日志里只看到注册成功却永远等不到 `MTNK -> HTNK`）。`CurrentRanking`（`TechnoClass+0x13C`）才是引擎/Ares 自己用来做这个比较的"已生效等级"缓存：
>
> ```c
> // 引擎 0x6FA054 与 Ares 的 FUN_10046af0 是同一套判断
> old = pThis->CurrentRanking;                 // MOV EDI,[ESI+0x13C]
> now = pThis->Veterancy.GetRemainingLevel();  // CALL 0x750030
> if (old == now) return;                      // 没变 → 什么都不做
> if (old == Rank::Invalid) goto only_cache;   // 新单位第一次 → 只缓存，不播升级效果
> ... 应用等级效果 / Ares 在这里顺便换单位 ...
> pThis->CurrentRanking = now;                 // MOV [ESI+0x13C],EAX
> ```
>
> `Rank::Invalid`（-1）表示这个单位还没有生效过等级（刚造出来），Ares 对这种情况是"只缓存、不播升级效果"，HAres 也照此跳过，避免把"出生即老兵/精英"的单位当成刚升级而换掉。

好处：

* **判断一定准**：用的是引擎/Ares 自己那个"已生效等级"缓存，外部（超武、Ares 的 `DropPod.Veterancy` 等）直接写 `Veterancy` 造成的升级同样能被抓到；
* **不打断 Ares**：原函数完整执行，Ares 的升级音效、闪光、EVA、`Promote.*Experience`、以及它自己的换型都照旧；
* **不抢 Phobos 的补丁**：Phobos 改的是 `ConvertTypeTo` 的另外 8 个调用点（`Ares+0x39DAE / 0x46C6D / …`），与这三处不重叠；
* 因为调用约定是 `__stdcall`（函数尾部是 `RET 0xc`，已反汇编核实），包装函数写成 `__stdcall (TechnoClass*, bool, bool)` 即可，不需要自己造 trampoline。

于是"Ares 先换、HAres 再套策略"分层很清楚：如果 Ares 已经按它自己的键把单位换掉了，包装函数里 `pThis->GetTechnoType() == 目标类型`，HAres 就**只套策略**（满血/星级），不会再换一次。

### 2.4 换单位本体：Ares 的 `ConvertTypeTo`

`bool __stdcall ConvertTypeTo(TechnoClass*, TechnoTypeClass*)`，RVA 由 Phobos 的版本表给出（`Phobos\src\Utilities\AresAddressInit.cpp`）：

| Ares 版本 | PE TimeDateStamp | `ConvertTypeTo` RVA |
|---|---|---|
| 3.0 | `0x5fc37ef6` | `0x00043650` |
| 3.0p1 | `0x61daa114` | `0x00044130` |

它做的事（Ghidra 反编译 + Phobos 无 Ares 兜底路径对照）：校验两个类型同类 → 挂起 CLEG 锁定 → `RegisterLoss`/`RemoveTracking` → **换类型指针** → 按百分比重算血量（`SetHealthPercentage` @ `0x5F5C80`）→ 同步 `EstimatedHealth` → 恢复跟踪/`RegisterGain`/`RecheckTechTree` → 夹弹药 → 重设 `PrimaryFacing`/`SecondaryFacing` 的 ROT → 必要时替换 locomotor → `RecalculateStats`。

**注意它保留血量百分比、且完全不动 `Veterancy`** —— 这就是为什么"血量是否重置""星级是否继承"必须在它之后补。

---

## 3. 实现

```
src\Misc\AresHelpers.h/.cpp     Ares 模块 / 版本 / ConvertTypeTo 的解析（可复用）
src\Misc\PromoteConvert.h/.cpp  Promote.* 规则表 + Ares 调用点包装
src\Misc\SharedUtils.h          房子过滤、格→缇换算、INI 小工具
```

注意：**本功能不注册任何 Syringe hook**，它改的是 Ares.dll 里的三处 `CALL`（运行期 `Patch::Apply_CALL`），所以 `.syhks00` 段里没有它的条目。

流程：

1. `HAres::ExeRun()` → `AresHelpers::Init()`（读 Ares.dll 与 TimeDateStamp）→ `PromoteConvert::InitAresIntegration()`（仅 Ares 3.0，改写三个调用点）。
2. `ScenarioClass::Start`（或首次用到时惰性建表）→ `BuildRegistry()`：遍历 `TechnoTypeClass::Array`，读 `Promote.*`，做未知类型/同类/支持类型的校验与日志，存进 `unordered_map<const TechnoTypeClass*, Rule>`。
3. 单位升级 → Ares 调用被包装 → `OnRankChanged()` 用**升级前的类型**查表 → `Convert()`：
   `AresHelpers::GetConvertTypeTo()(pThis, pToType)` → `ApplyPolicy()`（满血/星级）→ 写日志。

---

## 4. 关键地址与偏移

| 地址 | 类型 | 用途 | 现场 / 说明 |
|---|---|---|---|
| `Ares+0x46AF0` | 调用（不 hook） | Ares 的等级更新函数 | `void __stdcall (TechnoClass*, bool, bool)`，末尾 `RET 0xc` |
| `Ares+0x4F2CF` | `Apply_CALL` | 击杀即时升级路径 | `TechnoClass_RegisterDestruction_Veterancy` 内的调用点 |
| `Ares+0x4F2EB` | `Apply_CALL` | 每帧常规升级路径 | `TechnoClass_Update_Veterancy`（Ares 的 0x6FA054 钩子）内的调用点 |
| `Ares+0x46B94` | `Apply_CALL` | 乘客递归升级路径 | `AresRankUpdate` 内部对自己的调用 |
| `Ares+0x43650` | 调用 | `ConvertTypeTo`（换单位本体） | `bool __stdcall (TechnoClass*, TechnoTypeClass*)` |

**字段（YRpp 名字 ↔ 实证偏移）**：

| 字段 | 偏移 | 证据 |
|---|---|---|
| `TechnoClass::Veterancy`（float，0/1/2） | `0x150` | `0x6FA05A LEA EBX,[ESI+0x150]`；Ares `FUN_10046af0` 里 `param_1[0x54]`（0x150）就是它 |
| `TechnoClass::CurrentRanking` | `0x13C` | `0x6FA054 MOV EDI,[ESI+0x13c]`、`0x6FA145 MOV [ESI+0x13c],EAX`；Ares `FUN_10046af0` 里 `param_1[0x4f]` |
| `ObjectClass::Health` / `EstimatedHealth` | `0x6C` / `0x70` | 引擎 `SetHealthPercentage` @`0x5F5C80` 的 `FILD [ESI+0x6C]`；`ObjectTypeClass::Strength` 由 `[type+0xA0]` 读取 |

**怎么重新推导**（换 Ares 版本时）：

1. Ares 的 hook 地址 → `D:\Games\Ra2\Ares.dll.inj`（或 Ares.dll 自己的 `.syhks00` 段）拿到 `6FA054 = TechnoClass_Update_Veterancy, 6`；
2. `dumpbin /exports Ares.dll | findstr Veterancy` → 得到处理函数 RVA（本机 `0x4F2E0`）；
3. 在 Ghidra 里按"映像基址 `0x10000000` + RVA"定位（**不是**按住 RVA 找，见 DEVELOPMENT.md §8 最后一行），反编译即可看到它调用 `0x46AF0` 并 `return 0x6fa14b`；
4. 对 `0x46AF0` 做 `list_xrefs` 得到全部调用点，再确认每个调用点是 `E8 rel32`；
5. `ConvertTypeTo` 的 RVA 直接抄 Phobos 的 `AresAddressInit.cpp`（同一个 Ares 版本、同一张表）。

---

## 5. 实测验证

### 5.1 编译

Debug / Release 均通过、零警告。`.syhks00` 为 **0x50 = 80 字节 = 8 个 hook**（原有 5 个 + 范围升星超武 1 个 + 心控屏蔽 2 个）；本功能不占 hook 条目。

### 5.2 Ares 调用点补丁

```
[Ares] detected Ares 3.0 at 0x5C620000 (ConvertTypeTo at 0x5C663650)
[PromoteConvert] redirected Ares rank update call at Ares+0x4F2CF
[PromoteConvert] redirected Ares rank update call at Ares+0x4F2EB
[PromoteConvert] redirected Ares rank update call at Ares+0x46B94
[PromoteConvert] Ares 3.0 integration ACTIVE (3 call site(s) patched)
```

注意 `0x5C620000` 是 Ares.dll 的**运行期基址**（ASLR），不是它的首选基址 `0x10000000` —— HAres 一律用 `GetModuleHandleA` 得到的基址加 RVA，所以不受影响。

### 5.3 规则表与校验

```
[PromoteConvert] MTNK: Veteran=HTNK Elite=<none> KeepHealth=0 KeepVeterancy=0
[PromoteConvert] E2: Veteran=<none> Elite=BORIS KeepHealth=1 KeepVeterancy=0
[PromoteConvert] MTNK: Promote.VeteranType names unknown type "HVR"      ← 写错类型名时的校验
```

### 5.4 观察到的等级变化（包装函数确实在跑）

```
[PromoteConvert] HARV changed rank 2 -> 1 (rule for this type: no)
[PromoteConvert] E2 changed rank 2 -> 1 (rule for this type: yes)
[PromoteConvert] E2 changed rank 1 -> 0 (rule for this type: yes)
```

`2 -> 1` 是 新兵→老兵，`1 -> 0` 是 老兵→精英（`Rank` 枚举：`Elite=0, Veteran=1, Rookie=2`）；括号里说明该类型有没有配规则，便于判断"为什么没换"。

### 5.5 调试用配置（已按用户要求挂在游戏里）

用户要求：**苏军基地挂载升星超武；动员兵三星变鲍里斯，不保留等级但保留血量。**
单位/建筑/超武的定义只能写在 `rulesmd.ini` 一侧（`Ra2md.ini` 是客户端配置，只有 `[Options]`/`[Video]`/`[Skirmish]` 这些段）。本 mod 的扩展 INI 是 `Add.ini` → `AddCN.ini`（`rulesmd.ini` 顶部 `[#include]` 引入），它自己就是这么挂超武的（`[SuperWeaponTypes] + = AngrnessOfCN`），所以调试配置写在 **`AddCN.ini` 末尾**（带 `HAres debug setup` 注释段，删掉即还原）：

```ini
[SuperWeaponTypes]
+ = PromoteAuraSpecial

[PromoteAuraSpecial]
UIName=Name:Iron
Name=Promote Aura
IsPowered=false
RechargeTime=0.5
Type=PromoteAura          ; ← HAres 识别的新超武类型
Action=Custom             ; ← 让 Ares 允许它选点发射
SidebarImage=IRCRICON
ShowTimer=yes
DisableableFromShell=no
Range=8
PromoteAura.Range=8
PromoteAura.Levels=1      ; 正数升星；改成 -1 就是降星
PromoteAura.Houses=owner
PromoteAura.AffectBuildings=no
PromoteAura.AffectAircraft=yes
PromoteAura.MaxTargets=0

; 苏军基地（苏联建造厂）提供这个超武
[NACNST]
SuperWeapon=PromoteAuraSpecial

; 动员兵：三星(精英) → 鲍里斯；不继承等级、保留血量
[E2]
Promote.EliteType=BORIS
Promote.KeepVeterancy=no
Promote.KeepHealth=yes
```

原文件已备份为 `D:\Games\Ra2\AddCN.ini.hares_backup`；`rulesmd.ini` 也已备份（`rulesmd.ini.hares_backup`），且**当前没有对 `rulesmd.ini` 做任何修改**（期间为试验 ElitePlus 临时改过 `VeteranCap`，砍掉该需求后已还原为原值 `2`）。

### 5.6 实机结果

* **犀牛(HTNK) ↔ 灰熊(MTNK) 互换**：玩家实测正常 ✅（`Promote.VeteranType=HTNK` / `=MTNK`）。
* **动员兵(E2) 三星 → 鲍里斯(BORIS)**：玩家实测正常 ✅，HAres 的日志把最后一步（策略）也记录了下来：

  ```
  [PromoteConvert] E2 changed rank 2 -> 1 (rule for this type: yes)     ← 新兵→老兵
  [PromoteConvert] E2 changed rank 1 -> 0 (rule for this type: yes)     ← 老兵→精英
  [PromoteConvert] E2 was converted into BORIS by Ares - policy applied
      (KeepHealth=1 KeepVeterancy=0: health 100% -> 100%, rank 0 -> 2)  ← 换完回到新兵
  ```

  `rank 0 -> 2` 就是 `Elite -> Rookie`，正是 `Promote.KeepVeterancy=no` 的效果；`KeepHealth=1` 则保持血量百分比。

> **重要：实际游戏里"换型"这一步通常是 Ares 做的。** 因为 Ares 读**同样**的两个键（§0），它会在自己的 veterancy 处理里先把单位换掉；HAres 的包装函数随后发现"类型已经是目标类型"，于是**只套策略**并打上上面这条日志。只有"Ares 没配、只有 HAres 配"的情况才会走 HAres 自己调 `ConvertTypeTo` 的分支（日志为 `X -> Y (KeepHealth=... : health ...% -> ...%, rank ...)`）。
> 这也解释了为什么"注册成功但看不到 `X -> Y`"并不代表功能失效 —— 先看有没有 `policy applied` 那条。

* 期间修掉两个真 bug（都靠实测日志抓到）：`CurrentRanking` 判据（§2.3）、以及心控屏蔽释放日志里的 use-after-free（见 [心控屏蔽区间](mind-control-shield.md) §5）。
* "三星之后再升级"（ElitePlus）在实测中被证明无法可靠成立，需求已砍（§1.5），相关代码与 `rulesmd.ini` 的 `VeteranCap` 改动都已还原。

---

## 6. 已知限制

* **必须 Ares 3.0**：换单位本体是 Ares 的 `ConvertTypeTo`；HAres 没有（也不打算）复刻 Phobos 那套 60 行的无 Ares 兜底换型。
* **只映射了 Ares 3.0 的调用点 RVA**：Ares 3.0p1 会用 `ConvertTypeTo` 的新 RVA，但三个 veterancy 调用点的 RVA 未映射，此时 HAres 会**明确记一条日志并整体停用**本功能（不会冒险改错地址）。
* **不做"三星之后再升级"**（需求已砍，见 §1.5）：引擎默认 `VeteranCap=2`，精英单位的经验不再增长，做精确的"再攒 X 点经验"需要额外机制。
* 不支持建筑换型（引擎类型族限制），也不迁移乘客：换型时如果单位里有乘客，乘客留在原对象上继续跟随（与 Ares/Phobos 行为一致）。
* `Promote.KeepVeterancy=yes` 时**不会**覆盖 Ares 的 `Promote.*Experience`（如果 Ares 已经换过型）。
* 光环升星（[范围升/降星超武](promote-aura-superweapon.md)）同样会触发换型，这是设计意图，不是 bug。
* 击杀的"经验归属"由 Ares 决定（可能记给运输车/炮手/乘客）；HAres 只按**升级的那个单位**判断规则，与 Ares 的分流规则一致（因为观测点就在 Ares 处理完之后）。

---

## 7. 变更记录

| 日期 | 变更 |
|---|---|
| 本次交付 | 新增 `Promote.KeepHealth` / `Promote.KeepVeterancy`；复用 Ares 的 `Promote.VeteranType` / `Promote.EliteType`；新增 `AresHelpers`（版本解析 + `ConvertTypeTo`）与三处 Ares 调用点包装 |
| 本次交付（实机反馈修复） | "升级前等级"改用 `CurrentRanking`（否则超武升星导致的升级不换单位）；新增等级变化/未知类型/换算前后血量等限量诊断日志 |
| 本次交付（需求调整） | 移除 `Promote.ElitePlusType` / `Promote.ElitePlusExperience` 与 `0x6FA14B` 相关代码（原因见 §1.5），`rulesmd.ini` 的 `VeteranCap` 已还原 |
