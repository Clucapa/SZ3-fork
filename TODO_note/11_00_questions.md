# 复合函数 & 多 QoI 组合 — 设计问题

## 背景

QoZ 有两种组合多个 QoI 的方式：

- **MultiQoIs** (`qoi=999`)：并列关系，持有多个独立 QoI 实例，各自有独立的 tolerance。`interpret_eb` = min(所有子 QoI 的 eb)，`check_comply` 要求全部通过。用于 x² + logx 等预定义组合（qoi=5~8）。

- **XComposite** (`qoi=20`)：函数组合 f(g(x))，嵌套关系。"1 12 2" 表示 e^(x²)。`interpret_eb` 正向算中间值、反向逐层传播 tolerance。

我们改用更紧凑的十六进制 nibble 编码，用 `F` 作为分隔符替代多维组合语义。

## int qoi 编码方案

`int qoi` 从低位到高位，每 4 bit 一格（hex nibble）。0x0~0xD 为函数 ID，0xF 为分隔符。单组内函数相加（SumQoI），多组之间取最严格（MultiQoI，AND 语义）。

| 编码 | 含义 |
|------|------|
| `0x1` | XSquare 单独约束 |
| `0x12` | XSquare + XCubic 求和约束（组内 `eval = x² + x³`）|
| `0x1F3` | XSquare AND XSqrt（两组各自为点态约束） |
| `0x12F3F456` | Sum(1,2) AND 3 AND Sum(4,5,6) |

Regional 单独处理：将 regional 编号做 `~` 翻转存入 `conf.qoi`。最高位为 1 触发 regional 路径，`~` 回翻取编号。
Regional 编号与 pointwise component 编号独立（0=RegionalMean, 1=RegionalMeanSq, 2=RegionalAvgInterp, 3=RegionalMeanSqInterp）。`is_pointwise()` 改为 `return id >= 0;`，regional QoI 的 id 为负值，无需子类逐个重写。

---

## Q1. 如何从 nibble 编码解析出 QoI 组？

解析逻辑：

```
从低位到高位读 nibble：
  遇到 0x0~0xD → 收集到当前组
  遇到 0xF    → 当前组结束，新建下一组
  遇到 0x0    → 但前面没有值：错误（单函数不能是空组）
               → 但前面已有值：单函数单独成组（允许）

解析结束：
  单组（无 F）：每组内 SumQoI → 直接给它做约束
  多组（有 F）：外层 MultiQoI（AND 语义），内层各组各自为 SumQoI
```

例：`0x12F3F456`
- 读 0x6,0x5,0x4,0xF → 组1 = Sum(4,5,6)
- 读 0x3,0xF → 组2 = 3（单函数组）
- 读 0x2,0x1 → 组3 = Sum(1,2)
- 结果：MultiQoI(Sum(4,5,6), 3, Sum(1,2))

## Q2. SumQoI（组内求和）怎么算 interpret_eb？

组 `{id₁, id₂, ..., idₙ}`，约束 `|Σ f_i(orig) - Σ f_i(dec)| ≤ τ`。

每个函数 `f_i` 有 `eval(x)`（正向求值）和 `derivative(x)`（局部导数）。

```
Δf ≈ |Σ f_i'(x)| · eb
```

因此：

```
eb = τ / |Σ f_i'(x)|
```

各函数单独提供 `derivative`，SumQoI 把它们加起来求倒数——逻辑和单个函数的 interpret_eb 公式一致（X2 的 `eb = -|x| + √(x²+τ)` 也是导数反推）。

## Q3. MultiQoI（多组 AND 约束）怎么组合？

`eb = min(eb_组1, eb_组2, ...)`

`check_comply = check1 && check2 && ...`

与 QoZ 的 MultiQoIs 逻辑完全相同，已经在 `11_00_questions.md` Q1-Q3 中分析过。

## Q4. SumQoI 的 check_comply 怎么做？

直接对所有函数分别求值再求和：

```
bool check_comply(T orig, T dec) const {
    double sum_orig = eval(orig);  // Σ f_i(orig)
    double sum_dec  = eval(dec);   // Σ f_i(dec)
    return fabs(sum_orig - sum_dec) <= tol_;
}
```

## Q5. eval 和 derivative 需要加入 QoIIf 基类吗？

**需要 `eval`。** 每个基础函数提供 `eval(T val)`（返回 f(x)），SumQoI 和 check_comply 都要用它。

**`derivative` 可选。** derive 可以通过 `(eval(x+ε) - eval(x-ε)) / 2ε` 数值逼近，也可以让 QoI 提供解析导数。首版用数值导数，后续优化加解析版本。

基类改动：

```cpp
virtual double eval(T val) const { return static_cast<double>(val); }
// derivative 先不加，用 (eval(x+h)-eval(x-h))/(2h) 逼近
```

## Q6. XComposite（f∘g 嵌套复合）还需要吗？

**不需要。** nibble 编码不提供 `f(g(x))` 语义，也没有 `set_qoi_tolerance` 反向传播的需求。如果未来需要支持 f∘g，用单独的 QoI 实现（带 `comp_string` 参数以区别于 nibble 编码），不计入基础函数 ID。

## Q7. SumQoI 是 Pointwise 还是 Regional？

**Pointwise。** check_comply 逐点检查、interpret_eb 逐点计算。blockwise 路径走 `PointwiseEBProvider`（预计算 ebs[]）。

如果需要区域性 sum 约束（块级 `|Σ eval(orig) - Σ eval(dec)| ≤ τ·n`），用 int qoi 最高位置 1 的 regional 路径，QoI 逻辑相同 + budget tracking。

## Q8. SumQoI 内部各函数的 tolerance 如何分配？

组内所有函数共享同一个 tolerance τ（来自 `conf.qEB` 或 `conf.qoiEBs[i]`）。不做各函数的独立 tolerance 分配——这是不同于 QoZ 的地方。QoZ 的 MultiQoIs 中每个子 QoI 有独立 tolerance，而我们的 sum 约束是单一约束作用于组合函数。

多组之间可以通过 `conf.qoiEBs` 数组给每组分配独立 τ：

```
qoi = 0x1F3      → 两组：XSquare 和 XSqrt
qoiEBs = [1.0, 0.5] → XSquare 的 τ=1.0，XSqrt 的 τ=0.5
```

## Q9. 如何创建各组对应的 EBProvider？

方案沿用 Q9 的推荐（`create_eb_provider` 虚函数），但需要处理多组场景：

- **MultiQoI**：持有多个 EBProvider（每组一个），`advance()` 取 min
- **SumQoI**：自己的 `create_eb_provider` 返回 PointwiseEBProvider（预计算 eb 时完成所有函数的 interpret_eb 链）

## Q10. save/load 怎么处理复合 QoI？

**Pointwise 路径**（SumQoI / MultiQoI of pointwise groups）：预计算 ebs[]，`PointwiseEBProvider::save` 序列化整个 ebs[] 数组。

**Regional 路径**：各组的 QoI 参数（τ, geb）序列化，不存 ebs[]。

多组 QoI 的 save/load 不做嵌套递归——按 Flat 序列化各组的 τ 和 geb。

## 决策汇总

| # | 问题 | 推荐 |
|---|---|---|
| Q1 | nibble 解析 | 从低位读，F 分隔组，组内 Sum |
| Q2 | SumQoI interpret_eb | `τ / |Σf_i'(x)|`（数值导数首版） |
| Q3 | 多组组合 | min eb（QoZ MultiQoIs 逻辑） |
| Q4 | SumQoI check_comply | `eval(orig) - eval(dec) ≤ τ` |
| Q5 | eval 接口 | 加到 QoIIf 基类，derivative 推迟 |
| Q6 | XComposite 保留 | 不需要，f∘g 用单独 id 不参与编码 |
| Q7 | SumQoI 点/区 | Pointwise；Regional 用高位控制 |
| Q8 | 各函数 tolerance | 组内共享，组间通过 qoiEBs 分配 |
| Q9 | Provider 创建 | create_eb_provider 虚函数 |
| Q10 | save/load | Flat 序列化各组参数 |
