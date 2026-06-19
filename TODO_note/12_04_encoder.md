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

| 输入类型 | 入口语法 | 输出 qoi | 依赖 | 状态 |
|---------|----------|---------|------|------|
| nibble 函数组合 | `sqr+abs`, `exp(3)@sqr` 等 | 0x0 ~ 0xB/0xE nibble 编码 | C++17 标准库 | ✅ |
| 任意函数 FX | `fx("sin(x)+x^2")` | 高位 nibble = 0x7 | SymEngine + TinyExpr | ⬜ |

**不可混用**。0x7 标记占用最高 nibble，pipeline 检测 `(qoi >> 28) & 0xF == 7` 时跳过 nibble 解析，走 FX 路径。

当前文件结构：

```
tools/qoi_encoder/
├── CMakeLists.txt      # 独立构建（nibble 模式零依赖；FX 需 -lsymengine -lgmp）
├── encode.hpp          # 核心：encode(expr) → {qoi, qoiParams, ok, error}
└── main.cpp            # CLI 包装
```

## nibble 模式 ✅ 已完成

语法和编码规则已实现。核心函数 `qoi_encode::encode()` 从 `encode.hpp` 导出，可被 test harness 复用。

### 函数表（nibble 模式，大小写忽略）

| nibble | 输入名 | 参数 | 默认 |
|--------|--------|------|------|
| 0x0 | `lin` | A, B | 1.0, 0.0 |
| 0x1 | `sqr` | — | — |
| 0x2 | `cubic` | — | — |
| 0x3 | `sqrt` | — | — |
| 0x4 | `exp` | base | ℯ |
| 0x5 | `xlogx` | — | — |
| 0x6 | `log` | base | ℯ |
| 0x7 | — | *reserved for FX marker* | |
| 0x8 | `abs` | — | — |
| 0x9 | `sin` | — | — |
| 0xA | `tanh` | — | — |
| 0xB | `pow` | expo | 2.0 |

### 实现步骤

- [x] 1. Nibble Lexer
- [x] 2. Nibble Parser (recursive descent)
- [x] 3. Nibble emitter (AST → nibble + params)
- [x] 4. SumQoI 0x0 重排
- [x] 5. Compose 末尾 0x0 检查（非恒等 lin 拒绝）
- [x] 6. main.cpp CLI
- [x] 7. CMakeLists.txt
- [x] 8. Tests: encode() roundtrip 集成到 e2e `--compose`

### 已验证的编码示例

| 输入 | qoi | params |
|------|-----|--------|
| `sqr` | `0x1` | `""` |
| `lin(2,0.5)` | `0x0` | `base64([2, 0.5])` |
| `sqr+abs+cubic` | `0x281` | `""` |
| `lin(2,0.5)+exp(10)+sqr` | `0x140` | `base64([2, 0.5, 10])` |
| `sqr+lin(2,0.5)` | `0x10` | `base64([2, 0.5])`（重排） |
| `exp(2.718)@sqr` | `0x14E` | `base64([2.718])` |
| `sqrt@exp(2)@lin(1,0)` | `0x4E3` | `""`（identity elision） |
| `sqrt@exp(2)@lin(2,1)` | — | 错误提示（非恒等 lin 在 Compose 中拒绝） |
| `sqr+abs|exp(3)+sin` | `0x94F81` | `base64([3])` |

## FX 模式 ⬜ 待实现

### FX 编码规则

1. **解析**：提取字符串 `"sin(x)+x^2"`
2. **符号求导**（SymEngine）：
   - `f(x) = sin(x)+x^2`
   - `f'(x) = cos(x)+2*x`
   - `f''(x) = -sin(x)+2`
3. **SymEngine → TinyExpr 格式**：AST walker 显式转换每个节点
4. **序列化**：
   ```
   [4B: len_f] + [len_f: f_str] + [4B: len_df] + [len_df: df_str] + [4B: len_ddf] + [len_ddf: ddf_str]
   → base64 → conf.qoiParams
   ```
5. **qoi**：`qoi = 0x70000000`

### Pipeline FX 实现（引入 TinyExpr）

- 压缩/解压端检测 `(qoi >> 28) & 0xF == 7`
- 解码 base64 → 提取三个字符串
- TinyExpr compile 为内部 bytecode
- `interpret_eb`: 使用 `df(x)` 和 `ddf(x)`（精确导数）
- `eval`: 调用 `f(x)` 闭包

### FX 模式示例

| 输入 | qoi | qoiParams |
|------|-----|-----------|
| `fx("sin(x)")` | `0x70000000` | `base64("sin(x)" + df + ddf)` |
| `fx("sqrt(x)+exp(-x)")` | `0x70000000` | 同上 |
| `fx("x^3+2*x+1")` | `0x70000000` | 同上 |

### 待实现步骤

- [ ] 9. FX 分支：SymEngine parse + diff + AST→TinyExpr walker + 序列化 + base64
- [ ] 10. QoI_FX.hpp：TinyExpr compile + eval/interpret_eb（精确导数）
- [ ] 11. Pipeline 接入：`QoIIf.hpp` 检测 0x7 高位 nibble → `GetQOI` 返回 `QoI_FX`
- [ ] 12. e2e compose 测试覆盖 FX 表达式

### SymEngine 编译环境

```bash
# 已安装到 ~/.local
g++ -std=c++17 \
  -I$HOME/.local/include -I$CONDA_PREFIX/include \
  -L$HOME/.local/lib -L$CONDA_PREFIX/lib \
  your_code.cpp -lsymengine -lgmp
```

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
