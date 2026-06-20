# SZ3 QPET 测试文档

**所有 pointwise QoI 和 Isoline 测试均通过外部 `qoi_encoder` 二进制统一编码**（nibble 优先，失败回落 SymEngine FX）。仅 Regional QoI 使用 hardcoded id。encoder 二进制路径统一通过 `--encoder-path` 指定。

## 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

依赖：CMake ≥ 3.14、C++17、zstd。GTest 自动拉取 v1.16.0。

## e2e 测试 CLI

所有 pointwise QoI 测试均通过外部 `qoi_encoder` 二进制编码表达式（nibble 优先，失败后自动回落 FX）。Regional QoI 使用 hardcoded id。

```bash
# 基础门禁（QOI 矩阵 + 剪枝，约 1s）
./test/bin/e2e --basic --encoder-path=./test/bin/qoi_encoder --encoder-path=./test/bin/qoi_encoder

# 全量测试（QOI 矩阵 + 编码器往返 + Isoline，约 2min）
./test/bin/e2e --full --encoder-path=./test/bin/qoi_encoder

# 仅 Isoline
./test/bin/e2e --isoline --encoder-path=./test/bin/qoi_encoder --encoder-path=./test/bin/qoi_encoder

# 仅编码器往返（自定义参数 + FX + Iso6）
./test/bin/e2e --compose --encoder-path=./test/bin/qoi_encoder

# 仅 Block / Interp 算法
./test/bin/e2e --block-only --encoder-path=./test/bin/qoi_encoder
./test/bin/e2e --interp-only --encoder-path=./test/bin/qoi_encoder
```

## CI 触发

| 关键字 | 效果 |
|--------|------|
| (无标签) | 仅检查非 ASCII 字符 |
| `[build]` | Linux 编译 |
| `[test]` | 编译 + `e2e --full --encoder-path=./test/bin/qoi_encoder`（1860 条，约 2min） |

## 测试覆盖清单

### 一、QOI 矩阵测试（`e2e --full`，15 pointwise × 8 模式 × 3 维 × 5 算法 + 4 regional = 1860 条）

所有 pointwise QoI 经 encoder 统一编码。Regional 使用 hardcoded id。

| QoI | 表达式 | f(x) | qEB | absEB | Domain | max_data |
|-----|--------|------|-----|-------|--------|----------|
| XLin | `lin` | x | 1.0 | 10.0 | 全域 | 0 |
| X2 | `sqr` | x² | 1.0 | 10.0 | 全域 | 100 |
| XCubic | `cubic` | x³ | 1.0 | 10.0 | 全域 | 100 |
| XSqrt | `sqrt` | √x | 0.1 | 5.0 | x≥0 | 0 |
| XExp | `exp` | eˣ | 1.0 | 10.0 | 全域 | 5 |
| XLogX | `xlogx` | x·log(x) | 1.0 | 10.0 | x>0 | 0 |
| LogX | `log` | log(x) | 0.1 | 5.0 | x>0 | 0 |
| XRecip | `recip` | 1/x | 1.0 | 10.0 | x≠0 | 0 |
| XAbs | `abs` | \|x\| | 1.0 | 10.0 | 全域 | 0 |
| XSin | `sin` | sin(x) | 0.1 | 5.0 | 全域 | 0 |
| XTanh | `tanh` | tanh(x) | 0.1 | 5.0 | 全域 | 0 |
| XPower | `pow` | x² | 1.0 | 10.0 | 全域 | 100 |
| SumQoI | `sqr+cubic` | x²+x³ | 1.0 | 10.0 | 全域 | 100 |
| MultiQoI | `sqrt\|sqr` | √x & x² | 1.0/1.0 | 10.0 | 全域 | 100 |
| Compose | `exp@sqr` | e^(x²) | 1.0 | 10.0 | 全域 | 0 |

#### Regional（4 个，不使用 encoder）

| QoI | 约束 | qEB | absEB | 路径 |
|-----|------|-----|-------|------|
| RegMean | \|mean(orig) - mean(dec)\| ≤ qEB | 2.0 | 5.0 | Block |
| RegMeanSq | \|mean(orig²) - mean(dec²)\| ≤ qEB | 200.0 | 10.0 | Block |
| RegAvgInt | 同 RegMean | 2.0 | 5.0 | Interp |
| RegMeanSqI | 同 RegMeanSq | 200.0 | 10.0 | Interp |

> ILorenzo + ZeroCross 组合对 RegAvgInt 跳过（2 条）。原因：ILorenzo 的 CSD（压缩顺序依赖）在符号交替的零穿越数据上产生同向偏置，而 Interp 路径的 Regional QoI 按 QoZ 设计不做 budget tracking（`update_tolerance` 为空），全局均值无法被纠正。Block 路径的 RegionalMean 有 budget tracking，不受影响。

#### 算法 × 维数矩阵

| 算法 | 1D | 2D | 3D | 插值类型 |
|------|:--:|:--:|:--:|----------|
| Block (Lorenzo_reg) | ✓ | ✓ | ✓ | — |
| Interp | ✓ | ✓ | — | Cubic, Linear |
| InterpLorenzo | ✓ | ✓ | — | Cubic, Linear |

维数大小从 500–524 动态轮换，3D 固定为 18。

---

### 二、编码器往返测试（`e2e --compose`，37 表达式 × 2 模式 × 2 算法 = 148 条）

通过外部 `qoi_encoder` 编码为 `(qoi, qoiParams)`，feed 给压缩器后验证 QoI 合规。数据模式为 Ramp (0→100) 和 Sinusoid (~12–88)，算法为 Block + Interp-Cubic。

#### Nibble 单函数（带默认/自定义参数）

| 标签 | 表达式 | f(x) | qEB | Domain | 备注 |
|------|--------|------|-----|--------|------|
| LinDefault | `lin` | x | 1.0 | 全域 | 默认 A=1,B=0 |
| Sqr | `sqr` | x² | 1.0 | 全域 | |
| Cubic | `cubic` | x³ | 1.0 | 全域 | |
| Sqrt | `sqrt` | √x | 0.1 | x≥0 | |
| ExpDefault | `exp` | eˣ | 1.0 | 全域 | max_data=3 |
| XLogX | `xlogx` | x·log(x) | 1.0 | x>0 | |
| LogDefault | `log` | log(x) | 0.1 | x>0 | |
| Recip | `recip` | 1/x | 1.0 | x≠0 | |
| Abs | `abs` | \|x\| | 1.0 | 全域 | |
| Sin | `sin` | sin(x) | 0.1 | 全域 | |
| Tanh | `tanh` | tanh(x) | 0.1 | 全域 | |
| PowDefault | `pow` | x² | 1.0 | 全域 | |
| LinCustom | `lin(2,0.5)` | 2x+0.5 | 3.0 | 全域 | |
| ExpCustom | `exp(3)` | 3ˣ | 1.0 | 全域 | base=3, max_data=5 |
| LogCustom | `log(2)` | log₂x | 0.1 | x>0 | base=2 |
| PowCustom | `pow(3.5)` | x^3.5 | 1.0 | 全域 | max_data=50 |

#### SumQoI（组内求和）

| 标签 | 表达式 | f(x) | qEB |
|------|--------|------|-----|
| Sum2 | `sqr+cubic` | x²+x³ | 1.0 |
| Sum3Param | `lin(2,0.5)+exp(3)+sqr` | 2x+0.5+3ˣ+x² | 1.0 |
| Sum3NoParam | `abs+sin+tanh` | \|x\|+sin(x)+tanh(x) | 1.0 |
| SumLinReorder | `sqr+lin(2,0.5)` | x²+2x+0.5 | 3.0 |

#### MultiQoI（组间 AND）

| 标签 | 表达式 | Group 1 | Group 2 | qEB |
|------|--------|---------|---------|-----|
| Multi2 | `sqr\|abs` | sqr | abs | 1.0 / 1.0 |
| MultiSum | `sqr+cubic\|exp(3)+sin` | sqr+cubic | 3ˣ+sin | 1.0 / 1.0 |

#### Compose

| 标签 | 表达式 | f(x) | 备注 |
|------|--------|------|------|
| CompExpSqr | `exp@sqr` | e^(x²) | max_data=30 |
| CompAbsSin | `abs@sin` | \|sin(x)\| | |
| CompNested | `sqrt@exp(2)@sqr` | √(2^(x²)) | 三重嵌套, max_data=3 |
| CompExpCubic | `exp@cubic` | e^(x³) | max_data=30 |

#### FX 模式（SymEngine 任意函数）

| 标签 | 表达式 | f(x) | Domain |
|------|--------|------|--------|
| FX_SinX2 | `fx("sin(x)+x^2")` | sin(x)+x² | 全域 |
| FX_SqrtExp | `fx("sqrt(x)+exp(-x)")` | √x+e^(-x) | x≥0 |
| FX_X3_2X_1 | `fx("x^3+2*x+1")` | x³+2x+1 | 全域 |

---

### 三、Isoline 测试（`e2e --isoline`，2 用例 × 2 维 × 8 模式 × 3 算法 = 96 条）

Hardcoded qoi/qoiParams，独立验证等值线合规性（禁止数据穿越 isovalue 线）：

| 标签 | qoi | Isoline 配置 | 子 QoI | qEB | absEB |
|------|-----|-------------|--------|-----|-------|
| Iso6-XLin | 0x60000000 | [-5,5] cnt=3 meb=0.01 | XLin (恒等) | 5.0 | 10.0 |
| Iso6-X2 | 0x60000001 | [-5,5] cnt=3 meb=0.01 | X2 (x²) | 5.0 | 10.0 |

算法：Block / Interp-Cubic / Interp-Linear。维数：1D、2D。

---

### 四、Isoline 编码器往返测试（`e2e --compose`，内置，9 表达式 × 2 模式 × 2 算法 = 36 条）

通过 encoder 生成 iso6 表达式，验证子 QoI 点态合规：

| 标签 | 表达式 | 子 QoI | qEB | Domain |
|------|--------|--------|-----|--------|
| Iso6Sqr | `iso6(sqr, -5, 5, 3, 0.01)` | sqr | 1.0 | 全域 |
| Iso6Sqrt | `iso6(sqrt, 0, 10, 3, 0.001)` | sqrt | 0.1 | x≥0 |
| Iso6Abs | `iso6(abs, -3, 3, 3, 0.001)` | abs | 1.0 | 全域 |
| Iso6Cubic | `iso6(cubic, -5, 5, 3, 0.01)` | cubic | 1.0 | 全域 |
| Iso6Sin | `iso6(sin, -1, 1, 3, 0.001)` | sin | 0.1 | 全域 |
| Iso6Exp | `iso6(exp, 0, 5, 3, 0.001)` | exp | 1.0 | 全域 |
| Iso6Sum | `iso6(sqr+cubic, -5, 5, 3, 0.01)` | sqr+cubic | 1.0 | 全域 |
| Iso6Multi | `iso6(sqr,-5,5,3,0.01;abs,-3,3,3,0.001)` | sqr & abs | 1.0 / 1.0 | 全域 |

---

### 五、数据模式

| 枚举 | 模式名 | 1D 值域 | 特点 |
|------|--------|---------|------|
| D1_RAMP | Ramp | 0→100 线性 | 最优 predictor，暴露 QoI 导数变化 |
| D2_WIDE | WideRange | 0.001→10000 对数 | 大动态范围，stress per-point eb |
| D3_SINUSOID | Sinusoid | ~12–88 双音+噪声 | 模拟真实数据 |
| D4_CLIFF | Cliff | 0.1→1000 断崖 | 预测器失效点 |
| D5_ZEROCROSS | ZeroCross | -10→10 过零 | 负值域约束 |
| D6_EXP | Exponential | exp 分布 | 指数分布 |
| D7_CONST | Constant | 全 1.0 | 退化场景 |
| D8_RANDWALK | RandomWalk | 随机游走 | 无规律波动 |

### 六、Unit 测试（GTest）

| 二进制 | 类型 | 内容 |
|--------|------|------|
| `test_qpet_interp_pointwise` | 端到端 | Interp + pointwise QoI 全链路 |
| `test_qpet_qoi` | 单元 | XLin/X2 的 interpret_eb、check_comply |
| `test_qpet_eb_provider` | 单元 | PointwiseEBProvider advance/save/load |
| `test_qpet_regional` | 单元 | Regional budget tracking、geb capping |
| `test_qpet_interp` | 单元+端到端 | QpetInterpDecomp 1D/2D round-trip |
| `test_qpet_composite` | 单元+端到端 | 12 基函数 + Sum/Multi/Compose + nibble 解析器 |
