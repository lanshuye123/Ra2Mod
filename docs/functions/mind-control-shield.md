# 功能：心控屏蔽区间（MindControlShield）

> 通用方法见 [`../DEVELOPMENT.md`](../DEVELOPMENT.md) 的 §5.9（调用约定）、§5.10（与 Ares/Phobos 共存）、§1.3（Syringe 的 hook 链）。

| 项目 | 值 |
|---|---|
| 状态 | ✅ **实机验证通过**：开火屏蔽（原版尤里改与测试心控武器使用者均被拦）与解除心控（平民进入区间即被归还，一局 10 次）都已在自动对局中观察到，见 §5 |
| 依赖 | 无（不依赖 Ares）；与 Phobos 共存时行为更好（见 §2.3） |
| 源码 | `src\Misc\MindControlShield.h`、`src\Misc\MindControlShield.cpp` |
| 引入版本 | HAres v0.1.0.0 |

---

## 1. 功能说明

给**任何 TechnoType**（建筑、车辆最常用，步兵/飞机也可以）注册一个"心控屏蔽区间"。区间生效时（该对象存活、在场上、未被停用），在半径内：

1. **已经被心控的单位会被解除心控** —— 控制节点被摘掉、单位归还原主、播放心控解除音效；
2. **使用心控武器的单位无法攻击** —— 它的开火判定直接返回 `FireError::REARM`，于是保持目标但开不出火，离开区间后自动恢复。

### 1.1 怎么用

```ini
[GAPSYCH]                          ; 建筑或车辆的类型 ID 都行
MindControlShield.Range=8          ; 屏蔽半径（格）；0 或省略 = 不启用
MindControlShield.Houses=all       ; 这个屏蔽对"哪一方"生效
MindControlShield.ReleaseMindControl=yes   ; 是否解除区间内已心控单位
MindControlShield.BlockAttack=yes          ; 是否禁止区间内使用心控武器的单位开火
```

例如"尤里阵营的心灵信标建筑 + 8 格无敌心控区"：

```ini
[GAPSYCH]
MindControlShield.Range=8
MindControlShield.Houses=owner
MindControlShield.ReleaseMindControl=yes
MindControlShield.BlockAttack=yes
```

### 1.2 行为语义

| 键 | 默认 | 说明 |
|---|---|---|
| `MindControlShield.Range` | `0`（不启用） | 半径，单位**格**。平面距离（忽略高度）。 |
| `MindControlShield.Houses` | `all` | `owner` / `allies` / `enemies` / `all` / `none`，逗号可组合，语义与 Phobos 的 `AffectedHouse` 一致。**解除心控**时判定的是单位的**原主**（控制节点的 `OriginalOwner`），**禁止攻击**时判定的是攻击者**当前的所有者** —— 这样 `Houses=owner` 的含义就是"保护我自己的东西、约束我自己的人"。 |
| `MindControlShield.ReleaseMindControl` | `yes` | 关掉就只屏蔽攻击，不解除已有心控。 |
| `MindControlShield.BlockAttack` | `yes` | 关掉就只解除心控，不禁攻击。 |

判定发生在**每一个逻辑帧**（游戏逻辑 15 帧/秒）：解除心控由 `TechnoClass::AI` 上的 hook 驱动，屏蔽攻击在每次开火判定时实时查询。

`HAres.log` 里可以看到：

```
[MindControlShield] GAPSYCH: Range=8.00 Houses=0x1 ReleaseMindControl=1 BlockAttack=1
[MindControlShield] released YURI (original owner Americans) inside a shield zone
```

### 1.3 攻击被屏蔽时用哪个错误码

用的是 `FireError::REARM`（3），和 Ares/Phobos 处理 "weapons disabled" 用的是同一个出口。选它而不是 `ILLEGAL`（5）的原因：`REARM` 的语义是"现在不行、等会儿再试"，单位**不会放弃目标**，走出屏蔽区立刻就能开火；`ILLEGAL` 偏"根本不能打"，某些任务逻辑可能因此丢掉目标。想改成 `ILLEGAL` 只需把 hook 里的返回值换成 `0x6FC86A`。

---

## 2. 原理

### 2.1 解除心控：调用引擎自己的释放函数

心控的数据结构是"控制者在 `TechnoClass::CaptureManager` 里挂一串 `ControlNode`，每个节点记着被控单位和它**原来的主人**"。释放一个单位的最小正确做法**不是**自己拼字段，而是调用引擎的：

```cpp
// CaptureManagerClass::FreeUnit(TechnoClass* pUnit)
// 0x471FF0，__thiscall，1 个参数（末尾 RET 4）
pManager->FreeUnit(pUnit);
```

它会依次：`MindControlRingAnim->UnInit()` 并清空 → 播放 `GetTechnoType()->MindClearedSound`（-1 时取 `RulesClass::Instance->MindClearedSound`）→ `SetOwningHouse(node->OriginalOwner, true)` → `DecideUnitFate()` → 清 `MindControlledBy` → `GameDelete(node)` 并压缩 `ControlNodes`。

HAres 的解除流程是遍历 `CaptureManagerClass::Array`（全局所有的控制管理器），倒序扫描每个管理器的 `ControlNodes`，对"原主符合房子过滤、且位置落在生效区间内"的单位调用 `FreeUnit`。倒序扫描是因为 `FreeUnit` 会摘掉当前节点。

> Ares 的永久心控（Psychic Dominator 那类、`MindControlledByAUnit`）同样是走 `CaptureManager` 的，所以这套释放对它一样有效。

### 2.2 屏蔽攻击：在 `GetFireError` 里否决

引擎判断"这个单位现在能不能用这把武器打这个目标"，统一走虚函数 `TechnoClass::GetFireError(pTarget, nWeaponIndex, ignoreRange)`，实现体在 `0x6FC0B0`。

* 它是**虚函数**（vtable 槽 `+0x3C0`），引擎里有 **20 处**虚调用点；`InfantryClass::GetFireError`（`0x51C8B0`）和 `UnitClass::GetFireError`（`0x740FD0`）最终也都汇进 `0x6FC0B0`；
* 玩家手动下令攻击和 AI 自动攻击走的是同一条任务代码（例如 `0x44B00F` 后面紧跟 `CMP EDI,2` 判断 `FACING` 后转炮塔，这就是攻击任务）。

所以**一处注入就同时覆盖手动与 AI**。HAres 选的是 `0x6FC356`：

```
006fc356  MOV AL, byte ptr [EDI+0x142]      ; 8A 87 42 01 00 00 —— 正好 6 字节
```

这里的现场非常理想：**`EDI` 已经是引擎为这次开火选好的 `WeaponTypeClass*`**（`0x6FC32B MOV EDI,[EAX]`，其中 `EAX` 来自 `pThis->GetWeapon(nWeaponIndex)`），`ESI` 是 `TechnoClass* pThis`。于是判定只需：

```cpp
if (pWeapon->Warhead && pWeapon->Warhead->MindControl)      // 是不是心控武器
    if (在生效区间内) return 0x6FC0DF;                     // → FireError::REARM
return 0;
```

`0x6FC0DF` 是函数内部共享的"返回 `REARM`"尾声（`POP EDI/ESI/EBP; MOV EAX,3; POP EBX; ADD ESP,0x10; RET 0xC`）。它位于 prologue 之后，所以从函数中部跳过去时栈是自洽的 —— 这正是 Ares 和 Phobos 否决开火时用的同一招。

### 2.3 每帧的区间刷新

* 生效区间列表（`{位置, 参数, 主人}`）每个逻辑帧重建一次，用 `Unsorted::CurrentFrame` 做帧守卫，挂在 `TechnoClass::AI`（入口 `0x6F9E50`）上。`TechnoClass::AI` 是每单位每帧都跑的，链路安全（见 §4 的冲突分析）。
* 列表里**只存值，不存 `TechnoClass*`**：区间提供者随时可能被摧毁，存指针会在跨帧时变成野指针。列表里的 `HouseClass*` 是安全的（房子对象在一局里不会销毁）。
* 开火判定侧也做一个 `EnsureFreshShields()`（帧号变了就重建），这样渲染阶段（鼠标指针、索敌显示）在逻辑帧之外查询时拿到的也是当前帧的数据。

---

## 3. 实现

```
src\Misc\MindControlShield.h/.cpp   规则表 + 每帧释放 + 开火否决 + 两个 hook
src\Misc\SharedUtils.h              房子过滤、格→缇换算、INI 小工具
```

| hook | 地址 | size | 作用 |
|---|---|---|---|
| 开火否决 | `0x6FC356` | 6 | `BlocksFiring(pThis, EDI)` → 命中返回 `0x6FC0DF`（REARM） |
| 每帧释放 | `0x6F9E50` | 5 | `Update()`：帧守卫 → 重建区间列表 → 解除心控 |

`BlocksFiring()` 的快速路径顺序是刻意排的（都是热路径）：

```
AnyBlockAttack? → pAttacker/pWeapon 非空? → Warhead->MindControl? → 刷新区间 → 距离判定
```

没有配任何"禁止攻击"的屏蔽时，第一次判断就返回，代价是一次布尔比较。

---

## 4. 关键地址与偏移

| 地址 | 用途 | 证据 |
|---|---|---|
| `0x6FC0B0` | `TechnoClass::GetFireError` 实现体 | prologue `SUB ESP,0x10; PUSH EBX; MOV EBX,[ESP+0x18]…`，`RET 0xC`；虚表槽 `+0x3C0` |
| `0x6FC356` | **HAres 的注入点**（抢占 `MOV AL,[EDI+0x142]`，6 字节） | `read_bytes` → `8A 87 42 01 00 00`；`EDI` 在 `0x6FC32B` 被赋成当前武器 |
| `0x6FC0DF` | 返回 `FireError::REARM(3)` 的共享尾声 | `POP EDI/ESI/EBP; MOV EAX,3; POP EBX; ADD ESP,0x10; RET 0xC` |
| `0x6FC86A` | 返回 `FireError::ILLEGAL(5)` 的共享尾声 | `POP …; MOV EAX,5; … RET 0xC` |
| `0x6F9E50` | `TechnoClass::AI` 入口，**HAres 的注入点**（5 字节 = `SUB ESP,0x68`+`PUSH EBX`+`PUSH EBP`） | `read_bytes` → `83 EC 68 53 55`；`MOV ESI,ECX` 紧随其后 |
| `0x6FA14B` | 同函数内、Ares 跳回的落点（升级功能用） | 见 [升级换单位](promotion-convert.md) |
| `0x471FF0` | `CaptureManagerClass::FreeUnit`（**调用**，不 hook） | `RET 4`；Phobos 把整函数替换成自己的超集版本，调用它即可 |
| `0x471D40` | `CaptureManagerClass::CaptureUnit`（不 hook） | Phobos 整函数替换，hook 会被截断 |

**字段偏移**（本功能用到的，均已用反汇编交叉验证）：

| 字段 | 偏移 | 证据 |
|---|---|---|
| `TechnoClass::CaptureManager` | `0x2BC` | `0x4692CA MOV ECX,[ECX+0x2BC]; CALL 0x471D40` |
| `TechnoClass::MindControlledBy` | `0x2C0` | `IsMindControlled`(`0x7105E0`) 读它；`0x471E34` 写入 |
| `TechnoClass::MindControlledByAUnit` | `0x2C4` | 同上 |
| `TechnoClass::MindControlRingAnim` | `0x2C8` | `FreeUnit`/`CaptureUnit` 读它后调 `UnInit` |
| `CaptureManagerClass::ControlNodes` | `+0x28`(Items) / `+0x34`(Count) | `FreeUnit` 的遍历代码 |
| `WarheadTypeClass::MindControl` | `+0x155` | `WarheadTypeClass::ReadINI`：`PUSH "MindControl"` → 写 `[ESI+0x155]`（前一键 `EMEffect` 写 0x154、后一键读 0x156，与 YRpp 字段序吻合） |
| `WeaponTypeClass::Warhead` | `+0xAC` | `0x6FC325 CALL [EDX+0x3F8]`(=`GetWeapon`) → `0x6FC32B MOV EDI,[EAX]`，随后按 `[weapon+0xAC]` 取战斗部 |

**怎么重新推导**：

1. `GetFireError` 的虚表槽可以用 YRpp 的相邻函数地址（`GreatestThreat`/`SetTarget` 的 `JMP_THIS` 地址）与 vtable 内容对照确认；`0x6FC0B0` 也可以直接由 `InfantryClass::GetFireError`（`0x51C8B0`）内部的 `CALL` 目标反查（`Phobos\src\Ext\Infantry\Hooks.Firing.cpp` 有它的地址）。
2. "这个地址会不会被 Ares/Phobos 抢"必须查：
   * Ares 3.0：`D:\Games\Ra2\Ares.dll.inj`（纯文本清单），或从 Ares.dll 自带的 `.syhks00` 段读（表项 `{addr, size, name, pad}` 16 字节）；
   * Phobos：在 `Phobos\src` 里 grep `DEFINE_HOOK`。
   * 已知 Ares 在 `GetFireError` 里占了 `0x6FC0D3`（`TechnoClass_CanFire_DisableWeapons`，条件返回 `0x6FC0DF` **非零**）、`0x6FC339`、`0x6FC417`（`TechnoClass_CanFire_PsionicsImmune`，**永远返回非零**，绝对不能用）、`0x6FCA0D`、`0x6FCB6A`、`0x6FCBAD`；Phobos 占了 `0x6FC0C5`、`0x6FC339`、`0x6FC3AE` 等。**`0x6FC356` 两边都没占。**
3. 返回值必须是函数内部真实存在的尾声地址，用反汇编确认它的 `MOV EAX,<错误码>` 与栈清理字节数（这里是 `RET 0xC`，对应 3 个参数）。

---

## 5. 实测验证

**（1）编译**：Debug / Release 均通过、零警告；`.syhks00` = 0x90（9 个 hook），导出表新增：

```
HAres_TechnoClass_GetFireError_MindControlShield
HAres_TechnoClass_AI_MindControlShield
```

**（2）抢占字节**（`read_bytes`）：

```
006fc356  8A 87 42 01 00 00   MOV AL,[EDI+0x142]        ; 6 字节，指令边界完整
006f9e50  83 EC 68 53 55      SUB ESP,0x68 / PUSH EBX / PUSH EBP   ; 5 字节
```

**（3）链路冲突分析**（反编译 Ares 3.0 的处理函数 + Phobos 源码）：

| 地址 | Ares 占用 | Ares 返回值 | Phobos 占用 | 结论 |
|---|---|---|---|---|
| **`0x6FC356`** | 否 | — | 否 | ✅ 采用 |
| `0x6F9E50` | 是（`TechnoClass_Update`） | **恒为 0** | 是（`TechnoClass_AI`） | ✅ 可用，HAres 也必须 `return 0` |
| `0x6FC0C5` | 否 | — | 是（`return 0`） | ✅ 可用（备选） |
| `0x6FC0D3` / `0x6FC417` / `0x6FC339` | 是 | **非零**（会截断链） | 部分 | ❌ 不可用 |
| `0x471FF0` / `0x471D40` | 否 | — | **整函数替换** | ❌ 不可 hook；✅ 可以调用 |
| `0x471E34` / `0x4720E8` | 否 | — | 否 | ⚠️ 空闲但被上一条的整函数替换"屏蔽"，永不执行 |

**（4）实机 —— 开火屏蔽：✅ 已通过。** 自动对局里把屏蔽区间挂在 GI（`[E1] MindControlShield.Range=10`）和苏联建造厂（`Range=15`）上，`Houses=all`：

```
[MindControlShield] E1: Range=10.00 Houses=0x7 ReleaseMindControl=1 BlockAttack=1
[MindControlShield] NACNST: Range=15.00 Houses=0x7 ReleaseMindControl=1 BlockAttack=1
[MindControlShield] YURIPR may not fire mind control weapon SuperMindControl from inside a shield zone (block 1)
... (block 2..5)
[MindControlShield] PLA may not fire mind control weapon MindControl from inside a shield zone (block 1)
... (block 2..5)
```

两种心控武器使用者都被拦住了：原版的**尤里改（`YURIPR`，武器 `SuperMindControl`）**和测试用的**解放军（`PLA`，被临时换成 `MindControl`）**。这说明 `0x6FC356` 的注入、`EDI` 上的武器判定、以及跳 `0x6FC0DF` 返回 `FireError::REARM` 都在真实游戏里成立；`gamemd.exe` 全程无异常。

**（5）实机 —— 解除心控：✅ 已通过。** 先看到"看到了被心控的单位、但它不在任何区间内"的诊断，随后**同一个单位进入区间时被释放**，并且在一局里反复发生 10 次：

```
[MindControlShield] CIV1 is controlled (original owner Neutral) but outside every shield zone (log 1..5)
...
[MindControlShield] released CIV2 (original owner Neutral) inside a shield zone
[MindControlShield] released CIV1 (original owner Neutral) inside a shield zone
[MindControlShield] released CIV3 (original owner Neutral) inside a shield zone
... (同一局共 10 次)
```

被反复抓走又释放的是中立平民（`CIV1`/`CIV2`/`CIV3`，原主 `Neutral`）：尤里 AI 抓走平民，平民一进入 GI/建造厂的屏蔽区间就被立刻归还，AI 再抓、再放。这条日志同时验证了：控制节点被正确遍历、`OriginalOwner` 参与了房子过滤、以及引擎的 `FreeUnit` 被正确调用（归还给 `Neutral`，而不是留在当前控制者名下）。

> 这一步还修掉了自己写出来的一个 bug：第一版日志里 `original owner` 打印为空 —— 因为 `pManager->FreeUnit(pUnit)` 内部会 `GameDelete(pNode)` **删掉控制节点**，而日志是在调用*之后*才去读 `pNode->OriginalOwner`，属于 use-after-free（读到已释放内存）。现在先把 `OriginalOwner` 和它的 ID 取出来，再调用释放。

---

## 6. 已知限制

* **只解除"有控制节点"的心控**：常规心控和 Ares 的永久心控都走 `CaptureManager`，所以都在覆盖范围内；但如果某个单位是被 TAction 直接改写 `MindControlledByHouse` 而没有控制节点的，`FreeUnit` 找不到节点会返回 `false`，HAres 只记一条日志（不猜原主，避免归错阵营）。
* **没有穷尽所有开火旁路**：已确认 `GetFireError` 覆盖手动攻击与 AI 攻击、以及四个兵种类（Infantry/Unit 显式汇入基类，Aircraft/Building 的 vtable 指向同一实现）。UI 强制开火、超级武器、Spawner 子机这类特殊路径未逐条追查；若发现漏网，可在 `0x6FDD50`（`TechnoClass::Fire`）再补一道硬后盾 —— 注意那里是 `RET 8`，**不能**简单返回一个地址否决，需要跳到函数内部的 `return nullptr` 路径。
* 屏蔽区间不区分"谁的心控武器"：区间一视同仁（由 `MindControlShield.Houses` 过滤的是**攻击者所属方**）。
* 判定用平面距离，不考虑高度；`InLimbo`（建筑在"离场"状态）的提供者不算生效。
* 每帧重建区间列表是 O(全场单位数) 的哈希查表，实测足够轻（只在有配 `MindControlShield.Range` 的类型时才做）。

---

## 7. 变更记录

| 日期 | 变更 |
|---|---|
| 本次交付 | 新增 `MindControlShield.*` 键、`0x6FC356`（开火否决）与 `0x6F9E50`（每帧解除心控）两个 hook |
| 本次交付（实机反馈修复） | 修掉释放日志里的 use-after-free：`FreeUnit` 会删掉控制节点，原主必须在调用前取出；同时加入"被控但不在区间内"与"开火被拦"两条限量诊断日志 |
