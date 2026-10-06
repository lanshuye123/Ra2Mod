# HAres 开发指南

> 本文档面向"魔改《红色警戒2：尤里的复仇》引擎"这一目标，基于本机 **已实际编译并运行验证** 的环境写成。
> 文中标注 ✅ 的内容是本次实测跑通的；标注 ⚠️ 的是已知限制或未实测的路径。

---

## 0. 速查（TL;DR）

```bat
:: 构建
cd D:\Codes\RA2Mods\HAres
scripts\build_debug.bat          :: -> Debug\HAres.dll + HAres.pdb
scripts\build_release.bat        :: -> Release\HAres.dll + HAres.pdb

:: 部署到游戏目录（同时会放好 RunHAres.bat）
scripts\deploy.bat Release D:\Games\Ra2

:: 启动：直接双击/运行游戏目录下的 RunHAres.bat
::   = Syringe.exe "gamemd.exe" --handshakes --args="-WIN -CD -NOLOGO -LOG -AI-CONTROL"
D:\Games\Ra2\RunHAres.bat

:: 看日志
D:\Games\Ra2\HAres.log            :: 自己的日志
D:\Games\Ra2\syringe.log          :: 注入/hook 装载情况
D:\Games\Ra2\debug\debug.log      :: Ares/Phobos 的日志

:: 还原游戏目录到验证前状态
scripts\restore_game_dir.bat
```

**核心结论**：在 Ares 只开源到 0.A（2016）而实机装的是闭源 Ares 3.0 的现实下，"魔改 Ares"最可行的形态是
**写一个和 Ares/Phobos 并存的扩展 DLL（HAres.dll）**，用同一套 Syringe + YRpp + hook 技术栈去改引擎行为。
本仓库就是这样一个已经能编译、能注入、能执行 hook 的最小可用工程。

---

## 1. 架构与工作原理

### 1.1 组件关系

```
gamemd.exe  (尤里的复仇 1.11, 32 位 x86, 映像基址 0x400000)
   │
   │  Syringe.exe 启动 gamemd.exe 并作为调试器附加
   │      ↓ 在每个"钩子地址"写入 5 字节 JMP
   ├── Ares.dll      (闭源 3.0)   引擎本体扩展：新增逻辑、超武、AI 等
   ├── Phobos.dll    (开源)       在 Ares 之上继续扩展：更多新逻辑
   └── HAres.dll     (本仓库)     你自己的扩展，与上面两个并存
        │
        └── YRpp/  (纯头文件库)   描述 gamemd.exe 里的类、结构、全局变量、虚表
```

| 组件 | 作用 | 本机位置 |
|---|---|---|
| **gamemd.exe** | 游戏本体，被修改的目标 | `D:\Games\Ra2\gamemd.exe` |
| **Syringe / SyringeEx** | 注入器：启动游戏、加载 hook DLL、在指定地址植入 JMP | `D:\Codes\RA2Mods\SyringeEx` ✅ 已编译 |
| **YRpp** | 逆向成果的头文件化：`TechnoClass`、`RulesClass`、`DEFINE_REFERENCE(...)` 等 | `Phobos\YRpp`、`HAres\YRpp` |
| **Ares** | 引擎扩展本体（实机为闭源 3.0；开源版本只到 0.A） | `D:\Games\Ra2\Ares.dll` |
| **Phobos** | Ares 之上的开源扩展，也是最好的**代码范本** | `D:\Codes\RA2Mods\Phobos` ✅ 已编译 |
| **HAres** | 你的扩展 DLL | `D:\Codes\RA2Mods\HAres` ✅ 已编译并注入验证 |

### 1.2 注入与 hook 机制（最关键的一节）

**第一步：声明 hook 点。**
`DEFINE_HOOK(地址, 函数名, 长度)` 在编译期做两件事：

```cpp
#define DEFINE_HOOK(hook, funcname, size) \
    declhook(hook, funcname, size) \      // 往 .syhks00 段塞一条 hookdecl 记录
    EXPORT_FUNC(funcname)                 // 展开成导出的处理函数
```

`.syhks00` 段里的记录结构（`YRpp/Syringe.h`，16 字节对齐）：

```cpp
__declspec(align(16)) struct hookdecl
{
    unsigned int hookAddr;   // gamemd.exe 内的绝对地址
    unsigned int hookSize;   // 抢占（steal）的原始指令字节数
    const char*  hookName;   // 导出函数名，Syringe 用它 GetProcAddress
};
```

**第二步：Syringe 读取并装载。**
Syringe 在**游戏启动之前**扫描游戏目录下所有 DLL：

1. 优先读 PE 的 `.syhks00` 段（`ParseHooksSection`）；
2. 没有该段则回退读 `<dll名>.inj` 文本文件（`ParseInjFileHooks`，Ares 3.0 仍在用这个）；
3. 若 DLL 导出了 `SyringeHandshake`，则调用它让 DLL 自己决定是否接受当前游戏版本；
4. 把所有 hook 按地址汇总到 `Breakpoints[地址] -> vector<Hook>`。

**第三步：运行期改写代码。**
对每个 hook 地址，Syringe 生成一段 trampoline：

```
PUSHAD / PUSHFD                     ; 保存现场
PUSH <hook地址>                     ; 作为 R->Origin() 的来源
PUSH ESP                            ; 作为 REGISTERS* 参数
CALL <你的导出函数>
ADD ESP, 8
; 若你的函数通过 R->ESP() 指定了显式返回地址，就从 fs:0x14 取出来直接跳
; 否则执行被抢占的原始字节，再跳回 地址+长度
```

同时把该地址开头 `长度` 个字节**备份到 trampoline**，然后原地写入 `E9 xx xx xx xx`（5 字节相对 JMP）。

**第四步：`REGISTERS*` 让你拿到现场。**
参数是 `REGISTERS* R`，用宏读取：

```cpp
GET(TechnoClass*, pThis, ESI);     // ESI 寄存器里的对象指针
GET_STACK(int, damage, 0x4);       // ESP+0x4 处的参数
LEA_STACK(CoordStruct*, pCoord, 0xC);  // 取栈上变量的地址
REF_STACK(CoordStruct, coord, 0xC);    // 取栈上变量的引用
R->EAX(123);                       // 回写寄存器
```

### 1.3 hook 链：为什么 HAres 能和 Ares/Phobos 抢同一个地址 ✅

这是本工程能成立的前提，务必理解：

Syringe/SyringeEx 对同一个地址上的多个 hook 是**链式调用**的（代码注释原文：*"return 0 hooks are chained"*）。
同一地址上的**抢占长度取所有 hook 的最大值**：

```cpp
auto const [count, overridden] = std::accumulate(
    it.second.hooks.cbegin(), it.second.hooks.cend(), std::make_pair(0u, 0u),
    [](auto acc, auto const& hook) {
        if (hook.proc_address) {
            if (acc.second < hook.num_overridden)
                acc.second = hook.num_overridden;   // ← 取最大值
            acc.first++;
        }
        return acc; });
```

由此得到三条实用规则：

1. **`return 0` 就是"交给链上的下一个 hook"**，不是"立刻返回游戏"。
2. 抢占长度不一致是安全的，**取最大值**；但你自己声明的长度必须覆盖完整指令。
3. Ares(3.0) 和 Phobos 都 hook 了 `0x7CD810`，本次 HAres 也 hook 同一地址，三个都正常执行 ✅
   （实测 hook 总数 2318 → 加入 HAres 后 2323，正好 +5）。

> 反过来也要注意：**如果你 `return 0x某个地址`，链上后面的 hook 就不会执行了**。
> 只有当你确实要短路原始逻辑时才这么写。

### 1.4 ABI 契约（不能乱改的编译选项）

注入的代码和游戏**共用同一个栈、堆和 C++ 对象**。下面的选项搞错不会编译报错，而是运行期静默内存损坏：

| 选项 | MSBuild 属性 | 原因 |
|---|---|---|
| `/MT` | `RuntimeLibrary=MultiThreaded` | 必须和游戏的静态 CRT 一致，**绝不能用 `/MD`** |
| `/Gz` | `CallingConvention=StdCall` | 默认调用约定与游戏二进制一致 |
| `/Zp8` | `StructMemberAlignment=8Bytes` | 结构体布局必须和游戏一致 |
| `/GS-` | `BufferSecurityCheck=false` | 游戏没有栈 cookie，注入代码也不该有 |
| `/GR-` | `RuntimeTypeInfo=false` | 无 RTTI，`dynamic_cast` 不可用，改用 YRpp 的 `specific_cast`/`generic_cast` |
| 关闭异常 | `ExceptionHandling=false` | 不要写 `try/catch/throw` |
| `/std:c++20` + `/permissive-` | `LanguageStandard` / `ConformanceMode` | YRpp 头文件按这个标准写 |
| `%(Directory)` 目标文件名 | `ObjectFileName` | **必须**：Ares/Phobos 里大量同名 `.cpp`（如 `Ext/WarheadType/Hooks.cpp` 和 `Ext/WeaponType/Hooks.cpp`），MSBuild 默认会互相覆盖 `.obj` 导致链接出错 |
| `SYR_VER=2` | 预处理宏 | 选用 `.syhks00` 段格式而非老式 `.inj` |
| Win32/x86 | 平台 | 游戏是 32 位，没有 x64 版本 |

---

## 2. 环境（✅ 已实测跑通）

### 2.1 实测环境清单

| 项 | 值 |
|---|---|
| Visual Studio | 2022 Community，`D:\Program Files\Microsoft Visual Studio\2022\Community` |
| MSVC 工具集 | `14.44.35207`（v143） |
| MSBuild | `...\2022\Community\MSBuild\Current\Bin\MSBuild.exe` |
| Windows SDK | `10.0.22621.0`、`10.0.26100.0` |
| vswhere | 各仓库 `scripts\vswhere.exe`（已内置，无需另装） |
| Git | 2.39.1，GitHub 访问统一走 `https://ghfast.top/` 前缀 |
| 游戏 | `D:\Games\Ra2`，`gamemd.exe` 版本 1.11 |
| 已装 Ares | `Ares.dll` 产品版本 **3.0**（FileVersion 20.333.289），**闭源** |
| 原始 Syringe | `Syringe.exe` 0.7.3.0（闭源 baseline，325,120 字节） |

### 2.2 工作区结构

```
D:\Codes\RA2Mods\
├── Ares\               Ares 0.A 源码（最后一次开源版本，2016-01-02）
├── Ares-YRPP\          与 Ares 0.A 配套的 YRpp（2016-03-07）
├── Phobos\             现代开源扩展，最好的代码范本  ✅ 已编译
│   └── YRpp\           子模块 (Phobos-developers/YRpp @ 8468aab5)
├── Phobos-YRpp\        YRpp 的独立克隆
├── SyringeEx\          Syringe 的现代化开源分支  ✅ 已编译
└── HAres\              ← 本指南的项目  ✅ 已编译 + 已注入验证
    └── YRpp\           子模块 (同上, 固定到 8468aab5)
```

### 2.3 构建命令与产物（全部实测通过）

| 项目 | 命令 | 产物 | 实测大小 |
|---|---|---|---|
| Phobos Debug | `Phobos\scripts\build_debug.bat` | `Phobos\Debug\Phobos.dll` | 2,205,696 |
| Phobos Release | `Phobos\scripts\build_release.bat` | `Phobos\Release\Phobos.dll` | 1,712,640 |
| SyringeEx Release | `SyringeEx\scripts\build_release.bat` | `SyringeEx\Release\Syringe.exe` | 868,864（ver 0.1.0.2） |
| HAres Debug | `HAres\scripts\build_debug.bat` | `HAres\Debug\HAres.dll` | 133,120 |
| HAres Release | `HAres\scripts\build_release.bat` | `HAres\Release\HAres.dll` | 132,096 |

> 首次构建 Phobos 前必须初始化子模块。若 `git submodule update --init` 拉不动，先给仓库加 URL 重写：
> ```bat
> git config url."https://ghfast.top/https://github.com/".insteadOf "https://github.com/"
> git submodule update --init --recursive
> ```

### 2.4 已完成的验证与证据 ✅

编译产物结构检查（`dumpbin /headers`、`dumpbin /exports`）：

- `Phobos\Debug\Phobos.dll`：8 个段，含 `.syhks00`（0x7000 = 28,672 字节 hook 声明）和 `.patch`（0x1000）。
- `HAres\Release\HAres.dll`：`.syhks00` 大小恰为 **0x50 = 80 字节 = 5 个 hook × 16 字节**。
  导出符号齐全：
  ```
  HAres_ExeRun              HAres_ExeTerminate
  HAres_GScreenClass_DrawText   HAres_ScenarioClass_Start
  HAres_YR_CmdLineParse
  ESPModification   ZFPreservation   ReladdrInstructionFixup   ← SyringeEx 特性标志
  ```

在游戏里实际注入运行的证据：

**（1）用原版闭源 Syringe 0.7.3.0**
```
Recognized DLL: "Ares.dll"    → Handshake: "Found Yuri's Revenge 1.001 (modified). Applying Ares 3.0."
Recognized DLL: "HAres.dll"   ← 被识别
Recognized DLL: "Phobos.dll"
Done (2323 hooks added).      ← 原为 2318，+5 正好是 HAres 的 5 个 hook
```

**（2）用自己编译的 SyringeEx 0.1.0.2**
```
SyringeEx 0.1.0.2, based on Syringe 0.7.2.0
Done (3112 hooks added).
Set feature flag "ESPModification"          in "Phobos.dll" at 0x6116FAE0
Set feature flag "ZFPreservation"           in "Phobos.dll" at 0x6116FAE1
Set feature flag "ReladdrInstructionFixup"  in "Phobos.dll" at 0x6116FAE2
Set feature flag "ESPModification"          in "HAres.dll"  at 0x61FF0990
Set feature flag "ZFPreservation"           in "HAres.dll"  at 0x61FF0991
Set feature flag "ReladdrInstructionFixup"  in "HAres.dll"  at 0x61FF0992
```
→ 特性标志被正确写入 HAres.dll 的导出变量，说明整条 Syringe↔DLL 交互链路是通的。

**（3）HAres 的 hook 真的执行了** —— `D:\Games\Ra2\HAres.log`
```
[16:01:49.514] HAres v0.1.0.0 (release build)
[16:01:49.514] [Syringe] ESPModification=1 ZFPreservation=1 ReladdrInstructionFixup=1
[16:01:49.514] [Config] file: D:\Games\Ra2\HAres.ini
[16:01:49.514] [Config] ShowWatermark=1 VerboseLog=0 WatermarkCorner=0
[16:01:49.514] [Init] applying static patches
[16:01:49.514] [Init] complete
[16:01:49.553] [CmdLine] arg[0] = D:\Games\Ra2\gamemd.exe
```
- `ExeRun` hook（0x7CD810）执行 → 完成了初始化
- `CmdLineParse` hook（0x52F639）执行 → **正确读出了 ESI/EDI 里的 argv/argc**

**（4）HAres.dll 确实被加载进 gamemd.exe 进程** —— Ares 的 `debug\debug.log`
```
Module Phobos.dll base address : 0x610C0000.
Module HAres.dll base address : 0x61FD0000.
[Phobos] Detected Ares 3.0.
```

**（5）A/B 对照实验，证明 HAres 无害**

| 实验 | 结果 |
|---|---|
| A：原版 Syringe + 原版 Phobos，**无** HAres | 10s ALIVE → 20s 死亡；异常 `0xE06D7363`×2 → `0xC0000005 @ 0x006BDCAC` |
| B：原版 Syringe + 原版 Phobos，**有** HAres | 10s ALIVE → 20s 死亡；异常 `0xE06D7363`×2 → `0xC0000005 @ 0x006BDCAC` |

两次的 `debug.log` 结尾**完全一致**：
```
SetDisplayMode: 800x600x16
Display mode set
DSurface::Create_Primary - Creating surface
CreateSurface failed with error code 80070057
```
`0x80070057` = `E_INVALIDARG`。这是 DirectDraw 在当前会话下无法创建主表面导致的，
**与 HAres 无关**（去掉 HAres 一模一样）。

**（6）加上 `-WIN` 窗口模式后，游戏正常进入主菜单，全部 hook 都被证实执行** ✅

这是最终、最完整的一次验证：

```
syringe.log:
  arguments = "gamemd.exe --handshakes --args=-WIN -CD -NOLOGO -LOG -AI-CONTROL "
  Handshake: Answers "Found Yuri's Revenge 1.001 (modified). Applying Ares 3.0."
  Recognized DLL: "Ares.dll" / "HAres.dll" / "Phobos.dll"
  Done (3112 hooks added).
  → gamemd 存活，窗口标题 "Yuri's Revenge"

HAres.log:
  [Init] complete
  [CmdLine] arg[0] = D:\Games\Ra2\gamemd.exe
  [CmdLine] arg[1] = -WIN
  ... arg[5] = -AI-CONTROL
  [Hook] ScenarioClass::Start #1
  [Hook]   FileName="XMP22S8.MAP"
  [Hook]   Rules loaded, MessageDelay=1073741824
  [Hook] GScreenClass::DrawText fired (surface 1112x720)

debug\debug.log:
  Initialized Ares version: 20.333.289
  [Phobos] Initialized version: v0.5.0.0 @ 3a26b8c0 @ refs/heads/develop
```

所以 HAres 的 5 个 hook 中，`ExeRun`、`YR_CmdLineParse`、`ScenarioClass::Start`、
`GScreenClass_DrawText` 四个**已用实际执行结果证实**（`ExeTerminate` 只在进程正常退出时才跑，
测试中是强制结束进程的，所以未观察到）。

> **结论：构建环境、注入链路、参数传递、hook 执行全部验证通过，
> Ares 3.0 + Phobos + HAres 三者共存无冲突。**
> 关键前提是**窗口模式**：RA2 引擎太老，不加 `-WIN` 会在 DirectDraw 建表面时失败退出。

---

## 3. 关于"魔改 Ares"的路线选择

### 3.1 Ares 的现状（重要）

我把 `Ares` 仓库所有分支都翻了一遍：

```
origin/master   4f1d9299  2016-01-02  Reset release flag and branch info   ← 最新代码
origin/v08      23da0b22  2014-10-03
origin/stable   5acf9e59  2011-11-25
（其余 2016 年之后的提交都在 origin/gh-pages 文档分支上，2025-08-22）
```

**结论：Ares 公开源码的最新版本就是 0.A（2016-01-02），之后 9 年的开发全部闭源。**
你游戏目录里装的是 Ares **3.0**，那是闭源产物，拿不到源码。

另外，Ares 仓库里**没有任何构建系统**（没有 `.sln`、没有 `.vcxproj`）。
官方当年的做法是用一个独立的 C# 工具 **AresBuilder**（`svn://www.renegadeprojects.com/aresbuilder`，早已不可访问）
来绕过 MSBuild 的同名 `.obj` 覆盖问题。这就是为什么 `HOW-TO-CONTRIBUTE-TO-ARES.md` 里写着
*"At the moment, the Visual Studio IDE cannot compile/link Ares correctly."*

### 3.2 路线 B：写并存扩展 DLL（本仓库采用）✅

既然 Ares 3.0 改不了源码、Ares 0.A 又太老（会丢掉 9 年的功能），实际可行的做法是：

> **用和 Ares/Phobos 完全相同的技术栈，写一个自己的 DLL 挂到 gamemd.exe 上。**

- 保留 Ares 3.0 的全部功能
- 保留 Phobos 的全部功能
- 通过 hook **只改你想改的地方**
- 这也正是 Phobos 自己在做的事 —— HAres 只是"第二个 Phobos"

这正是本次交付的 `D:\Codes\RA2Mods\HAres`。它的能力边界是：
**凡是你能在反汇编里找到地址、并且能用 YRpp 描述出来的东西，都能改。**

### 3.3 路线 A：改 Ares 0.A 本体 ⚠️（未实测，仅记录路径）

如果你确实需要"改 Ares 本体"（比如要动 Ares 自己的数据结构），只能基于 0.A。要点：

1. `Ares-YRPP` 已经就位，其 `Syringe.h` **已经**使用 `.syhks00` 段（不需要再生成 `.inj`）。
2. 需要自己造一个 `.vcxproj`：把 `Ares\src\**\*.cpp` 全部列入 `<ClCompile>`，
   **务必**设置 `ObjectFileName = $(IntDir)\%(Directory)` 来解决同名 `.obj` 冲突。
3. 编译选项照抄 `HOW-TO-CONTRIBUTE-TO-ARES.md` 的记录（与 §1.4 基本一致）：
   ```
   /Ox /W4 /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /EHsc /Gz /Zp8 /Gm /Zi /GS-
   链接: /MANIFEST:NO /DLL /SUBSYSTEM:WINDOWS /DEFAULTLIB:"user32.lib" "dbghelp.lib" "ole32.lib"
   ```
   （Ares 0.A 是 2016 年的代码，用 v143 编译大概率需要少量修补。）
4. **代价**：产出的 Ares.dll 是 0.A 版本，会**替换**掉你现在的 Ares 3.0，丢掉 9 年的功能。
   而且 Ares 0.A 与 Ares 3.0 的存档格式不兼容。

> 我的建议：除非你明确要改 Ares 内部实现，否则一律走路线 B。

---

## 4. HAres 项目结构

```
HAres\
├── HAres.sln                      VS2022 解决方案
├── HAres.vcxproj                  工程文件（新增 .cpp 必须登记到这里）
├── HAres.props                    编译/链接设置（§1.4 的 ABI 契约都在这里）
├── YRpp\                          git 子模块，YRpp @ 8468aab5
├── scripts\
│   ├── vswhere.exe                定位 VS 安装（内置，不依赖 PATH）
│   ├── run_vsdevcmd.bat           启动 VS 开发者命令行
│   ├── run_msbuild.bat            在开发者命令行里调用 msbuild
│   ├── build.bat / build_debug.bat / build_release.bat
│   ├── clean.bat
│   ├── deploy.bat                 拷贝 DLL+PDB 到游戏目录
│   └── restore_game_dir.bat       还原游戏目录到验证前状态
├── docs\
│   └── DEVELOPMENT.md             本文档
└── src\
    ├── HAres.version.h            版本号 / 产品名（改名从这里改）
    ├── HAres.h                    对外状态声明（刻意不引入 YRpp）
    ├── HAres.cpp                  生命周期、配置、DllMain、日志初始化
    ├── version.rc                 DLL 版本资源
    ├── Utilities\
    │   ├── Debug.h / Debug.cpp    文件日志（含 hex dump）
    │   ├── Macro.h                静态补丁宏：DEFINE_PATCH / DEFINE_JUMP / ...
    │   └── Patch.h / Patch.cpp    补丁执行引擎 + .patch 段遍历
    └── Misc\
        └── Hooks.Demo.cpp         所有 hook 都写在这里（含教学模板）
```

**分层原则**：`HAres.h` / `HAres.cpp` 只依赖 Win32，不引入 YRpp；
所有和游戏对象打交道的代码放在 `src\Misc\` 或 `src\Ext\` 下并 `#include <YRPP.h>`。
这样配置/日志这部分逻辑不会因为 YRpp 变化而受影响。

---

## 5. 开发工作流

### 5.1 新增一个 hook 的完整流程

**① 确定地址。** 见 §5.2。

**② 在 `src\Misc\Hooks.Demo.cpp` 写：**

```cpp
DEFINE_HOOK(0x683E7F, HAres_ScenarioClass_Start, 0x7)
{
    // 可选：给返回地址起名字，比裸数字可读
    enum { Continue = 0x683E86, Skip = 0x683F00 };

    auto const pScenario = ScenarioClass::Instance;
    if (!pScenario)
        return 0;

    Debug::LogLine("[Hook] scenario=%s", pScenario->FileName);

    return 0;      // 0 = 执行被抢占的原始指令，然后继续
}
```

**③ 如果新增了 `.cpp` 文件，登记到 `HAres.vcxproj`：**

```xml
<ClCompile Include="src\Misc\Hooks.MyFeature.cpp" />
```

**④ 构建 + 部署 + 跑：**

```bat
scripts\build_release.bat
scripts\deploy.bat
D:\Games\Ra2\RunHAres.bat
```

**⑤ 看 `HAres.log` 确认 hook 执行。**

> 命名约定：`类名_方法_用途`，并且**加上 `HAres_` 前缀**。
> 因为导出符号名在整个进程里是全局的，Ares 和 Phobos 都有叫 `ExeRun` 的导出，重名有风险。

### 5.2 如何确定 hook 地址与 size

这是整个开发中最需要"逆向"的一步。三种办法，按性价比排序：

**（1）抄现成的（最快，推荐入门）**
Ares 0.A 源码和 Phobos 源码里已经标注了**数千个** `DEFINE_HOOK(地址, ...)`，
每一个都是经过验证的指令边界 + 合理长度。直接 `grep`：

```powershell
# 在 Phobos 里找和伤害相关的 hook 点
Select-String -Path "D:\Codes\RA2Mods\Phobos\src\*\*.cpp","D:\Codes\RA2Mods\Phobos\src\*\*\*.cpp" -Pattern "DEFINE_HOOK.*Damage"
# 在 Ares 0.A 里找
Select-String -Path "D:\Codes\RA2Mods\Ares\src\*\*.cpp","D:\Codes\RA2Mods\Ares\src\*\*\*.cpp" -Pattern "DEFINE_HOOK"
```

Ares 3.0 的 `Ares.dll.inj` 也是一份现成的地址清单（`D:\Games\Ra2\Ares.dll.inj`，71 KB）：
```
; \Ares.TechnoType.cpp
71A92A = _Temporal_AvoidFriendlies, 5
```

**（2）IDA Pro / Ghidra 反汇编（主力手段）**
用 IDA 打开 `D:\Games\Ra2\gamemd.exe`（32 位 x86，基址 0x400000），
然后要么人工看，要么用 [IDA Pro MCP server](https://github.com/mrexodia/ida-pro-mcp) 让 AI 直接查反汇编。

**（3）确认 size 的规则**
`size` = **抢占了几个字节的原始指令**，必须满足：

- 必须是**完整指令**的边界结束（不能切在指令中间）
- 必须 **≥ 5**（JMP 本身 5 字节）；如果第 5 字节落在一条指令中间，就要扩到那条指令结束
- 可以 < 5（当尾部是 NOP 填充时）
- 同级多 DLL 时取最大值（§1.3），所以和 Ares/Phobos 用同地址时**照抄它们的 size 最省事**

### 5.3 读取寄存器与栈

```cpp
GET(TechnoClass*, pThis, ESI);        // pThis = (TechnoClass*)R->ESI
GET(char**, ppArgs, ESI);
GET(int, nNumArgs, EDI);
GET_STACK(int, damage, 0x4);          // 相对 hook 入口处 ESP 的偏移
LEA_STACK(CoordStruct*, pCoord, 0xC);
REF_STACK(CoordStruct, coord, 0xC);
GET_BASE(int, arg0, 0x4);             // EBP 相对；游戏里几乎不用（见下）
STACK_OFFSET(cur, wanted)             // 偏移换算
R->EAX(value);  R->EDX(value);  R->ESP(value);
R->Origin()                           // 当前 hook 的地址（一个函数挂多个地址时用）
```

⚠️ **不要假设 EBP 帧指针**：编译 gamemd.exe 的编译器有 bug，EBP 几乎从不作为帧指针使用，
所以 `GET_BASE` 基本没用，**一律用 `GET_STACK`**。

⚠️ **不要用内联汇编改真实 ESP**（`push`/`pop`），会破坏栈。
需要改变栈深度后跳到别处时，用 `R->ESP(...)`，由 SyringeEx 的 `ESPModification` 特性支持。

### 5.4 返回值语义

| 返回 | 行为 | 用途 |
|---|---|---|
| `return 0;` | 执行被抢占的原始指令，然后在 `地址+size` 继续；链式时**交给下一个 hook** | 观察/增强，不改变原逻辑 |
| `return 0x地址;` | 直接跳过去，**跳过**被抢占的原始字节；链上后续 hook 不再执行 | 替换或短路原逻辑 |
| `return R->Origin() + 偏移;` | 相对当前 hook 地址跳转 | `DEFINE_HOOK_AGAIN` 一个函数服务多个地址时 |

一个函数服务多个地址：

```cpp
DEFINE_HOOK_AGAIN(0x43C30A, HAres_TechnoClass_Draw, 0xC)   // 只声明，不开函数体
DEFINE_HOOK(0x43BF8B, HAres_TechnoClass_Draw, 0x5)         // 这行开函数体
{
    // R->Origin() 区分是哪个地址进来的
    return R->Origin() + 0xF;
}
```
注意 `DEFINE_HOOK_AGAIN` 必须写在 `DEFINE_HOOK` **之前**。

### 5.5 静态补丁与运行时补丁

**静态补丁**（编译期写进 `.patch` 段，`ExeRun` 时由 `Patch::ApplyStatic()` 一次性应用）：

```cpp
DEFINE_PATCH(0x123456, 0x90, 0x90);                      // 写原始字节
DEFINE_JUMP(LJMP, 0x6BB596, 0x6BB5A3);                   // E9 远跳
DEFINE_JUMP(CALL, 0x123456, 0x654321);                   // E8 调用
DEFINE_JUMP(CALL6, 0x48A3A0, 0x654321);                  // FF15 调用 + NOP（6 字节）
DEFINE_JUMP(VTABLE, 0x7E2280, &MyHandler);               // 改写虚表项
DEFINE_FUNCTION_JUMP(CALL, 0x48A3A0, MyFunc);            // 目标是 C++ 函数指针
```

**运行时补丁**（当"要不要打"取决于运行期状态时）：

```cpp
Patch::Apply_LJMP(0x6BB596, 0x6BB5A3);
Patch::Apply_CALL(0x48A3A0, &MyFunc);
Patch::Apply_CALL6(0x48A3A0, &MyFunc);
Patch::Apply_VTABLE(0x7E2280, &MyFunc);
Patch::Apply_RAW(0x6BB596, { 0x90, 0x90 });
```

**一个容易踩的坑**：静态补丁记录放在 `.patch` 段里，开了 `/OPT:REF` 后链接器可能把它当死数据丢掉。
`HAres\src\Utilities\Macro.h` 里的 `_ALLOCATE_STATIC_PATCH` 通过 YRpp 的
`_YR_DEFINE_INCLUDE_ANCHOR` 解决（本质是 `#pragma comment(linker, "/include:符号")` 强制保留）——
**自己新增静态补丁宏时不要漏掉这一步**。

### 5.6 扩展游戏类（Ext 模式）

要"给 `TechnoClass` 加字段"这种需求，RA2 modding 的标准做法是**旁挂扩展对象**，而不是改游戏类本体：

```
src\Ext\<类名>\
├── Body.h      声明 <类名>Ext（派生自 AbstractExt），以及 ExtContainer / ExtMap
├── Body.cpp    构造函数、LoadFromINIFile、Serialize（存档）、共用 hook
└── Hooks.cpp   DEFINE_HOOK(...) 具体逻辑
```

要点：

- **一个游戏对象对应一个 Ext 对象**，用 map 关联，例如 Phobos 的
  `TechnoExt::Fetch(pThis)`（找不到直接 fatal）或 `TechnoExt::TryFetch(pThis)`（返回 null，
  读存档过程中要用这个）。
- 新增 Ext 类之后必须在 **`Phobos.Ext.cpp` 的 `PhobosTypeRegistry`** 里注册（HAres 里对应你自己建一个注册点）。
- **凡是需要跨帧保存的新字段，都必须在 `Serialize` 里用流读写**，否则多人游戏会不同步（desync）。
- 用 `GameCreate<T>(...)` / `GameDelete(ptr)` 分配/释放**游戏中存在的对象**（用游戏堆）；
  DLL 独有的对象（比如 Ext 类）可以直接 `new`/`delete`。

> 想照抄现成范本，最好的教材是 `Phobos\src\Ext\Techno\`（最完整）和
> `Phobos\src\Ext\WarheadType\`（较简单）。

### 5.7 INI 配置

HAres 演示了最省事的一种：用 Win32 的 `GetPrivateProfileIntA` 直接读游戏目录下的 `HAres.ini`：

```ini
[General]
ShowWatermark=1        ; 是否在画面上显示版本水印
VerboseLog=0           ; 额外诊断（会启用补丁机制自检）
WatermarkCorner=0      ; 0=左上 1=右上 2=左下 3=右下
```

代码在 `HAres::LoadConfig()`（`src\HAres.cpp`）。注意**路径必须从
`GetModuleFileNameA(NULL)` 推导**，不能依赖当前工作目录——注入进程的工作目录不可靠。

要读 `rulesmd.ini` 这类游戏 INI，就要走游戏的 `CCINIClass`（YRpp 里有定义），
在 `ExeRun` 之后再读，因为规则文件是游戏加载的。

### 5.8 存档序列化

只要你的扩展保存了状态，且这个状态会影响游戏逻辑，就必须序列化，否则：
单人读档错乱、多人 desync。

Phobos 的做法是 `PhobosStreamReader` / `PhobosStreamWriter` + `.Process()` 调用链，
入口在 `Phobos.Save.cpp` 挂的一系列 Save/Load hook。
`SNAPSHOT`/`SWIZZLE` 相关的 hook 也需要一并处理指针重定位。

---

## 6. 调试与排错

### 6.1 日志三件套

| 文件 | 来源 | 看什么 |
|---|---|---|
| `D:\Games\Ra2\HAres.log` | 自己（`Debug::LogLine`） | 你的 hook 有没有执行、参数对不对 |
| `D:\Games\Ra2\syringe.log` | Syringe | DLL 有没有被识别、hook 总数、异常码 |
| `D:\Games\Ra2\debug\debug.log` | Ares / Phobos（需 `-LOG` 参数） | 游戏初始化走到哪一步、模块加载基址、崩溃快照 |

`Debug` 类每次 `LogLine` 都 `fflush`，所以**崩溃前最后一行日志一定能看到**——
本次定位 `hInstance` 为空就是靠这个（见 §6.3）。

### 6.2 附加调试器

1. 把 `HAres.pdb` 和 `HAres.dll` 一起放进游戏目录（`deploy.bat` 已经做了）。
2. 正常启动游戏，等主菜单出现。
3. VS2022 → **调试 → 附加到进程** → 找到 `gamemd.exe` → 附加。
4. 调试 → 窗口 → 模块，确认 `HAres.dll` 已加载且符号已加载。
5. 在你的 hook 函数里下断点。注意 hook 是**热路径**，别在高频 hook 里打断点。

调试构建（`scripts\build_debug.bat`）保留了完整调试信息且不做优化。

### 6.3 崩溃定位流程（附本次真实案例）

**案例：`HAres` 第一次上机直接把游戏打崩**

`syringe.log`：
```
Exception (Code: 0xC0000005 at 0x61FD177E)!
The process tried to read from 0x0000003C.
Registers:
    EDI = 0x007CD80F   EIP = 0x61FD177E
```
`HAres.log` 最后一行是 `[Config] ShowWatermark=...`，**没有** `[Init] complete`。

逐步推理：
1. 崩在 `0x61FDxxxx`，而 HAres.dll 基址就在 `0x61FD0000` → 崩在自己的 DLL 里。
2. `HAres.log` 停在 `LoadConfig()` 之后、`Patch::ApplyStatic()` 之后的那行日志之前
   → 崩在 `Patch::ApplyStatic()` 里。
3. **读地址 `0x3C`** ← 这是决定性线索。`0x3C` 正是 `IMAGE_DOS_HEADER::e_lfanew` 的偏移。
   `NULL + 0x3C` 说明 `HAres::hInstance` 是空指针。
4. 回头看代码：`HAres::hInstance` 只在 `DllMain` 里赋值——而**我漏写了 `DllMain`**。

修复：补上 `DllMain` 并在 `GetSection` 里加空指针防御。重编译后一切正常。

**这套方法论可以复用**：
崩溃地址落在哪个模块 → 用 `dumpbin /headers` 或日志里的模块基址判断；
日志最后一行 → 锁定崩溃区间；**故障读地址** → 往往直接指向空指针的字段偏移。

### 6.4 Syringe 日志解读

```
SyringeDebugger::FindDLLs: Recognized DLL: "HAres.dll"     ← 被识别（有 .syhks00 或 .inj）
SyringeDebugger::Handshake: Answers "..." (0)              ← 该 DLL 导出了 SyringeHandshake
Done (2323 hooks added).                                   ← hook 总数
SyringeDebugger::HandleException: Creating code hooks.     ← 开始改写游戏代码
Exception (Code: 0xE06D7363 ...)                           ← C++ 异常（0xE06D7363 是 MSVC 的魔数）
Exception (Code: 0xC0000005 ...)                           ← 访问违例，通常就是真崩了
Done with exit code 0                                      ← 游戏正常退出
```

常见现象：

- **`0xE06D7363` 不一定是错误** —— 游戏内部会抛/接 C++ 异常，Syringe 只是报告了它。
  本次实测中，有无 HAres 都会出现，且游戏可以继续。
- **hook 总数没涨** → DLL 没被识别：检查 `.syhks00` 段是否存在、是否导出了 hook 函数、
  DLL 是否真的是 32 位。
- **DLL 被识别但游戏行为异常** → 大概率是 ABI 选项错了（§1.4）或 hook 地址/size 不合法。

---

## 7. 部署与还原

### 7.1 部署与启动

```bat
:: 编译产物 -> 游戏目录（同时会部署 RunHAres.bat，并在缺失时生成默认 HAres.ini）
HAres\scripts\deploy.bat Release D:\Games\Ra2

:: 改了 Phobos / SyringeEx 的话
copy /Y Phobos\Release\Phobos.dll    D:\Games\Ra2\
copy /Y Phobos\Release\Phobos.pdb    D:\Games\Ra2\
copy /Y SyringeEx\Release\Syringe.exe D:\Games\Ra2\
```

启动直接用部署好的 `D:\Games\Ra2\RunHAres.bat`（窗口模式 + 日志 + 跳过 logo），
或手工执行：

```bat
Syringe.exe "gamemd.exe" --handshakes --args="-WIN -CD -NOLOGO -LOG -AI-CONTROL"
```

#### ⚠️ SyringeEx 与老 Syringe 的参数语法不一样（很容易踩）

SyringeEx 重写了命令行解析（`SyringeEx\Support.h` 的 `parse_command_line`）：

| | 游戏参数怎么传 |
|---|---|
| **原版闭源 Syringe** | 直接跟在后面：`Syringe.exe "gamemd.exe" -WIN -CD -NOLOGO -LOG` |
| **SyringeEx** | **必须**用 `--args="..."`：`Syringe.exe "gamemd.exe" --args="-WIN -CD -NOLOGO -LOG"` |

SyringeEx 把**所有不是 exe 名、也不是 `--args=` 的参数**都归入 `syringe_arguments`，即它自己的选项，
**根本不会传给游戏**。后果非常隐蔽：

- 游戏实际上只收到 `argv[0]`，`-NOLOGO` / `-LOG` / `-WIN` 全部丢失；
- 于是 `debug\debug.log` 不生成（因为 Ares 没收到 `-LOG`）；
- 而且不加 `-WIN` 就会撞上 DirectDraw 建表面失败直接退出。

**怎么发现**：看 `HAres.log` 里的 `[CmdLine] arg[n]`。如果只有 `arg[0]`，就是参数没传进去。

> SyringeEx 另外有个 `--handshakes` 开关，默认**关闭**（即不调用 DLL 的 `SyringeHandshake`）。
> 上面的 `RunHAres.bat` 默认加上它，以还原原版 Syringe 的行为（日志里能看到
> `Answers "Found Yuri's Revenge 1.001 (modified). Applying Ares 3.0."`）。
> 实测**去掉它游戏也能正常跑**，Ares 一样会初始化 —— 它不是必需项。
> 唯一的区别是：若某个 DLL 的 handshake 会拒绝加载，加了这个开关它就不会被装载。

### 7.2 本次验证对游戏目录做了什么

**已备份**到 `D:\Games\Ra2\_dsh_backup_orig\`：`Syringe.exe`(0.7.3.0)、`Phobos.dll`、`Phobos.pdb`、`Ares.dll`。

**当前游戏目录状态**（为验证而更新为新编译版本）：

| 文件 | 现在 | 原来（备份里） |
|---|---|---|
| `Syringe.exe` | 868,864 我们自己编译的 SyringeEx 0.1.0.2 | 325,120 闭源 0.7.3.0 |
| `Phobos.dll` | 1,712,640 我们自己编译的 0.5.0.0 | 905,728 旧 nightly |
| `HAres.dll` / `.pdb` | 新增 | 无 |
| `HAres.ini` | 新增 | 无 |
| `Ares.dll` | 未改动（Ares 3.0） | 同 |

想要退回原状：
```bat
HAres\scripts\restore_game_dir.bat
```
（注意：新版 Phobos 依赖 SyringeEx，所以**两者要么一起换，要么一起退**。）

> 说明：`Phobos` 仓库当前 HEAD 的版本会检查 `SyringeFeatures` 标志，
> 在原版闭源 Syringe 下会弹框拒绝运行，所以必须配套部署 SyringeEx。本次已验证
> SyringeEx + 新 Phobos + HAres 三者协同正常。

---

## 8. 常见坑清单

| 坑 | 症状 | 解法 |
|---|---|---|
| 用 `/MD` 动态 CRT | 随机崩溃 / 堆损坏 | 必须 `/MT` |
| 同名 `.cpp` 覆盖 `.obj` | 链接错误、符号错乱 | `ObjectFileName=$(IntDir)\%(Directory)` |
| 静态补丁被 `/OPT:REF` 丢掉 | 补丁静默不生效 | 用 `_YR_DEFINE_INCLUDE_ANCHOR` 强制保留 |
| hook 地址不在指令边界 | 立刻崩 | 在反汇编里核对指令起始 |
| `size` 没覆盖完整指令 | 崩 / 行为诡异 | size 扩到最后一条被覆盖指令的末尾 |
| 用了 EBP 帧指针 | 读到垃圾 | 一律用 `GET_STACK` |
| 内联汇编改真实 ESP | 栈损坏 | 改用 `R->ESP(...)` |
| 写了 `try/catch/throw` | 编译失败或运行异常 | 异常已关闭，不要用 |
| 用了 `dynamic_cast` | 编译失败 | RTTI 已关，用 `specific_cast`/`generic_cast` |
| 高频 hook 里写日志/重活 | 帧率暴跌 | 每帧 hook 里只做必要的事 |
| 新增字段没序列化 | 读档错乱 / 多人 desync | 在 `Serialize` 中读写 |
| 导出函数名和各扩展重名 | 未定义行为 | 加 `HAres_` 前缀 |
| 漏写 `DllMain` | 空指针崩溃（见 §6.3） | 记得保存 `hInstance` |
| 忘记把新 `.cpp` 加进 vcxproj | 代码"没生效" | 登记到 `<ClCompile>` |
| 装了新 Phobos 却用老 Syringe | 弹框拒绝启动 | 配套用 SyringeEx |
| SyringeEx 下把游戏参数直接跟在后头 | 游戏只收到 `argv[0]`，`-LOG` 等全丢 | 必须 `--args="..."`（§7.1） |
| 不加 `-WIN` | DirectDraw `CreateSurface 80070057` 后退出 | 用窗口模式启动 |
| 连续 kill 后立刻再启动 Syringe | `拒绝访问` 无法启动 | 等 10 秒左右再启动 |
| 调用 Ares/游戏函数时猜调用约定 | 栈被破坏，几帧后跳到垃圾地址崩 | 本栈全是 `/Gz`，自由函数默认 **`__stdcall`**；用反汇编末尾的 `RET n` 确认（§10.4） |
| 想 hook Ares 已接管的函数 | 自己的 hook 根本不执行 | Ares 的 hook `return` 非零，链就断了；只能改 Ares 内部或另找注入点（§10.3） |

---

## 9. 参考

### 9.1 本项目内已标注/使用的 hook 地址

| 地址 | size | 用途 | 现场 |
|---|---|---|---|
| `0x7CD810` | 9 | 游戏 WinMain（一次性初始化） | — |
| `0x52F639` | 5 | 命令行解析 | ESI=`char** argv`，EDI=`argc` |
| `0x4F4583` | 6 | `GScreenClass` 文本绘制（每帧） | `DSurface::Composite` 可用 |
| `0x683E7F` | 7 | `ScenarioClass::Start`（开局） | — |
| `0x7CD8EF` | 9 | 进程退出 | — |

### 9.2 常用 YRpp 设施

| 需求 | 用什么 |
|---|---|
| 访问游戏全局变量 | `DEFINE_REFERENCE(type, name, 地址)`、`DEFINE_POINTER(...)` |
| 调用游戏函数 | `JMP_THIS(地址)`（`__thiscall`）、`JMP_STD(地址)`（`__stdcall`/静态） |
| 类型安全转换（无 RTTI） | `specific_cast<T*>`（比 `WhatAmI()`）、`generic_cast<T*>`（比 `AbstractFlags`） |
| 游戏堆分配 | `GameCreate<T>(...)` / `GameDelete(ptr)` |
| 嵌入成员不改布局 | `DECLARE_PROPERTY(type, name)`、`PROTECTED_PROPERTY(type, name)` |
| 禁止生成虚表 | `class NOVTABLE X : public Y` |

### 9.3 本机关键路径

```
VS2022          D:\Program Files\Microsoft Visual Studio\2022\Community
MSVC 14.44.35207  ...\VC\Tools\MSVC\14.44.35207
MSBuild         ...\2022\Community\MSBuild\Current\Bin\MSBuild.exe
dumpbin         ...\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe
游戏            D:\Games\Ra2       (gamemd.exe 1.11, Ares.dll 3.0)
游戏备份        D:\Games\Ra2\_dsh_backup_orig
Phobos 源码     D:\Codes\RA2Mods\Phobos      ← 最佳代码范本
Ares 0.A 源码   D:\Codes\RA2Mods\Ares
SyringeEx 源码  D:\Codes\RA2Mods\SyringeEx
HAres 源码      D:\Codes\RA2Mods\HAres
```

### 9.4 外部资料

- Phobos 贡献者指南：`D:\Codes\RA2Mods\Phobos\.github\copilot-instructions.md`
  （对 hook 宏、Ext 模式、Serialization 的讲解比本文更细，强烈建议通读）
- Ares 老版贡献指南：`D:\Codes\RA2Mods\HOW-TO-CONTRIBUTE-TO-ARES.md`
- SyringeEx：<https://github.com/Phobos-developers/SyringeEx>
- Phobos：<https://github.com/Phobos-developers/Phobos>
- YRpp：<https://github.com/Phobos-developers/YRpp>
- IDA Pro MCP（让 AI 直接查反汇编，做 hook 开发强烈推荐）：
  <https://github.com/mrexodia/ida-pro-mcp>

---

## 10. 实战案例：让单位提供超级武器（已实现并实测通过）

这是本工程第一个真正的功能，完整走了一遍"读源码 → 反汇编 → 设计 → 实现 → 上机验证"，
可作为以后做其它功能的模板。

### 10.1 需求与语义

让**载具 / 步兵 / 飞机**也能给玩家提供超武，而不只是建筑：

```ini
[MTNK]
SuperWeapon=IronCurtainSpecial
SuperWeapon2=AmericasParaDropSpecial
SuperWeapons=ChronoSphereSpecial,AmericasParaDropSpecial   ; 列表形式
```

语义：玩家**拥有至少一辆**该单位时获得超武，全部损失后失去。超武仍然是玩家级的
（侧边栏按钮 → 点地图发射），单位只是"会移动的提供者"。

### 10.2 原版机制

`HouseClass::UpdateSuperWeaponsOwned`（`0x50AF10`）与 `UpdateSuperWeaponsUnavailable`（`0x50B1D0`）
在每个 house 更新时跑，扫描该 house 的建筑，看 `BuildingTypeClass::SuperWeapon`（类型 +0x16F0）
/ `SuperWeapon2`（+0x16F4）是否等于某个超武下标，然后决定 `Grant` / `Lose` / `SetOnHold`。
原版还会扫描建筑的第 3 个字段（升级槽）。

关键结构（已用反汇编核对）：

| 字段 | 偏移 |
|---|---|
| `SuperClass::Type` | +0x28 |
| `SuperClass::CanHold` | +0x60 |
| `SuperClass::Granted`（Phobos 里叫 `IsPresent`） | +0x6D |
| `SuperClass::OneTime`（`IsOneTime`） | +0x6E |
| `SuperClass::IsCharged`（`IsReady`） | +0x6F |
| `SuperClass::IsOnHold`（`IsSuspended`） | +0x70 |
| `HouseClass::Supers`（数据指针 / 个数） | +0x258 / +0x264 |
| `HouseClass::Defeated` | +0x1F5 |
| `BuildingTypeClass::SuperWeapon` / `SuperWeapon2` | +0x16F0 / +0x16F4 |

> 注意 Phobos 的 YRpp 与 Ares 的 YRpp 给同一批字段起了不同名字
> （`Granted`→`IsPresent`、`IsCharged`→`IsReady`、`IsOnHold`→`IsSuspended`），
> **偏移和顺序完全一致**，用哪套名字都行，但别被名字绕晕。

### 10.3 与 Ares 的冲突（本次最大的坑）

Ares 3.0 把这两个函数**整个替换掉了**，而且它的 hook 都 `return` 一个非零地址；
Syringe 只对 `return 0` 的 hook 做链式调用，所以**我们在同样地址上挂 hook 永远不会被执行**。
`SuperClass::Lose`（`0x6CB7B0`）同理。

Ares 3.0 的实际实现（反汇编得到，比 0.A 源码更简单）：

```cpp
// Ares.dll + 0x38F10，__stdcall
std::vector<SWStatus>* GetSuperWeaponStatuses(HouseClass* pHouse);
// SWStatus = { bool Available; bool PowerSourced; bool Charging; }  每项 3 字节，
// 由 SuperWeaponTypeClass::ArrayIndex 索引
```

两个 hook 都调用它。它只扫 `pHouse->Buildings`。

**解法**：把这两处对 `GetSuperWeaponStatuses` 的调用**改指向我们自己的包装函数**——
先调用原函数，再把"由单位提供"的超武标成可用。之后 Ares 自己的
Grant / Lose / SetOnHold / 侧边栏 cameo / 断电规则全部照旧生效，不存在任何状态争夺。

实测的调用点（`Ares.dll`，PE 时间戳 `0x5fc37ef6`，即 3.0）：

| 位置 | RVA |
|---|---|
| `GetSuperWeaponStatuses` | `0x38F10` |
| 第一个调用点（Owned） | `0x3945B` |
| 第二个调用点（Unavailable） | `0x395A7` |

定位方法（可复用，不必依赖 Ghidra）：Ares 的两个 hook 是**导出函数**，用
`dumpbin /exports` 拿到它们的 RVA，然后在各自函数体内扫描 `E8 rel32` 调用，
**取两个函数调用目标的交集**，再按源码里的调用顺序确定哪个是目标函数。

### 10.4 第二个坑：调用约定

Ares、Phobos、HAres 都用 `/Gz`（默认 `__stdcall`）。所以 Ares 里的**自由函数**
（不是显式写了 `__cdecl` 的 Syringe hook 导出）默认是 **`__stdcall`**。

第一版我把 `GetSuperWeaponStatuses` 声明成了 `__cdecl`，结果每次调用**栈多弹 4 字节**，
跑几十帧后跳到垃圾地址崩溃：

```
Exception (Code: 0xC0000005 at 0x329344F8)
Eax:5F291C90        ← Ares 基址 0x5F1D0000 + 0xC1C90，正是那个静态 vector
Bytes at CS:EIP: A0 A8 7E 00 84 A8 7E 00 ...   ← EIP 落在数据表里
```

**怎么确认调用约定**：看函数末尾的 `RET`：
- `C3` → `RET`，无参清理 → `__cdecl`（调用方清栈）
- `C2 04 00` → `RET 4` → `__stdcall`，清理 1 个参数（被调方清栈）

本例三个出口都是 `C2 04 00`，即 `__stdcall`。**改对之后就再也没崩过。**

### 10.5 实测结果

`HAres.log`（`RunHAres.bat` 启动，Ares 3.0 + Phobos + HAres）：

```
[UnitSW] redirected GetSuperWeaponStatuses call at Ares+0x3945B
[UnitSW] redirected GetSuperWeaponStatuses call at Ares+0x395A7
[UnitSW] Ares 3.0 integration ACTIVE (2 call site(s) patched)
[UnitSW] AMCV provides 1 superweapon(s)
[UnitSW] registry built: 3 provider(s), 1 distinct superweapon(s)
[UnitSW] "IronCurtainSpecial": providedByUnit=1 granted=0 charged=0 rechargeLeft=0
[UnitSW] superweapon "IronCurtainSpecial" (index 1) made available by a unit provider to the human player
[UnitSW] "IronCurtainSpecial": providedByUnit=1 granted=1 charged=0 rechargeLeft=4499   ← Ares 授予并开始充能
[UnitSW] "IronCurtainSpecial": providedByUnit=0 granted=0 charged=0 rechargeLeft=4247   ← 单位没了 → 超武收回
[UnitSW] "IronCurtainSpecial": providedByUnit=1 granted=1 charged=0 rechargeLeft=4499   ← 单位回来 → 重新授予
```

第二轮的状态变化正好对应 MCV 展开成建造厂（`providedByUnit` 1→0）以及之后又拥有提供者。
全程 `syringe.log` 只有那两个正常的 `0xE06D7363`，**没有任何访问违例**。

### 10.6 代码位置

| 文件 | 作用 |
|---|---|
| `src\Misc\UnitSuperWeapon.cpp/.h` | 读 INI 建立"提供者"表；O(1) 查询 house 是否拥有提供者；诊断日志 |
| `src\Misc\AresUnitSuperWeapon.cpp` | 识别 Ares 版本、按 RVA 改写两个调用点、包装函数 |
| `src\Misc\Hooks.Demo.cpp` | `ScenarioClass::Start` 时建立注册表 |

扩展新版本 Ares 时，只要用 §10.3 的方法重新定位那三个 RVA，填进 `Offsets_Ares30` 旁边即可；
版本用 PE `TimeDateStamp` 区分（3.0 = `0x5fc37ef6`，3.0p1 = `0x61daa114`，与 Phobos 的
`AresHelper` 一致）。

### 10.7 已知限制

- **依赖 Ares**：Ares 缺席时功能自动停用并打日志，不会崩。
- **目前只映射了 Ares 3.0**；检测到 3.0p1 会明确记录"偏移未映射"并停用。
- `IsProvidedByHouse` 用的是 `CountOwnedAndPresent`（"Active"计数器，即已部署在场上的单位），
  因此**装在运输载具里（limbo）的单位不算提供者**——这与 Ares 对建筑的
  `IsAlive && !InLimbo` 判定一致。
- 超武仍然从玩家层面发射，不是从单位身上发射；单位不享有独立的充能计时。
