# 端到端 QoI 测试套件 — 实现计划

## 动机

现有测试在 `tools/test/modules/` 下的覆盖盲区：
- 无 `ALGO_INTERP` + pointwise qoi 的 dispatcher 级测试（导致 #SZDispatcher 分支 bug 长期未被发现）
- 仅测试 1D 小尺寸数据（64~128 点），无 2D/3D 端到端
- 数据范围窄（~10~60），不考验 per-point eb 的极端差异
- 无 MultiQoI/SumQoI/Compose 的 dispatcher 级端到端
- 无 `ALGO_INTERP_LORENZO` 混合算法的端到端测试
- Regional QoI 端到端测试只检查均值，不检查 QoI 语义

目标：在 `<repo>/test/` 下建一个独立可执行文件，用合成数据对全部 QoI × 数据模式 × 维度 × 算法做参数化端到端验证。

---

## 两种算法及其 QoI 安全网

| | ALGO_LORENZO_REG (Block) | ALGO_INTERP (Interp) |
|---|---|---|
| 分解类 | `QpetBlockDecomp` | `QpetInterpDecomp` |
| 结构 | 分块 (1D:128, 2D:16, 3D:6) | 全量 anchor 网格 + 多层插值 |
| 预测器 | Lorenzo 邻域 / Regression | 线性 cubic 插值 |
| QoI 兜底 | **有** — `check_comply` 失败 → 回写原始值无损 | **无** — 只靠 per-point eb |
| eb 计算 | `eb_provider->advance()` 直接 | `min(advance(), cur_eb_geo)` |

Block 路径有安全网兜底，Interp 路径没有。因此 Interp 路径必须严格验证 per-point eb 是否对所有点都正确约束了 QoI。

---

## 测试维度

```
[数据模式] × [维度 N] × [算法] × [QoI 列表]
```

### 1. 数据模式（8 种）

| ID | 名称 | 1D 生成 | 目的 |
|----|------|---------|------|
| D1 | 线性斜坡 | `linspace(0, 100, N)` | 基准：最优预测，测 overhead |
| D2 | 宽动态范围 | `10^linspace(-3, 4, N)` 0.001~10000 | per-point eb 极端差异，X2 下 x=10000 时 eb≈5e-5 |
| D3 | 正弦叠加 | `sin(2πf₁t)+0.3·sin(2πf₂t)` + N(0,σ) 噪声 | 典型物理场，中等预测难度 |
| D4 | 阶梯/断崖 | 前半=0.1，后半=1000，4 点过渡 | 预测器失效 + eb budget fallback |
| D5 | 过零点 | `linspace(-100, 100, N)` | XRecip, XAbs, XPower 在 0 附近导数发散 |
| D6 | 指数增长 | `1.02^i` | 对路 XExp；导数=自身，eb 与 val 同步缩放 |
| D7 | 常量 | 全部 = 42 | 边际：最佳预测 + QoI 通式验证 |
| D8 | 随机游走 | `y₀=0, yᵢ=yᵢ₋₁+N(0,σ²)` | 最差预测，最大化依赖 per-point eb |

2D/3D 推广：
- D1~D3：meshgrid 推广
- D4：象限分块（如 2D 中左上=0.1, 右下=1000）
- D5~D7：坐标混合

### 2. 数据尺寸

| N | 尺寸 | 点数 | Lorenzo block 数 |
|---|------|------|-----------------|
| 1 | 512 | 512 | 4 |
| 2 | 32×32 | 1024 | 2×2=4 |
| 3 | 18×18×18 | 5832 | 3×3×3=27 |

总量 < 100KB/维度，所有模式 × 3 维 ≈ 70KB，可放进 repo。

### 3. 算法

| Algo | 覆盖理由 |
|------|---------|
| `ALGO_INTERP` | **重点**：无 QoI 安全网，per-point eb 是唯一防线 |
| `ALGO_LORENZO_REG` | 有安全网，测 QoI 无损回退率与压缩率 |
| `ALGO_INTERP_LORENZO` | 可选：混合算法，低优先级 |

### 4. QoI 与数据模式映射

| QoI | id | 关键模式 | 1D | 2D | 3D |
|-----|-----|---------|----|----|-----|
| XLin | 0x0 | D1~D8 | ✓ | ✓ | ✓ |
| X2 | 0x1 | D2(宽范围), D5(过零) | ✓ | ✓ | ✓ |
| XCubic | 0x2 | D2, D5 | ✓ | — | — |
| XSqrt | 0x3 | D2, D6(>0) | ✓ | — | — |
| XExp | 0x4 | D6(对路), D2 | ✓ | — | — |
| XLogX | 0x5 | D2, D6(>0) | ✓ | — | — |
| LogX | 0x6 | D2, D5(平移>0) | ✓ | — | — |
| XRecip | 0x7 | D5(过零≠0), D2 | ✓ | ✓ | — |
| XAbs | 0x8 | D5(过零), D4(断崖) | ✓ | ✓ | — |
| XSin | 0x9 | D3(对路) | ✓ | — | — |
| XTanh | 0xA | D4, D2 | ✓ | — | — |
| XPower | 0xB | D2, D5 | ✓ | — | — |
| SumQoI | 0x12/0x401 | D2, D4 | ✓ | ✓ | ✓ |
| MultiQoI | 0x1F3 | D2, D5 | ✓ | ✓ | — |
| Compose | 0x14E/0x19E | D2, D6 | ✓ | ✓ | — |
| RegionalMean | ~0 | D1, D3, D4 | ✓ | ✓ | ✓ |
| RegionalMeanSq | ~1 | D2, D4 | ✓ | ✓ | — |
| RegionalAvgInterp | ~2 | D1, D3 | ✓ | — | — |
| RegionalMeanSqInterp | ~3 | D2 | ✓ | — | — |

## CLI 模式

```bash
./test/e2e             # 默认 --full：全量测试
./test/e2e --fast      # 裁剪测试，快速 CI 门禁
./test/e2e --interp-only   # 仅 ALGO_INTERP（重点）
./test/e2e --block-only    # 仅 ALGO_LORENZO_REG
```

`--fast` 裁剪规则：
- 关键 QoI（XLin, X2, XRecip, XAbs, SumQoI, MultiQoI, Compose）：全维度 + 全模式
- 其余 base QoI：仅 1D
- Regional：仅 1D/2D，仅相关模式
- 其余同 `--full`

**`--full`：不做裁剪，所有 18 种 QoI × 8 种数据 × 3 维 × 2 算法 = 864 条，全部跑。**

## 检查项

每个测试用例需验证：

1. **解压成功** — `SZ_decompress` 返回非空，`conf2.num == n`
2. **QoI 约束严格** — 逐点 `qoi->check_comply(orig[i], dec[i])`，容差 `≤ τ*(1+1e-8)`
3. **绝对误差边界** — `max|orig[i]-dec[i]| ≤ absErrorBound*(1+1e-6)`（如果 `absErrorBound ≥ qEB`）
4. **解压配置一致性** — `conf2.cmprAlgo == conf.cmprAlgo`, `conf2.qoi == conf.qoi`, `conf2.num == n`
5. **压缩大小合理性** — `cmpSize > 0`, `cmpSize < SZ_compress_size_bound`
6. （可选）**压缩率下限** — 对 D1 线性斜坡，压缩率 > 某个阈值（验证算法工作正常）

## 框架结构

```
test/
├── CMakeLists.txt              # 链接 SZ3，纯离线
├── e2e_main.cpp                # 主入口，--fast/--full/--interp-only/--block-only
├── data_gen.hpp                # D1~D8 合成数据生成器
└── test_config.hpp             # 参数结构体：{qoi, dims, algo, mode, tau, absEb}
```

不依赖 GTest（避免编译时间），用宏/循环驱动 + `assert`/`fprintf(stderr)` 即可。每条失败打印详细信息。

## 实现步骤

- [ ] 1. 创建 `test/` 目录和 CMakeLists.txt
- [ ] 2. 实现 `data_gen.hpp`：D1~D8 在 1D/2D/3D 上的生成器
- [ ] 3. 实现 `test_config.hpp`：QoI 配置工厂（qoi id → Config）
- [ ] 4. 实现 `e2e_main.cpp`：CLI 参数解析 + 参数化循环 + 逐条验证
- [ ] 5. 跑 `--fast` 确认通过
- [ ] 6. 跑 `--full` 确认通过
- [ ] 7. 集成到 CI（`--fast` 作为门禁）

## 预计测试数量

| 模式 | 1D | 2D | 3D | 合计 |
|------|-----|-----|-----|------|
| `--full` | 8×18×2 = 288 | 8×18×2 = 288 | 8×18×2 = 288 | **864** |
| `--fast` | 8×18×2 = 288 | 6×8×2 = 96 | 4×4×2 = 32 | **~416** |

`--full` 每条数据 <6000 点，预计总运行 < 2 分钟。
