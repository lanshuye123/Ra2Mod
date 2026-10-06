# 功能：单位提供超级武器（Unit-Provided Superweapons）

> 通用开发方法（调用约定、与 Ares 共存、定位 Ares 内部函数）见
> [`../DEVELOPMENT.md`](../DEVELOPMENT.md) 的 §5.9 – §5.11。
> 本文只讲这个功能本身。

| 项目 | 值 |
|---|---|
| 状态 | ✅ 已实现并实测通过 |
| 依赖 | **Ares 3.0**（Ares 缺席或版本不匹配时自动停用，不会崩） |
| 引入版本 | HAres v0.1.0.0 |
| 源码 | `src/Misc/UnitSuperWeapon.cpp` / `.h`、`src/Misc/AresUnitSuperWeapon.cpp` |
| 首次提交 | `bbd9f49` |

---

## 1. 功能说明

### 1.1 作用

原版和 Ares 只允许**建筑**给玩家提供超级武器。本功能让**载具、步兵、飞机**也能提供，
使用与建筑类型**完全相同的 INI 键**。

玩家拥有至少一个这样的单位时获得该超武，最后一个损失后失去。

### 1.2 用法

在 `rulesmd.ini` 的任意单位类型段（`[VehicleTypes]` / `[InfantryTypes]` / `[AircraftTypes]`
里登记的类型）中加上：

```ini
[MTNK]
SuperWeapon=IronCurtainSpecial

[AMCV]
SuperWeapon2=AmericasParaDropSpecial

[E1]
SuperWeapons=ChronoSphereSpecial,AmericasParaDropSpecial
```

| 键 | 说明 |
|---|---|
| `SuperWeapon=` | 单个超武 ID（对应 `[SuperWeaponTypes]` 里的名字） |
| `SuperWeapon2=` | 第二个槽位，与建筑一致 |
| `SuperWeapons=` | 逗号分隔列表，可写多个（数量不限） |

三个键可以同时使用，会全部合并登记。ID 写错或不存在时会在 `HAres.log` 里告警并跳过该条，
不影响其它条目。

### 1.3 行为语义

- **玩家级超武**：超武仍然从玩家层面发射 —— 侧边栏出现图标，点图标再点地图。
  单位只是"会移动的提供者"，不是从单位身上发射。
- **充能 / 断电**：与建筑提供的超武**完全一致**，因为走的是 Ares 原有的整套逻辑
  （`Grant` / `Lose` / `SetOnHold` / 侧边栏图标 / `SWAllowed` / `DisableableFromShell` / 断电停充）。
- **失去提供者即收回**：最后一个单位被摧毁、卖掉、或**展开成建筑**（如 MCV→建造厂）后，
  超武立即消失。
- **多个单位不叠加**：拥有 1 个和拥有 10 个效果相同，不加快充能。

### 1.4 生效时机

提供者表在 **`ScenarioClass::Start`**（`0x683E7F`）时从 `rulesmd.ini` 读取并建立，
因为那是 `CCINIClass::INI_Rules` 可用的最早时机。所以**每局开局时生效**，
改完 INI 需要重开一局（或重启游戏）。

---

## 2. 原理

### 2.1 原版机制

`HouseClass::UpdateSuperWeaponsOwned`（`0x50AF10`）与
`HouseClass::UpdateSuperWeaponsUnavailable`（`0x50B1D0`）在每个 house 更新时运行，
扫描该 house 的建筑，检查 `BuildingTypeClass::SuperWeapon` / `SuperWeapon2`，
据此决定授予、收回和是否暂停充能。原版还会扫描建筑的第 3 个字段（升级槽）。

已用反汇编核对的结构偏移（gamemd.exe 1.11）：

| 字段 | 偏移 |
|---|---|
| `SuperClass::Type` | +0x28 |
| `SuperClass::CanHold` | +0x60 |
| `SuperClass::Granted`（Phobos 名 `IsPresent`） | +0x6D |
| `SuperClass::OneTime`（`IsOneTime`） | +0x6E |
| `SuperClass::IsCharged`（`IsReady`） | +0x6F |
| `SuperClass::IsOnHold`（`IsSuspended`） | +0x70 |
| `HouseClass::Supers` 数据指针 / 个数 | +0x258 / +0x264 |
| `HouseClass::Defeated` | +0x1F5 |
| `BuildingTypeClass::SuperWeapon` / `SuperWeapon2` | +0x16F0 / +0x16F4 |

> Phobos 的 YRpp 和 Ares 的 YRpp 给同一批字段起了不同名字
> （`Granted`→`IsPresent`、`IsCharged`→`IsReady`、`IsOnHold`→`IsSuspended`），
> **偏移和顺序完全一致**，用哪套名字都行。

`HouseClass::Supers` 与 `SuperWeaponTypeClass::Array` 是 **1:1 按 `ArrayIndex` 对齐**的
（每个 house 对每种超武都有一个 `SuperClass` 实例），所以可以直接用超武下标索引它。
这一点是 Ares 代码隐含假设的（`Statuses[pHouse->Supers.Count]` 用 `pType->ArrayIndex` 索引），
本功能也依赖它。

### 2.2 Ares 3.0 改写了什么

Ares 把上面两个函数**整个替换**掉了，而且它的 hook 都 `return` 一个非零地址
（`0x50B1CA` / `0x50B36E`）。Syringe 只对 `return 0` 的 hook 做链式调用，
所以**在同样地址挂我们的 hook 永远不会执行**。`SuperClass::Lose`（`0x6CB7B0`）同理
（Ares 的 hook `return 0x6CB810`）。

Ares 3.0 的实际实现（反汇编得到，比 0.A 源码简单得多）是两个 hook 共同调用一个静态函数：

```cpp
// Ares.dll + 0x38F10，__stdcall（本栈是 /Gz，见 §4.2）
std::vector<SWStatus>* GetSuperWeaponStatuses(HouseClass* pHouse);

// 元素 3 字节，由 SuperWeaponTypeClass::ArrayIndex 索引
struct SWStatus
{
    bool Available;     // 该超武是否"存在"
    bool PowerSourced;  // 是否供电充足（决定能否充能）
    bool Charging;      // 是否可以推进充能
};
```

那个 `std::vector` 是函数的**函数内静态对象**（`Ares.dll + 0xC1C90`），
所以函数返回的是**指针**，不是按值返回 —— 也就没有隐藏返回槽参数。

Ares 只从 `pHouse->Buildings` 收集提供者，这正是它看不到单位的原因。

### 2.3 注入点

**把对 `GetSuperWeaponStatuses` 的两处调用改指向包装函数**：

```
Ares 的 hook ──call──> [我们改写] ──call──> GetSuperWeaponStatuses   （Ares 原逻辑，扫建筑）
                                        ↓ 返回后
                                     标记"由单位提供"的超武为
                                     Available = PowerSourced = Charging = 1
                                        ↓
Ares 的 hook 继续跑它自己的
Grant / Lose / SetOnHold / cameo / 断电判定
```

这样做的关键好处：**下游全部是 Ares 自己的代码**，
所以充能、断电、侧边栏图标、`SWAllowed`、`DisableableFromShell` 等规则
与建筑提供的超武**逐字节一致**，而且**不存在任何状态争夺**
（不需要 hook `Grant`/`Lose` 去对抗 Ares，也就不会出现闪烁或充能重置）。

包装函数里需要**复刻 Ares 自己的两个跳过条件**，否则会给不该有的 house 授予超武：

```cpp
if (!pHouse || pHouse->Defeated || pHouse == HouseClass::Observer)
    return pStatuses;      // Ares 在这两种情况下直接返回全 0 的表
```

（`HouseClass::Observer` 位于 `0xAC1198`，Ares 原函数里显式比对了它。）

---

## 3. 实现

### 3.1 文件与职责

| 文件 | 职责 |
|---|---|
| `src/Misc/UnitSuperWeapon.h` | 对外接口：建表、查询、诊断、安装 Ares 补丁 |
| `src/Misc/UnitSuperWeapon.cpp` | 从 `rulesmd.ini` 读键建"提供者"表；O(1) 查询 house 是否拥有提供者；诊断日志 |
| `src/Misc/AresUnitSuperWeapon.cpp` | 识别 Ares 版本、按 RVA 改写调用点、包装函数本体 |
| `src/Misc/Hooks.Demo.cpp` | 在 `ScenarioClass::Start` 时调用 `BuildRegistry()`；每帧调 `TickDiagnostics()` |
| `src/HAres.cpp` | 在 `ExeRun` 里调用 `InitAresIntegration()` |

### 3.2 数据结构

```cpp
struct Provider
{
    TechnoTypeClass* Type;   // 提供超武的单位类型
    int SWIndex;             // 指向 SuperWeaponTypeClass::Array
};

std::vector<Provider> Providers;      // 全部 (类型, 超武) 对
std::vector<int>      SWIndices;      // 去重后的超武下标，供包装函数遍历
```

`SWIndices` 只包含**真的有单位提供**的超武，所以每帧的工作量是
"提供者种类数" 而不是"全部超武数"。

### 3.3 拥有关系判定

```cpp
pHouse->CountOwnedAndPresent(provider.Type) > 0
```

用的是 `HouseClass` 的按类型 **"Active" 计数器**（`ActiveUnitTypes` /
`ActiveInfantryTypes` / `ActiveAircraftTypes`），所以：

- 每次查询是 **O(1)**，不需要遍历全场对象；
- 语义是"已部署在场上的数量"，与 Ares 对建筑的 `IsAlive && !InLimbo` 判定一致。

### 3.4 执行流程

```
ExeRun (0x7CD810)
  └─ InitAresIntegration()
       ├─ GetModuleHandleA("Ares.dll")      找不到 → 记日志并停用
       ├─ 读 PE TimeDateStamp 判版本        不认识的版本 → 记日志并停用
       ├─ 保存 GetSuperWeaponStatuses 原地址
       └─ Patch::Apply_CALL 改写两个调用点

ScenarioClass::Start (0x683E7F)
  └─ BuildRegistry()   读 rulesmd.ini 建表
     LogRegistry()     把登记结果写进 HAres.log

每帧（GScreenClass::DrawText 里）
  └─ TickDiagnostics()  状态变化时打日志（仅在变化时，不刷屏）

每帧（Ares 的 SW 更新里，被我们改写）
  └─ GetSWStatuses_Hook()
       ├─ 调 Ares 原函数（扫建筑）
       └─ 对本 house 拥有的单位提供者，把对应 SWStatus 标为可用
```

### 3.5 为什么把建表放在 `ScenarioClass::Start`

`CCINIClass::INI_Rules`（`0x887048`）要到规则文件加载完才有效。
`ScenarioClass::Start` 是 HAres 已经挂着的第一个"规则已就绪"的点，
且早于本局第一次 `HouseClass` 更新，所以不会漏掉任何一帧。

---

## 4. 关键地址与偏移

### 4.1 Ares 3.0 的 RVA

已在 `AresUnitSuperWeapon.cpp` 里硬编码，并按 **PE `TimeDateStamp`** 做版本门控
（与 Phobos 的 `AresHelper` 一致）：

| 版本 | PE TimeDateStamp |
|---|---|
| Ares 3.0 | `0x5fc37ef6` |
| Ares 3.0p1 | `0x61daa114`（偏移未映射，会明确停用） |

针对 **Ares 3.0**（`FileVersion 20.333.289`，`MD5 955a3977dfeaf7c76953a7c27d205dcd`）：

| 位置 | RVA |
|---|---|
| `GetSuperWeaponStatuses` | `0x38F10` |
| 调用点 1（`UpdateSuperWeaponsOwned` 内） | `0x3945B` |
| 调用点 2（`UpdateSuperWeaponsUnavailable` 内） | `0x395A7` |
| Ares 的静态 `std::vector<SWStatus>` | `0xC1C90` |

调用点的字节形态是 `E8 rel32`（5 字节直接调用），所以可以直接用
`Patch::Apply_CALL(base + RVA, &包装函数)` 改写。

### 4.2 调用约定：`__stdcall`（曾因此崩溃）

`GetSuperWeaponStatuses` 是 Ares 里的**自由函数**，而 Ares 是用 **`/Gz`** 编的，
所以它默认是 **`__stdcall`**，不是 Syringe hook 导出那种显式 `__cdecl`。

包装函数的签名必须是：

```cpp
using GetSWStatuses_t = std::vector<AresSWStatus>* (__stdcall*)(HouseClass*);
std::vector<AresSWStatus>* __stdcall GetSWStatuses_Hook(HouseClass* pHouse);
```

**第一版写成 `__cdecl` 的后果**：每次调用多弹 4 字节栈，跑几十帧后跳到垃圾地址崩溃。

```
Exception (Code: 0xC0000005 at 0x329344F8)
Eax:5F291C90        ← Ares 基址 0x5F1D0000 + 0xC1C90，正是那个静态 vector
Bytes at CS:EIP: A0 A8 7E 00 84 A8 7E 00 ...   ← EIP 落进了数据表
```

**判定方法**（见 `../DEVELOPMENT.md` §5.9）：看函数末尾的 `RET`。
本例三个出口都是 `C2 04 00`（`RET 4`）→ `__stdcall`。

### 4.3 重新定位的方法

`Ares.dll` 换版本后需要重新找这三个 RVA。方法（不依赖完整反汇编）：

1. `dumpbin /exports Ares.dll` → 拿到两个 hook 的 RVA
   （`HouseClass_UpdateSuperWeaponsOwned` / `...Unavailable`，它们是导出函数）。
2. 用下一个导出函数的 RVA 界定每个函数的范围。
3. 在各自函数体内扫描 `E8 rel32`，算出所有调用目标。
4. **取两个函数调用目标的交集** —— 目标函数被两者共同调用。
5. 按 Ares 源码里的调用顺序确定哪个是 `GetSuperWeaponStatuses`
   （它是第一个被调的；后面两个是 `GetObjectTabIdx` 与 `RepaintSidebar`）。
   实测交集为 3 个候选，第一个即目标。
6. 到 `GetSuperWeaponStatuses` 内部确认它返回一个静态 `std::vector`
   （会看到 3 指针的 vector 布局 + `_atexit` 注册析构）。

---

## 5. 实测验证

环境：`RunHAres.bat` 启动，Ares 3.0 + Phobos 0.5.0.0 + HAres，测试键加在
`[MTNK]` / `[AMCV]` / `[SMCV]` 上（`SuperWeapon=IronCurtainSpecial`）。

`HAres.log`：

```
[UnitSW] redirected GetSuperWeaponStatuses call at Ares+0x3945B
[UnitSW] redirected GetSuperWeaponStatuses call at Ares+0x395A7
[UnitSW] Ares 3.0 integration ACTIVE (2 call site(s) patched)
[UnitSW] AMCV provides 1 superweapon(s)
[UnitSW] MTNK provides 1 superweapon(s)
[UnitSW] SMCV provides 1 superweapon(s)
[UnitSW] registry built: 3 provider(s), 1 distinct superweapon(s)
[UnitSW]   AMCV -> IronCurtainSpecial
[UnitSW]   MTNK -> IronCurtainSpecial
[UnitSW]   SMCV -> IronCurtainSpecial
[UnitSW] superweapon "IronCurtainSpecial" (index 1) made available by a unit provider to the human player
```

生命周期（`TickDiagnostics` 在状态变化时打印）：

| 时间 | providedByUnit | granted | rechargeLeft | 含义 |
|---|---|---|---|---|
| 28.980 | 1 | 0 | 0 | 单位存在，超武尚未授予 |
| 28.995 | 1 | **1** | **4499** | **Ares 授予了超武并开始充能** |
| 30.923 | 0 | 0 | 4378 | MCV 展开成建造厂 → 提供者消失 → 超武收回 |
| 41.180 | 1 | **1** | **4499** | 重新拥有提供者 → 再次授予并重新充能 |

**`granted=1` 且 `rechargeLeft` 开始倒数**，证明是 **Ares 自己在授予和充能**，
不是我们硬塞状态 —— 这正是 §2.3 那个设计要保证的。

全程 `syringe.log` 只有两个正常的 `0xE06D7363`（游戏自身的 C++ 异常，
有无 HAres 都会出现），**没有任何 `0xC0000005`**。

---

## 6. 已知限制

| 限制 | 说明 | 影响 |
|---|---|---|
| **依赖 Ares** | Ares 缺席时功能停用并记日志，不崩 | 现状可接受（本机装的是 Ares 3.0） |
| **只映射 Ares 3.0** | 检测到 3.0p1 会记录"偏移未映射"并停用 | 换版本需按 §4.3 重新定位 |
| **limbo 中的单位不算提供者** | 判定用 "Active" 计数器 | 装在运输载具里的单位不提供超武；与 Ares 对建筑的 `!InLimbo` 判定一致 |
| **不叠加** | 有 1 个和 10 个效果相同 | 如需"每 N 个加快充能"要另做 |
| **超武不是从单位发射的** | 仍是玩家级侧边栏发射 | 单位不享有独立充能计时 |
| **需重开一局** | 注册表在 `ScenarioClass::Start` 建立 | 改 INI 后不重启不生效 |

---

## 7. 变更记录

| 日期 | 变更 |
|---|---|
| 2026-10-06 | 首次实现。单位提供者表 + Ares 3.0 调用点改写；载具/步兵/飞机全覆盖 |
