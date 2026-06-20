# Regional 新编码测试计划

## e2e 需要覆盖的场景

### 1. 新旧编码等价性

确保 nibble 新编码和旧 `~0`/`~1` 产生完全相同的 QoI 行为（已有测试作为 baseline）。

| 旧编码 | 新编码 | 语义 |
|--------|--------|------|
| `~0` | `~(0x00000000)` | RegionalMean(x), Block |
| `~1` | `~(0x00000001)` | RegionalMean(x²), Block |
| `~2` | `~(0x40000000)` | RegionalMean(x), Interp |
| `~3` | `~(0x40000001)` | RegionalMean(x²), Interp |

验证方式：用新编码跑一遍已有 e2e QOI 矩阵的 Regional 测试，结果和旧编码完全一致。

### 2. nibble 表达式 Regional

用新编码表达复合函数：

| 用例 | 翻转后 qoi | 翻转前 nibble | qoiParams |
|------|-----------|:--:|------|
| Sqr | `~(0x00000001)` | sqr | "" |
| Cubic | `~(0x00000002)` | cubic | "" |
| Sqrt | `~(0x00000003)` | sqrt | "" |
| Sqr+Cubic | `~(0x00000012)` | sqr+cubic | "" |
| Abs | `~(0x00000008)` | abs | "" |
| Lin(2,0.5) | `~(0x00000000)` | lin | base64([2.0, 0.5]) |
| Sqr+Lin(2,0.5) | `~(0x00000201)` | sqr+lin | base64([2.0, 0.5]) |

所有覆盖 8 数据模式 × 3 维 × Block 算法。Interp 路径用 `~(0x4...)` 前缀。

### 3. RegionalFX

| 用例 | 表达式 | qoiParams |
|------|--------|-----------|
| sin(x)+x² | `sin(x)+x²` | FX binary |
| √x+e^(-x) | `sqrt(x)+exp(-x)` | FX binary |
| x³+2x+1 | `x³+2x+1` | FX binary |

qoi = `~(0x30000000)` (Block) 或 `~(0x70000000)` (Interp)。

验证方式：
1. encoder 产出 qoi + qoiParams
2. 构造 Config，feed 给压缩器
3. 验证 `|Σ f(orig)/N - Σ f(dec)/N| ≤ qEB`

### 4. qoi_encoder CLI

encoder 需支持区域性标记：

```bash
# nibble Regional
./qoi_encoder --regional "sqr+cubic"       → qoi = ~(0x00000012)
./qoi_encoder --regional "lin(2,0.5)+sqr"  → qoi = ~(0x00000201), params = base64(...)

# FX Regional
./qoi_encoder --regional "sin(x)+x²"       → qoi = ~(0x3...), params = FX binary
```

### 5. 修改清单

| 文件 | 变更 |
|------|------|
| `test/test_config.hpp` | QoiDef 的 `qoi` 值更新为新编码 |
| `test/run_tests.hpp` | 新增 RegionalFX 的 `check_regional_fx_comply` |
| `tools/qoi_encoder/main.cpp` | 新增 `--regional` flag |
| `tools/qoi_encoder/encode.hpp` | 新增 `regional_encode()` 函数 |

### 6. 预期测试规模

- 新旧编码等价：4 × 8 × 1 × 1 = 32 条 (1D only for sanity)
- nibble Regional 新函数：7 × 8 × 3 × 1 = 168 条 (Block)
- Interp nibble Regional: 7 × 8 × 2 × 1 = 112 条
- RegionalFX: 3 × 2 × 1 × 1 = 6 条 (Ramp+Sinusoid, Block)
- 总计约 318 条新增
