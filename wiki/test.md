# SZ3 QPET 测试文档

所有 QoI 测试均通过外部 `qoi_encoder` 二进制统一编码。encoder 二进制路径统一通过 `--encoder-path` 指定。

## 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

依赖：CMake ≥ 3.14、C++17、zstd。

## e2e 测试 CLI

```bash
# 基础门禁（QOI 矩阵 + 剪枝，约 1s）
./test/bin/e2e --basic --encoder-path=./test/bin/qoi_encoder

# 全量（全部测试，约 2min）
./test/bin/e2e --full --encoder-path=./test/bin/qoi_encoder

# 分项测试
./test/bin/e2e --compose --encoder-path=./test/bin/qoi_encoder      # 编码器
./test/bin/e2e --isoline --encoder-path=./test/bin/qoi_encoder      # Isoline
./test/bin/e2e --regional --encoder-path=./test/bin/qoi_encoder     # Regional
./test/bin/e2e --conv --encoder-path=./test/bin/qoi_encoder         # Convolution

# 算法筛选
./test/bin/e2e --block-only --encoder-path=./test/bin/qoi_encoder
./test/bin/e2e --interp-only --encoder-path=./test/bin/qoi_encoder
```

## CI 触发

| 关键字 | 效果 |
|--------|------|
| `[test]` | 编译 + `e2e --basic --encoder-path=./test/bin/qoi_encoder` |
| `[full]` | 编译 + `e2e --full --encoder-path=./test/bin/qoi_encoder` |

---

## 测试覆盖清单

### 一、QOI 矩阵测试（19 QoI × 3 维 × 8 模式 × 3 算法）

所有 pointwise QoI 经 encoder 统一编码。Regional 也通过 encoder `--regional` 编码。

#### Pointwise（15 个）

| QoI | 表达式 | f(x) | qEB | max_data | Domain |
|-----|--------|------|-----|----------|--------|
| XLin | `lin` | x | 1.0 | 0 | 全域 |
| X2 | `sqr` | x² | 1.0 | 100 | 全域 |
| XCubic | `cubic` | x³ | 1.0 | 100 | 全域 |
| XSqrt | `sqrt` | √x | 0.1 | 0 | x≥0 |
| XExp | `exp` | eˣ | 1.0 | 5 | 全域 |
| XLogX | `xlogx` | x·log(x) | 1.0 | 0 | x>0 |
| LogX | `log` | log(x) | 0.1 | 0 | x>0 |
| XRecip | `recip` | 1/x | 1.0 | 0 | x≠0 |
| XAbs | `abs` | \|x\| | 1.0 | 0 | 全域 |
| XSin | `sin` | sin(x) | 0.1 | 0 | 全域 |
| XTanh | `tanh` | tanh(x) | 0.1 | 0 | 全域 |
| XPower | `pow` | x² | 1.0 | 100 | 全域 |
| SumQoI | `sqr+cubic` | x²+x³ | 1.0 | 100 | 全域 |
| MultiQoI | `sqrt\|sqr` | √x & x² | 1.0/1.0 | 100 | 全域 |
| Compose | `exp@sqr` | e^(x²) | 1.0 | 0 | 全域 |

#### Regional（4 个）

| QoI | 表达式 | 聚合约束 | qEB | max_data |
|-----|--------|---------|-----|----------|
| RegMean | `--regional` (hardcoded ~0) | avg(x-orig − x-dec) | 2.0 | 0 |
| RegMeanSq | `--regional` (hardcoded ~1) | avg(x²-orig − x²-dec) | 200.0 | 0 |
| RegSum | `--regional sqr+cubic` | avg(f-orig − f-dec), f=x²+x³ | 2.0 | 100 |
| RegCompose | `--regional sqr@exp` | avg((eˣ)²-orig − (eˣ)²-dec) | 1.0 | 5 |

#### 算法 × 维数矩阵

| 算法 | 1D | 2D | 3D | 插值类型 |
|------|:--:|:--:|:--:|----------|
| Block (Lorenzo_reg) | ✓ | ✓ | ✓ | — |
| Interp | ✓ | ✓ | — | Cubic, Linear |
| InterpLorenzo | ✓ | ✓ | — | Cubic, Linear |

维数大小从 500–524 动态轮换，3D 固定为 18。

---

### 二、编码器测试（24 表达式 × 2 模式 × 2 算法）

通过外部 encoder 编码为 `(qoi, qoiParams)`，feed 给压缩器后验证 QoI 合规。

#### Nibble 单函数

| 标签 | 表达式 | f(x) | Domain | 备注 |
|------|--------|------|--------|------|
| LinCustom | `lin(2,0.5)` | 2x+0.5 | 全域 | |
| ExpCustom | `exp(3)` | 3ˣ | 全域 | base=3, max_data=5 |
| LogCustom | `log(2)` | log₂x | x>0 | base=2 |
| PowCustom | `pow(3.5)` | x^3.5 | 全域 | max_data=50 |

#### SumQoI

| 标签 | 表达式 | f(x) |
|------|--------|------|
| Sum3Param | `lin(2,0.5)+exp(3)+sqr` | 2x+0.5+3ˣ+x² |
| Sum3NoParam | `abs+sin+tanh` | \|x\|+sin(x)+tanh(x) |
| SumLinReorder | `sqr+lin(2,0.5)` | x²+2x+0.5 |

#### MultiQoI

| 标签 | 表达式 | Group 1 | Group 2 |
|------|--------|---------|---------|
| Multi2 | `sqr\|abs` | sqr | abs |
| MultiSum | `sqr+cubic\|exp(3)+sin` | sqr+cubic | 3ˣ+sin |

#### Compose

| 标签 | 表达式 | f(x) | 备注 |
|------|--------|------|------|
| CompAbsSin | `abs@sin` | \|sin(x)\| | |
| CompNested | `sqrt@exp(2)@sqr` | √(2^(x²)) | 三重嵌套, max_data=3 |
| CompExpCubic | `exp@cubic` | e^(x³) | max_data=30 |

#### FX 模式（SymEngine 任意函数）

| 标签 | 表达式 | f(x) |
|------|--------|------|
| FX_SinX2 | `sin(x)+x^2` | sin(x)+x² |
| FX_SqrtExp | `sqrt(x)+exp(-x)` | √x+e^(-x) |
| FX_X3_2X_1 | `x^3+2*x+1` | x³+2x+1 |

#### Isoline

| 标签 | 表达式 | 子 QoI |
|------|--------|--------|
| Iso6Sqr | `iso6(sqr, -5, 5, 3, 0.01)` | sqr |
| Iso6Sqrt | `iso6(sqrt, 0, 10, 3, 0.001)` | sqrt |
| Iso6Abs | `iso6(abs, -3, 3, 3, 0.001)` | abs |
| Iso6Cubic | `iso6(cubic, -5, 5, 3, 0.01)` | cubic |
| Iso6Sin | `iso6(sin, -1, 1, 3, 0.001)` | sin |
| Iso6Exp | `iso6(exp, 0, 5, 3, 0.001)` | exp |
| Iso6Sum | `iso6(sqr+cubic, -5, 5, 3, 0.01)` | sqr+cubic |
| Iso6Multi | `iso6(sqr,...;abs,...)` | sqr \| abs |

---

### 三、Isoline 测试（2 用例 × 2 维 × 8 模式 × 3 算法）

经 encoder 生成 iso6 表达式，逐点硬编码验证等值线不穿越。

| 标签 | 表达式 | 子 QoI |
|------|--------|--------|
| Iso6-XLin | `iso6(lin, -5, 5, 3, 0.01)` | lin (恒等) |
| Iso6-X2 | `iso6(sqr, -5, 5, 3, 0.01)` | sqr |

---

### 四、Regional 编码测试（11 表达式 × 2 算法）

通过 encoder `--regional` 编码，硬编码 feval 验证聚合约束。Block 和 Interp 算法均测试。

| 标签 | 表达式 | 类型 | qEB |
|------|--------|------|-----|
| RegLin | `--regional sqr` | nibble | 1.0 |
| RegCubic | `--regional cubic` | nibble | 1.0 |
| RegAbs | `--regional abs` | nibble | 1.0 |
| RegSqrt | `--regional sqrt` | nibble | 0.1 |
| RegSum | `--regional sqr+cubic` | SumQoI | 1.0 |
| RegCompAbsSin | `--regional abs@sin` | Compose | 0.1 |
| RegMultiSqrAbs | `--regional sqr\|abs` | MultiQoI | 1.0 |
| RegSumParam | `--regional lin(2,0.5)+sqr` | Sum+Params | 3.0 |
| RegFX_SinX2 | `--regional sin(x)+x^2` | FX | 1.0 |
| RegFX_SqrtExp | `--regional sqrt(x)+exp(-x)` | FX | 1.0 |

---

### 五、Convolution 测试（6 核 × 2 算法）

通过 encoder `conv(...)` 编码，验证滑动窗口卷积约束。

| 标签 | 核 | conv_tol |
|------|-----|----------|
| ConvSum3 | `[1,1,1]` | 0.1 |
| ConvAvg3 | `[0.25, 0.5, 0.25]` | 0.1 |
| ConvLap3 | `[1,-2,1]` | 0.5 |
| ConvSum4 | `[1,1,1,1]` | 0.15 |
| ConvAvg5 | `[0.2,0.2,0.2,0.2,0.2]` | 0.1 |
| ConvEdge5 | `[-1,2,0,-2,1]` | 0.5 |

---

### 六、数据模式

| 枚举 | 模式名 | 1D 值域 | 特点 |
|------|--------|---------|------|
| D1_RAMP | Ramp | 0→100 线性 | 最优 predictor |
| D2_WIDE | WideRange | 0.001→10000 对数 | 大动态范围 |
| D3_SINUSOID | Sinusoid | ~12–88 双音+噪声 | 模拟真实数据 |
| D4_CLIFF | Cliff | 0.1→1000 断崖 | 预测器失效点 |
| D5_ZEROCROSS | ZeroCross | -10→10 过零 | 负值域约束 |
| D6_EXP | Exponential | exp 分布 | 指数分布 |
| D7_CONST | Constant | 全 1.0 | 退化场景 |
| D8_RANDWALK | RandomWalk | 随机游走 | 无规律波动 |
