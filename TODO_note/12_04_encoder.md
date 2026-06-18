# qoi_encoder 工具 — 实现计划

## 动机

用户需要一种方式将高层表达式（如 `XLin(2,1.5)+Exp(10)+X2`）转换为 `qoi` int + `qoiParams` base64 字符串，而不需要手动计算 nibble 编码和 base64 编码。

```bash
./qoi_encoder "XLin(2,1.5)+Exp(10)+X2"
→ 输出:
  qoi        = 1025    // = 0x401
  qoiParams  = "AAAAABAAAAAAAAAAF8AAAAAAACAACEA="
```

## 设计

### 独立可执行文件

完全独立于 SZ3，无任何依赖（只需 C++17 标准库）。

```
tools/qoi_encoder/
├── CMakeLists.txt
├── main.cpp
└── README.md
```

### 表达式语法

```
expr      = sum_expr
sum_expr  = term ('+' term)*
term      = func_expr
func_expr = IDENT ['(' double (',' double)* ')']
```

**支持的函数 ID**：

| 函数 | nibble | 参数 |
|------|--------|------|
| XLin | 0x0 | A, B |
| X2 / XSquare | 0x1 | 无 |
| XCubic | 0x2 | 无 |
| XSqrt | 0x3 | 无 |
| XExp | 0x4 | base |
| XLogX | 0x5 | 无 |
| LogX | 0x6 | base |
| XRecip | 0x7 | 无 |
| XAbs | 0x8 | 无 |
| XSin | 0x9 | 无 |
| XTanh | 0xA | 无 |
| XPower | 0xB | expo |
| @ (compose) | 0xE | 特殊操作符，`f@g` = f(g(x)) |

**不支持** `f(Σg)` 和 `MultiQoI`（F 分隔）——这些由用户手动编码。

### 解析 + 编码逻辑

```
输入: "XLin(2,1.5)+Exp(10)+X2"

AST: SumExpr
     ├── Func(XLin, {2.0, 1.5})
     ├── Func(XExp, {10.0})
     └── Func(X2, {})

编码:
  nibbles → 低→高: [1, 0, 4]    // X2, XLin, XExp
  qoi     → 0x401 = 1025
  params  → [2.0, 1.5, 10.0]   // 按 nibble 遍历顺序
  base64  → encode(...)
```

```
输入: "Exp(3)@Square"

AST: Compose
     ├── Func(XExp, {3.0})
     └── Func(X2, {})

编码:
  nibbles → 低→高: [E, 4, 1]    // @, XExp, X2 → Compose(Exp, Square)
  qoi     → 0x14E = 334
  params  → [3.0]
  base64  → encode(...)
```

### 输出格式

```
qoi        = 334
qoiParams  = "AAAAAAAACUA="

// C++ snippet:
// conf.qoi = 334;
// conf.qoiParams = "AAAAAAAACUA=";
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.14)
project(qoi_encoder LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(qoi_encoder main.cpp)
```

### 测试

| Test | 输入 | 预期 qoi | 预期 base64 |
|------|------|----------|-------------|
| 单函数无参数 | `XAbs` | `0x8` | `""` |
| 单函数有参数 | `XLin(3,0.5)` | `0x0` | `base64([3,0.5])` |
| Sum 混合 | `XLin(2,1.5)+Exp(10)+X2` | `0x401` | `base64([2,1.5,10])` |
| Compose | `Exp(3)@Square` | `0x14E` | `base64([3])` |
| 嵌套 Compose | `Sqrt@Exp(2)@Abs` | 待计算 | 对应 base64 |
| 空输入 | (none) | 报错 | — |
| 未知函数 | `Foo(1)` | 报错 | — |
