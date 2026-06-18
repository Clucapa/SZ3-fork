# 函数嵌套 & 基础函数参数 — 设计问题

## Q1. 0xE 组合语义：是否允许 `f(Σ g_i)` 嵌套？

**用户决策**：**不支持**。E 严格 2 操作数，每个操作数是一个 Expression（base 或 E）。`f(Σ g_i)` 这类复杂结构之后用 SymEngine 解决。

> 我觉得不应该支持 f(sum g)，而 sum f(g) 是已经可以支持的。f(sum g) 这种复杂情况我之后用 symengine 之类的方法去解决。

**补充**：`Sum f(g)`（多个 Composition 放在同一个 SumQoI 组里）是天然支持的——E 在 assembly 时解析出 Compose 对象，这些对象可以放进组向量中参与 SumQoI / MultiQoI。

---

## Q2. QoI_Compose 的 interpret_eb 如何计算？

Compose(f, g) = `F(x) = f(g(x))`：

```
dF/dx = f'(g(x)) · g'(x)
eb = τ / |f'(g(x)) · g'(x)|
```

数值导数（和 SumQoI 相同模式）：

```
h = max(1e-8, 1e-6·|x|)
inner_x = g->eval(x)
f' = (f(inner_x+h) - f(inner_x-h)) / 2h
g' = (g(x+h) - g(x-h)) / 2h
eb = τ / |f'·g'|,  capped at geb
```

`deriv == 0` 时返回 `geb`（最保守），和已有 SumQoI 一致。

---

## Q3. 0xE 在 nibble 解析器中的融合方式？

**用户决策**：合并到 `parse_qoi_nibbles` 中——放宽 nibble 范围从 `0x0~0xD` 为包含 `0xE`：

```diff
- } else if (nib >= 0x0 && nib <= 0xD) {
+ } else if ((nib >= 0x0 && nib <= 0xB) || nib == 0xE) {
```

`0xC`/`0xD` 保持抛错。assembly 阶段 `assemble_group` 检测 `func_ids` 中有 `0xE` 则走递归下降，否则走现有 SumQoI 路径。解析和构建在同一个流水线内完成。

> 就按你说的来。

---

## Q4. 参数消费顺序与 E 嵌套的关系？

深度优先左到右遍历（遇到 E → 消费操作数 1 → 消费操作数 2），与扁平 nibble 顺序一致。

---

## Q5. 是否需要新建 QoI_Compose 类？

**是**。类似 QoI_SumQoI，持有 `outer_` 和 `inner_` 两个 `shared_ptr<QoIIf>`。`create_eb_provider` 返回 `PointwiseEBProvider`（pointwise）。基类不需要加新方法。

---

## Q6. Compose 内部的 XLin 恒等 elision？

**用户决策**：内部做。`Compose(f, XLin(1,0))` 直接返回 `f`。但 **最外层不能变**：`GetQOI(conf)` 对 `conf.qoi == 0` 必须返回 `QoI_XLin` 实例，不能因为 elision 而改变接口。

> 在 qoi 算式内部遇到 f(x)=x 时可以进行恒等 elision。但是最外层面向 sz3 时不能改接口，需要是一个 xlin 传出去。

---

## Q7. 跨组（F 分隔）参数偏移？

F 分隔符不影响参数线性顺序——`qoiParams` 的 base64 按 base nibble 遍历顺序线性解码，与分组无关。无偏移问题。

---

## Q8. qoiParams 的序列化策略？

**用户决策**：需要序列化。`qoiParams` 在 Config 中以 `std::string` 存储 base64 编码，与 `qoi` 并排平行保存/加载。

> base64 版本的 param 总之和 qoi 并排平行传入，这样的侵入是必要的。

**详细设计**：
- Config 字段：`std::string qoiParams;`（空串 = 无参数）
- INI：`[QoISettings]` 下 `qoiParams = AAAA...`
- save()：写 uint32 长度 + base64 字符串内容
- load()：读 uint32 长度 + 字符串内容
- 运行时：factory 在 `GetQOI` 中 base64 decode → `vector<double>` → `ParamReader` 消费

---

## Q9. 参数默认值与回退？

**用户决策**：删除 `qEBase`、`qELogB` 字段。`qoiParams` 为空或用完时用硬编码默认值：

| Base 函数 | 默认参数 |
|-----------|---------|
| XLin | A=1, B=0（恒等） |
| XExp | base=ℯ |
| LogX | base=ℯ |
| XPower | expo=2 |

其余 8 个函数无参数。**不给 Config 加额外字段**——QoI 模块完全自洽。

---

## Q10. base64 decoder 放哪里？

在 `QoIIf.hpp` 的 `detail` 命名空间内嵌一个 ~20 行的 base64 decode 函数。不再新增依赖文件（不与 SZ3 公共 utility 耦合）。

Encoder 工具（`tools/qoi_encoder/`）反向 encode 用独立的实现。

---

## 决策汇总

| # | 问题 | 决策 |
|---|------|------|
| Q1 | E 支持 `f(Σg)`？ | ❌ 不，留给 SymEngine |
| Q2 | Compose interpret_eb | 数值导数 `τ/|f'·g'|` |
| Q3 | 解析器融合 | 合并到 `parse_qoi_nibbles`（含 0xE）+ assembly 递归下降 |
| Q4 | 参数消费顺序 | DFS 左到右，与扁平一致 |
| Q5 | QoI_Compose 类 | 新建，pointwise |
| Q6 | XLin elision | 内部做，最外层接口不变 |
| Q7 | 跨组参数偏移 | 无偏移 |
| Q8 | qoiParams 序列化 | Config 存 `std::string` base64，save/load |
| Q9 | 默认值 | 硬编码，删 qEBase/qELogB，不加新 Config 字段 |
| Q10 | base64 decoder | 内联在 `QoIIf.hpp`，不新增文件依赖 |
