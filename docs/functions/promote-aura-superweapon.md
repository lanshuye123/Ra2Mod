# 功能：范围升/降星超级武器（PromoteAura）

> 通用方法见 [`../DEVELOPMENT.md`](../DEVELOPMENT.md) 的 §1.2（hook 机制）、§5.1（新增 hook 的流程）、§5.10（与 Ares 共存）。

| 项目 | 值 |
|---|---|
| 状态 | ✅ **实机验证通过**（编译零警告 + hook 注册核验 + 自动对局中反复发射并批量升降星，见 §5） |
| 依赖 | Ares 3.0（发射链路要经过 Ares 的 `SuperClass::Launch` hook）、Syringe / SyringeEx |
| 源码 | `src\Misc\PromoteAura.h`、`src\Misc\PromoteAura.cpp` |
| 引入版本 | HAres v0.1.0.0 |

---

## 1. 功能说明

这是一个**新的超级武器类型**：`Type=PromoteAura`。它对目标格周围一定半径内的单位**批量改变老兵星级**，`PromoteAura.Levels` 为正数就是升星，为负数就是降星。

### 1.1 怎么用

超级武器类型本身还是普通的 `[SuperWeaponTypes]` 条目，只需要把 `Type` 写成 HAres 认得的名字，并让 Ares 允许它选点发射：

```ini
[PromoteAuraSpecial]
UIName=Name:PromoteAura
Name=Promote Aura
IsPowered=false
RechargeTime=2
Type=PromoteAura          ; ← HAres 识别的新超武类型
Action=Custom             ; ← 让 Ares 把它当作"可以点地图发射"的超武
SidebarImage=IRCRICON
ShowTimer=yes
Range=6                   ; 引擎自己的选点距离（可选）

; ---- HAres 的 PromoteAura.* 参数 ----
PromoteAura.Range=6            ; 影响半径（格，以目标格中心为圆心）
PromoteAura.Levels=1           ; >0 升星，<0 降星；1=升一级，-2=连降两级
PromoteAura.Houses=owner       ; owner / allies / enemies / all / none（逗号可组合）
PromoteAura.Types=             ; 可选：逗号分隔的 TechnoType 列表；留空=全部类型
PromoteAura.AffectBuildings=no ; 是否也影响建筑
PromoteAura.AffectAircraft=yes ; 是否影响空中单位
PromoteAura.MaxTargets=0       ; 0=不限；否则最多影响这么多个
PromoteAura.Anim=              ; 可选：每个被影响单位身上播放的动画
PromoteAura.Sound=             ; 可选：音效名（sound(md).ini 里的条目名）
```

把它发给某个建筑（和原版超武一样，用 `SuperWeapon=` / Ares 的 `SuperWeapons=` 列表），或者用 Ares 的 `SW.AuxBuildings` / 触发器等任何方式授予即可。

### 1.2 行为语义

| 键 | 默认 | 说明 |
|---|---|---|
| `PromoteAura.Range` | `0`（不生效） | 影响半径，单位是**格**。距离只算 X/Y 平面，忽略高度（否则高空飞机会免疫地面光环）。 |
| `PromoteAura.Levels` | `1` | 每次改变几个整档：`新兵(0) → 老兵(1) → 精英(2)`；正数升、负数降，越界会被夹在 `新兵`~`精英` 之间。 |
| `PromoteAura.Houses` | `owner` | 过滤"哪些房子所属的单位会被影响"。语义与 Phobos 的 `AffectedHouse` 一致：`allies` 含自己，`enemies` 是所有非盟友。 |
| `PromoteAura.Types` | 空（全部） | 只影响列出的 TechnoType。写错的名字会在 `HAres.log` 里报出来。 |
| `PromoteAura.AffectBuildings` | `no` | 建筑也有老兵等级，但默认不动它们（"单位"通常指机动单位）。 |
| `PromoteAura.AffectAircraft` | `yes` | 是否影响飞行中的单位。 |
| `PromoteAura.MaxTargets` | `0` | 目标上限，按引擎对象数组的顺序取前 N 个。 |
| `PromoteAura.Anim` / `.Sound` | 空 / 无 | 每个**真的被改变了星级**的单位身上播一次动画/音效，给玩家反馈。 |

每次发射都会在 `HAres.log` 写一行，例如：

```
[PromoteAura] PromoteAuraSpecial fired at cell (42,37): 7 techno(s) promoted by 1 level(s)
```

### 1.3 与其他升星来源的关系

HAres 只改引擎的 `VeterancyStruct`（浮点经验），**不碰 `TechnoClass::CurrentRanking`**。下一帧引擎（装 Ares 时是 Ares 自己那段逻辑）会发现"缓存的星级和当前经验不符"，于是照常执行升级该做的一切：重算属性、播升级音效/动画、更新徽章。所以：

* 升星得到的加成和正常打怪升级**完全一样**；
* 如果该单位类型配了 `Promote.VeteranType` / `Promote.EliteType`（Ares 原生键，或本工程的 [升级换单位](promotion-convert.md) 功能），**光环造成的升星同样会触发换单位** —— 这是刻意保留的一致性。

---

## 2. 原理

### 2.1 原版/Ares 怎么做"新超武类型"

引擎的超武效果是 `SuperClass::Launch` 里的一张跳转表，按 `SuperWeaponTypeClass::Type` 这个枚举（`Nuke=0 … PsychicReveal=11`）分派。**引擎不认识的名字它没法处理**，所以 Ares 走的是"另一个入口"：

1. Ares 读 `Type=<名字>`，若名字在它的 `NewSWType` 注册表里，就把 `pThis->Type` 改写成 Ares 自己的索引（> 11）；
2. Ares 在 `0x6CC390`（`SuperClass::Launch` 入口）挂 hook：**归 Ares 管**的类型直接 `return 0x6CDE40`（那是该函数的 `RET 8`）—— 等于跳过整个引擎逻辑，由 Ares 自己完成发射；
3. **不归 Ares 管**的类型 `return 0`，控制权沿 Syringe 链继续往下，最终进入引擎原有的发射代码。引擎的跳转表对 `Type > 0xb` 或 `Invalid(-1)` 会直接跳到函数尾声，什么也不做。

HAres 就坐在第 3 条那个"没人管"的位置上。

### 2.2 为什么用 `Type=PromoteAura` 而不是自己造一个键

* 这是 Ares 世界里"新增超武类型"的标准做法（Ares 的 `DropPod`、`GenericWarhead` 等都是 `Type=<名字>`），modder 的直觉一致；
* HAres 只要读同一个 `Type=` 字符串即可，不需要 Ares 配合，也不需要往 Ares 的数据结构里写东西。

### 2.3 为什么要把 `Type` 强制改成 `Invalid`

`Type=PromoteAura` 对引擎是个陌生字符串。引擎的 INI 解析对陌生名字**不会**报错，但 `SuperWeaponTypeClass::Type` 会停在构造时的默认值上；如果那个默认值恰好是 `0`（`Nuke`），点一下超武就会在目标格引爆一颗核弹。为了让行为确定，`BuildRegistry()` 把注册过的超武类型的 `Type` 写成 `SuperWeaponType::Invalid`（`-1`）：

* 引擎跳转表的入口是 `CMP EAX,0xb; JA 尾声`，`-1` 作为无符号数远大于 `0xb`，于是**必然走"什么都不做"**那条路；
* 这个写入发生在开局（`ScenarioClass::Start`）之后、任何超武发射之前；
* 对 Ares 安全：`SWTypeExt::ExtData::GetNewSWType()` 只在索引 `>= FirstCustomType` 时才去查它的数组（`Ares\src\Ext\SWType\Body.cpp:576`），`Invalid` 会直接返回 `nullptr`，不会越界。

充能计时、侧边栏图标、可用性判定都不经过这段逻辑，所以超武的其它行为完全正常（充能重置在**调用方** `FUN_006cb920` 里做：它在 `CALL 0x6CC390` 之前就写好了 `SuperClass+0x30` 的 `RechargeTimer`）。

### 2.4 发射注入点

`0x6CC390` 是 `SuperClass::Launch(CellStruct const& cell, bool isPlayer)` 的第一条指令，**所有**超武发射路径都经过它。在这里 `ECX = SuperClass*`，栈上是 `[ESP+4] = &cell`、`[ESP+8] = isPlayer`。

### 2.5 发射后必须自己清掉"超武选点"状态（踩过的坑）

**症状**：超武打出去之后，鼠标指针还停在"超武选点"的样子，不回普通指针。

**原因**：玩家点侧边栏超武图标后，游戏把该超武的索引存进全局 `Unsorted::CurrentSWType`（`0x8809A0`），鼠标指针就停在超武指针上。清掉它的责任在两个地方：

| 谁 | 在哪清 | 覆盖范围 |
|---|---|---|
| 引擎 | `SuperClass::Launch` 里**每种原版超武各自的 case 处理块**末尾，例如 `0x6CD04F: MOV [0x8809A0], -1`（无符号跳转表 12 项都在 `0x6CDE44`） | 12 种原版类型 |
| Ares | `SWTypeExt::Launch` → `Ares\src\Ext\SWType\Body.cpp:528-531` | Ares 自己的 `NewSWType` |

我们的超武 `Type` 被强制成 `Invalid`（§2.3），引擎的 12 个 case 一个都不走；又不是 Ares 的 `NewSWType`，Ares 那段也不会执行 → **两边都不清** → 指针卡住。这正是"新超武类型"必须自己补的账。

HAres 的做法（`PromoteAura.cpp` 的 `ClearSelectionAfterFiring()`）复刻 Ares 的条件：

```cpp
if (pSuper->Owner != HouseClass::CurrentPlayer) return;          // 只清"本机玩家自己"的超武
if (Unsorted::CurrentSWType != pSuper->Type->ArrayIndex) return; // 只清"当前选中的就是它"
Unsorted::CurrentSWType = -1;
```

不能无条件清（引擎原版是无条件的，`0x6CD04F`），因为 `CurrentSWType` 只是**类型索引**、不含房子信息：如果对手或 AI 发射了同类型的超武，无条件清会把**本机玩家正在选点**的状态一起取消。Ares 的注释写得很清楚："*we reset the selected SW only for the player on this computer, so others don't deselect it when firing simultaneously*"。清理必须在任何提前返回之前执行 —— 否则 `PromoteAura.Range=0` 之类的配置会连指针一起留下。

---

## 3. 实现

```
src\Misc\PromoteAura.h     对外接口（BuildRegistry / LogRegistry / Apply / IsPromoteAura）
src\Misc\PromoteAura.cpp   规则表 + 效果 + 0x6CC390 hook
src\Misc\SharedUtils.h     房子过滤、格→缇距离换算、INI 读取小工具（三个功能共用）
```

执行流程：

1. **建表**（`ScenarioClass::Start`，以及首次用到时的惰性兜底 —— 读档不会走 Start，所以每个 hook 入口都会先 `EnsureBuilt()`）：
   遍历 `SuperWeaponTypeClass::Array`，凡 `Type=` 等于 `PromoteAura` 的，读 `PromoteAura.*` 存进 `std::unordered_map<const SuperWeaponTypeClass*, Params>`，并强制 `Type = Invalid`。
2. **发射**（hook `0x6CC390`）：`Registry.find(pSuper->Type)`；命中就取目标格坐标，遍历 `TechnoClass::Array`，逐个判定房子/类型/建筑/飞机/距离，然后改星级。
3. **改名次**：只写 `pTechno->Veterancy`：

   ```cpp
   if (target == 0) veterancy.SetRookie(false);   // 正好 0.0，不是 -0.25 的"负新兵"
   else if (target == 1) veterancy.SetVeteran();
   else veterancy.SetElite();
   ```

   容器里的 `CurrentRanking` 故意不动，留给引擎/Ares 去发现变化。

---

## 4. 关键地址与偏移

| 地址 | size | 用途 | 现场 |
|---|---|---|---|
| `0x6CC390` | 6 | `SuperClass::Launch` 入口 | `ECX=SuperClass*`，`[ESP+4]=const CellStruct*`，`[ESP+8]=bool isPlayer` |

**抢占字节核验**（`read_bytes 0x6CC390 6` → `81 EC D4 01 00 00`）：

```
006cc390 SUB ESP,0x1d4          ; 正好 6 字节，指令边界完整
006cc396 PUSH EBX               ; ← 抢占窗口之后
```

**怎么重新推导这些地址**

* `0x6CC390` 来自 Phobos 自己的 hook（`Phobos\src\Ext\SWType\Hooks.cpp:4`），它同时是 Ares 的 hook 点（`D:\Games\Ra2\Ares.dll.inj` 里 `6CC390 = SuperClass_Launch, 6`）—— 两处独立来源互相印证。
* 判断"这个地址的返回值会不会截断链"：反编译 Ares.dll 里对应的 hook 函数。本例 Ares 的 `SuperClass_Launch` 逻辑是 `handled ? 0x6CDE40 : 0`（Ares 0.A 源码 `Ext\SWType\Body.cpp:444` 的 `Activate()` 返回 `false` 即不接管），所以未知类型一定会落到 HAres。
* `SuperWeaponTypeClass::Type` 的偏移 `0xb4` 是反汇编 `0x6CC39b` 的 `MOV EBP,[EBX+0x28]`（`SuperClass::Type`）+ `MOV EAX,[EBP+0xb4]` 得到的；HAres 代码里一律用 YRpp 成员名访问，不写裸偏移。

---

## 5. 实测验证

**（1）编译与 hook 注册**（本机 Release + Debug 均通过，零警告）：

```
HAres.vcxproj -> D:\Codes\RA2Mods\HAres\Release\HAres.dll
HAres.vcxproj -> D:\Codes\RA2Mods\HAres\Debug\HAres.dll
```

`dumpbin /headers Release\HAres.dll` → `.syhks00` 虚拟大小 **0x90 = 144 字节 = 9 个 hook**（原 5 个 + 本功能 1 个 + 升级换单位 1 个 + 心控屏蔽 2 个），`dumpbin /exports` 里能看到 `HAres_SuperClass_Launch_PromoteAura`：

```
.syhks00 name
   90 virtual size
```

**（2）抢占字节**：见 §4，6 字节恰好是 `SUB ESP,0x1d4` 一条完整指令。

**（3）实机**（✅ 已通过）：在游戏目录的 `AddCN.ini`（这个 mod 自己的扩展 INI）里按 §1.1 配置后，用 `RunHAres.bat` 启动、`-AI-CONTROL` 自动对局，`HAres.log` 的实际输出：

```
syringe.log:  Recognized DLL: "HAres.dll" ... Done (3116 hooks added).     ← 原 3112，+4 正好是本次新增的 hook
HAres.log:
  [Ares] detected Ares 3.0 at 0x5C620000 (ConvertTypeTo at 0x5C663650)
  [Hook] ScenarioClass::Start #1
  [PromoteAura] PromoteAuraSpecial: Range=8.00 Levels=1 Houses=0x1 Buildings=0 Aircraft=1 MaxTargets=0 Types=0 Anim=<none> Sound=-1
  [PromoteAura] PromoteAuraSpecial fired at cell (104,188): 7 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (104,188): 7 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (101,190): 11 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (102,190): 12 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (102,190): 3 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (102,190): 0 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (96,196): 3 techno(s) promoted by 1 level(s)
  [PromoteAura] PromoteAuraSpecial fired at cell (102,190): 0 techno(s) promoted by 1 level(s)
```

这份日志同时证明了四件事：

1. **注册表与 INI 读取正确**：`Range=8.00 Levels=1 Houses=0x1 Buildings=0 Aircraft=1` 与配置逐项一致，说明 HAres 能读到 mod 的 `AddCN.ini`（`rulesmd.ini` → `Add.ini` → `AddCN.ini` 的嵌套 `[#include]` 链路有效）；
2. **超武真的能选点发射**：日志里没有 `Action is not SuperWeaponAllowed` 的告警，说明 `Action=Custom` 被 Ares 正确映射，发射链路一路走到 `0x6CC390`；
3. **效果真的生效**：连续 8 次发射，每次都报出被影响的单位数（7/11/12/3/0…）；
4. **升到顶会自然停下**：同一个格子连续发射时数字从 12 掉到 3 再到 0 —— 那些单位已经全部是精英（`Levels=1` 无法再升），正是 §1.2 "越界夹在 新兵~精英 之间"的预期行为。

整个 150 秒测试期间 `gamemd.exe` 始终 `Responding=True`，`syringe.log` 无访问违例（只有游戏本身正常的 `0xE06D7363` C++ 异常），说明这个 hook 与 Ares/Phobos 的链共存无冲突。

**（4）调试用配置**：本次交付已按用户要求在游戏 INI 里挂好（苏联建造厂提供该超武，动员兵三星后转鲍里斯），详见 [promotion-convert.md](promotion-convert.md) §5.5。

---

## 6. 已知限制

* **必须有 Ares 3.0**：`Type=PromoteAura` 的超武能被选点发射，靠的是 Ares 把 `Action=Custom` 映射成它自己的 `SuperWeaponAllowed`。没有 Ares 时超武可能无法发射（日志里会有提示）。
* 距离只算平面，不考虑高度；坑道/建筑内部（`InLimbo`）的单位会被跳过。
* 降星同样只改经验值，不会主动撤销已经生效过的"升级音效/换单位"历史。
* `PromoteAura.MaxTargets` 的截断顺序是引擎对象数组顺序，不保证"最近的优先"。
* 光环导致的升星若触发了换单位（`Promote.*Type`），新单位是否满血/是否继承星级由 [升级换单位](promotion-convert.md) 的键决定。

---

## 7. 变更记录

| 日期 | 变更 |
|---|---|
| 本次交付 | 新增 `Type=PromoteAura` 超武类型、`PromoteAura.*` 参数、`0x6CC390` hook |
| 本次交付（实机反馈修复） | 修复"发射后超武指针不复位"：新增 `ClearSelectionAfterFiring()`，按 Ares 的条件清 `Unsorted::CurrentSWType`（详见 §2.5），并在日志里记录清理动作 |
