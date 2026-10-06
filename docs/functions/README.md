# 已实现功能索引

每个功能一份文档，记录**这个功能做了什么、为什么这么做、怎么用、以及它依赖的那些
Ares / 引擎内部地址是怎么来的**。

通用开发方法（构建、hook 机制、调用约定、与 Ares 共存、调试、常见坑）统一写在
[`../DEVELOPMENT.md`](../DEVELOPMENT.md)，本文档里的每篇只讲功能本身，
并在开头链接到对应的通用章节。

| 功能 | 文档 | 状态 | 依赖 |
|---|---|---|---|
| 单位提供超级武器 | [unit-superweapons.md](unit-superweapons.md) | ✅ 实测通过 | Ares 3.0 |
| 范围升/降星超级武器（`Type=PromoteAura`） | [promote-aura-superweapon.md](promote-aura-superweapon.md) | ✅ 实测通过（含发射后指针复位） | Ares 3.0 |
| 升级换单位（`Promote.VeteranType` / `EliteType` + 血量/星级策略） | [promotion-convert.md](promotion-convert.md) | ✅ 实测通过（"三星之后再升级"需求已砍，见该文档 §1.5） | Ares 3.0 |
| 心控屏蔽区间（`MindControlShield.*`） | [mind-control-shield.md](mind-control-shield.md) | ✅ 实测通过（禁止开火 + 解除心控） | 无 |

## 新增一篇功能文档

建议按同样的骨架写，便于以后查阅和交接：

```
# 功能：<名称>

> 通用方法见 ../DEVELOPMENT.md 的 §X
| 项目 | 值 |          ← 状态 / 依赖 / 引入版本 / 源码 / 首次提交

## 1. 功能说明        ← 用户视角：作用、INI 键、行为语义、生效时机
## 2. 原理            ← 原版怎么做、Ares 改写了什么、我们的注入点在哪
## 3. 实现            ← 文件职责、数据结构、执行流程
## 4. 关键地址与偏移   ← 硬编码的 RVA / 偏移，以及重新定位它们的方法
## 5. 实测验证        ← 日志证据，说明"为什么这能证明它是对的"
## 6. 已知限制
## 7. 变更记录
```

第 4 章最重要：**任何写死的 Ares / 引擎地址都必须能被后来的人重新推导出来**，
否则换一个 Ares 版本就没人能修了。
