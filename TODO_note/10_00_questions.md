# 统一 QoI 接口设计问题

## Q1. Pointwise QoI 的 eb 来源：预计算 ebs[] vs inline interpret_eb

当前 SZ3 点态走预计算 `conf.ebs[]`（SZDispatcher 一次性算完），Regional 则需 inline。

**选项 A：双路径（推荐）**

```
eb = qoi->eb_inline() ? qoi->interpret_eb(*c) : conf.ebs[off++];
```

- 点态：走预计算 ebs[]，`eb_inline()=false`
- Regional：走 inline interpret_eb，`eb_inline()=true`
- ✅ 零性能开销（点态 eb 计算与压缩分离，不影响逐点循环）
- ✅ 解压端点态 eb 从 ebs[] 恢复，不依赖 orig 值
- ✅ 与 QoZ 的 `qoi_id==16` 双路径一致
- ⚠️ 接口多一个 `eb_inline()` 虚函数

**选项 B：统一 inline**

```
始终 eb = qoi->interpret_eb(*c)
```

- 点态 eb 也每次当场算
- ✅ 接口最简洁，无 `eb_inline()`
- ❌ 解压端 `interpret_eb(dec)` ≠ `interpret_eb(orig)`，X2 这种依赖 orig 的 QoI 会产生偏差
- ❌ 每点多一次虚函数调用 + 可能 sqrt 计算

**推荐：A。** 解压端正确性优先。

> 我不明白，为什么会产生偏差？另外，能否对于单点情况提前 
> 计算eb，然后在后续流程中用统一的inline functor接口读取？
> 同时把eb的数据传输放到这个funtor中，使得数据传输接口和
> 标准sz3更接近？同时functor的构造也成为了两个流程都要调
> 用的接口，在单点qoi时负责计算eb，在regional qoi时则负责
> 预计算。这样可以在统一接口的同时避免重复运算。这个方案
> 可行吗？

---

## Q2. Regional 的合规验证方式

区域约束 `|Σ f(dec) - f(orig)| ≤ τ·n` 如何验证？

**选项 A：预算跟踪保证，check_comply 恒 true（推荐）**

```cpp
// RegionalAverage
check_comply(orig, dec) { return true; }
// 误差完全由 update_tolerance 跟踪，每点 eb 反算保证不超预算
```

- ✅ 与 QoZ 一致
- ✅ 无逐点验证开销
- ❌ 理论上预算跟踪可能因量化误差积累而轻微越界

**选项 B：块级验证**

```cpp
postcompress_block() {
    assert(|error| ≤ aggregated_tolerance + 1e-10);
}
```

- ✅ 可捕获数值异常
- ⚠️ 与 QoZ 不同（QoZ 不做任何验证）
- 可附加在 A 之上

**选项 C：逐点检查**

```cpp
check_comply(orig, dec) { return |accumulated_error| ≤ aggregated_tolerance; }
```

- ✅ 严格合规
- ❌ 过保守——一点接近超限会导致后续所有点 unpred

**推荐：A + B（check_comply 恒 true，postcompress_block 做 assert）。**

> 使用与qoz一致的A方案，eb已经向下取整过了，这一步轻微数
> 据越界是可以接受的。

---

## Q3. pointwise QoI 是否也走 update_tolerance 路径？

**选项 A：点态空实现，regional 实现（推荐）**

```
update_tolerance 在 QoIIf 中有默认空实现
```

- ✅ 接口统一，点态不用操心
- ✅ 零开销（空函数体编译期可优化）
- ✅ 与 QoZ 一致（注释掉的 update_tolerance 只在 Region 有意义）

**选项 B：点态也用 update_tolerance 做 check_comply**

```
点态: update_tolerance 内做 |f(orig)-f(dec)| ≤ τ，超限立 flag
```

- ❌ 过度设计，check_comply 已负责此功能

**推荐：A。**

> 对，按我前面说的，使用空实现，保证接口一致

---

## Q4. precompress_block 参数：num_elements vs dims[]

**选项 A：只传 num_elements（推荐）**

```cpp
virtual void precompress_block(size_t num_elements) {}
```

- ✅ 足够计算总预算 `τ × n`
- ✅ 接口最轻，不依赖任何容器类型
- ❌ Regional 的 Interp 变体需要 dims 做块 ID 映射

**选项 B：传 dims 数组**

```cpp
virtual void precompress_block(const std::vector<size_t> &dims) {}
```

- ✅ 可计算块形状、二维/三维边界
- ⚠️ 依赖 `std::vector`（可接受）
- ❌ 与 QoZ 的 `precompress_block(Range)` 模式不同

**选项 C：传 n + offset + 回调**

```
太复杂，否决
```

**推荐：A。** Interp 变体（`RegionalAverageOfSquareInterp`）的 dims 需求放到具体模块构造函数传入。`precompress_block` 只做预算初始化，形状信息在构造时就已知。

> 如果可以在functor中维护块结构的话，可以选A。functor应
> 该支持三个成员函数，一个是计算当前点，第二个是向后移动
> 一格并自动维护块边界，第三个是重置计数器用于特殊用途如
> 文切换。

---

## Q5. N 维度模板参数保留用途

当前 `QoIIf<T, N>` 带 N，但点态实现（XLin/X2）完全不用 N。

**选项 A：保留 N，regional 子类使用（推荐）**

- ✅ Regional 子类可能用 N 校验维度一致性
- ✅ 与现有接口兼容，不改签名
- ✅ 与 QoZ 一致

**选项 B：去掉 N**

- ❌ 需要改所有现有实现和 QpetBlockDecomp
- ❌ 与 QoZ 接口不兼容

**推荐：A。**

> 保留N，以统一接口

---

## Q6. 泰勒引擎规划时间

**选项 A：本次暂不纳入，作为 v2 规划（推荐）**

- ✅ 先完成 pointwise + regional 核心 QoI
- ✅ 模块独立，后续增加不影响现有设计
- ❌ 需额外一次迭代

**选项 B：本次纳入 SymEngine+Taylor 骨架**

- ✅ 一次性完成接口设计
- ❌ 复杂度高，需引入表达式解析器 / 自动求导

**推荐：A。** 单独开 `10_05_taylor.md` 做概念设计。

> 暂不考虑，记作后续拓展任务

---

## 决策汇总

| # | 问题 | 推荐 |
|---|---|---|
| Q1 | eb 来源 | A — 双路径 `eb_inline()` |
| Q2 | Regional 合规 | A+B — 恒 true + 块级 assert |
| Q3 | 点态 update_tolerance | A — 空实现 |
| Q4 | precompress_block 参数 | A — 仅 `num_elements` |
| Q5 | N 模板参数 | A — 保留 |
| Q6 | 泰勒引擎 | A — 放到 v2 |
