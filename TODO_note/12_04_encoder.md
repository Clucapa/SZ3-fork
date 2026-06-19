# qoi_encoder 工具 — 实现计划

## 动机

将高层表达式转换为 `qoi` int + `qoiParams` base64，避免手动计算 nibble 和 base64。

```bash
./qoi_encoder "lin(2,1.5)+exp(10)+sqr"
→ 输出:
  qoi        = 0x140
  qoiParams  = "AAAAABAAAAAAAAAAF8AAAAAAACAACEA="
```

## 两套编译路径

encorder 支持两种互斥的输入：

| 输入类型 | 入口语法 | 输出 qoi | 依赖 |
|---------|----------|---------|------|
| nibble 函数组合 | `sqr+abs`, `exp(3)@sqr` 等 | 0x0 ~ 0xB/0xE nibble 编码 | C++17 标准库 |
| 任意函数 FX | `fx("sin(x)+x^2")` | 高位 nibble = 0x7 | SymEngine |

**不可混用**。0x7 标记占用最高 nibble，pipeline 检测 `(qoi >> 28) & 0xF == 7` 时跳过 nibble 解析，走 FX 路径。

文件：

```
tools/qoi_encoder/
├── CMakeLists.txt
└── main.cpp
```

nibble 模式零依赖（C++17 标准库）。FX 模式额外依赖 SymEngine。

## 语法

### nibble 模式

```
expr     = group ('|' group)*
group    = compose_term ('+' compose_term)*
compose_term = factor_term ('@' factor_term)*
factor_term = IDENT [ '(' number (',' number)* ')' ]
```

### FX 模式

单一入口：
```
fx_expr = 'fx' '(' STRING ')'
```

### 冲突

Nibble 模式下函数名列表不含 `fx`，若输入以 `fx(...)` 开头，走 FX 编译路径。两种模式不可混合（例如 `sqr+fx("sin(x)")` 拒绝）。

## 函数表（nibble 模式，大小写忽略）

| nibble | 输入名 | 参数 | 默认 |
|--------|--------|------|------|
| 0x0 | `lin` | A, B | 1.0, 0.0 |
| 0x1 | `sqr` | — | — |
| 0x2 | `cubic` | — | — |
| 0x3 | `sqrt` | — | — |
| 0x4 | `exp` | base | ℯ |
| 0x5 | `xlogx` | — | — |
| 0x6 | `log` | base | ℯ |
| 0x7 | *不占用（reserved for FX marker）* |
| 0x8 | `abs` | — | — |
| 0x9 | `sin` | — | — |
| 0xA | `tanh` | — | — |
| 0xB | `pow` | expo | 2.0 |

注意 nibble `0x7` 是 `recip`，跟 FX 的 0x7 **高位 nibble 标记**无关。

## nibble 编码规则

1. **解析**：按语法 parse 成 AST
2. **Nibble 序列**：按输入顺序（左→右 = LSB→MSB）收集 nibble
   - SumQoI: nibbles = [fn1, fn2, ...]（左→右）
   - Compose: `[0xE, outer_nib, inner_nib]`（递归前缀式）
   - MultiQoI: `[...group1..., 0xF, ...group2...]`
3. **qoi**：`nibbles[0] + nibbles[1]*16 + nibbles[2]*256 + ...`，输出 `0x` 前缀
4. **Params**：按 nibble 顺序遍历全部函数，收集 double 参数 → base64 编码
5. **SumQoI 末尾 0x0 重排**：如果 nibble 序列末尾是 0x0（lin），自动把 lin 提前到首位（加法交换律保证语义正确）
6. **Compose 末尾 0x0 检查**：如果 Compose 最内层是 lin 且非 identity，拒绝并提示

## FX 编码规则

1. **解析**：提取字符串 `"sin(x)+x^2"`
2. **符号求导**（SymEngine）：
   - `f(x) = sin(x)+x^2`
   - `f'(x) = cos(x)+2*x`
   - `f''(x) = -sin(x)+2`
3. **SymEngine → TinyExpr 格式**：用 AST walker 显式转换每个节点，确保输出被 TinyExpr 正确识别
4. **序列化**：
   ```
   [4B: len_f] + [len_f: f_str] + [4B: len_df] + [len_df: df_str] + [4B: len_ddf] + [len_ddf: ddf_str]
   → base64 → conf.qoiParams
   ```
5. **qoi**：`qoi = 0x70000000`

## Pipeline FX 实现（引入 TinyExpr）

在压缩/解压端检测 `(qoi >> 28) & 0xF == 7`：
- 解码 base64 → 提取三个字符串
- TinyExpr compile 为内部 bytecode
- `interpret_eb`: 代入 EB 公式，使用 `df(x)` 和 `ddf(x)`（精确，非数值微分）
- `eval`: 调用 `f(x)` 闭包

TinyExpr 放入 `include/SZ3/utils/tinyexpr/`。

新增 `include/SZ3/qoi/QoI_FX.hpp`，实现 `QoI_FX` 类，依赖 TinyExpr。

## nibble 模式示例

| 输入 | qoi | params |
|------|-----|--------|
| `sqr` | `0x1` | `""` |
| `lin(2,0.5)` | `0x0` | `base64([2, 0.5])` |
| `sqr+abs+cubic` | `0x128` | `""` |
| `lin(2,0.5)+exp(10)+sqr` | `0x140` | `base64([2, 0.5, 10])` |
| `sqr+lin(2,0.5)` | `0x10` | `base64([2, 0.5])`（重排） |
| `exp(2.718)@sqr` | `0x14E` | `base64([2.718])` |
| `sqrt@exp(2)@lin(1,0)` | `0x4E3` | `""`（identity elision） |
| `sqrt@exp(2)@lin(2,1)` | — | 错误提示 |
| `sqr+abs\|exp(3)+sin` | `0xF138?` | `base64([3])` |

## FX 模式示例

| 输入 | qoi | qoiParams |
|------|-----|-----------|
| `fx("sin(x)")` | `0x70000000` | `base64("sin(x)" + df + ddf 三串)` |
| `fx("sqrt(x)+exp(-x)")` | `0x70000000` | 同上 |
| `fx("x^3+2*x+1")` | `0x70000000` | 同上 |
| `fx("sqr+abs")` | — | 错误：`fx` 内是数学表达式，不是函数名 |

## 实现步骤

- [ ] 1. Nibble Lexer: tokenize IDENT, `(`, `)`, `,`, number, `+`, `@`, `|`
- [ ] 2. Nibble Parser: recursive descent → nibble AST
- [ ] 3. Nibble emitter: AST → nibble sequence + params vector
- [ ] 4. SumQoI 0x0 重排
- [ ] 5. Compose 末尾 0x0 检查
- [ ] 6. main.cpp: CLI 入口，检测 `fx(` 前缀分流
- [ ] 7. CMakeLists.txt: nibble 模式零依赖
- [ ] 8. Tests: 覆盖全部 nibble 示例 + 错误情况
- [ ] 9. FX 分支：SymEngine parse + diff + AST→TinyExpr walker + 序列化 + base64
- [ ] 10. QoI_FX.hpp：TinyExpr compile + eval/interpret_eb
- [ ] 11. Pipeline 接入：检测 0x7 高位 nibble → 走 QoI_FX

## 输出格式

nibble 模式：
```
qoi        = 0x14E
qoiParams  = "AAAAAAAACUA="
```

FX 模式：
```
qoi        = 0x70000000
qoiParams  = "base64(f_str + df_str + ddf_str)"
```
