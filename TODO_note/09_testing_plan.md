# 测试方案

测试代码放在 `tools/test/modules/` 下。

## Phase 1: EBLogQnt 单元

**文件**: `tools/test/modules/test_qpet_eblog.cpp`

1. 基本 round-trip: `eb=0.005`, `base=0.001`, `lbase=2` → `qi = qnt_overwrite(eb)`, 验证 `eb_out ≤ eb_in`（floor 安全）
2. 边界: `eb ≤ base` → `qi=0`, `eb=geb`
3. `recv_eb(qi)` 往返一致
4. save/load 往返

## Phase 2: QpetQnt 单元

**文件**: 同上

1. 基本 round-trip: `d=1.0, pred=0.5, eb=0.1` → `qi = qnt_overwrite(d, pred, eb)`, 验证 `|d' - 1.0| ≤ eb'`（eb' 为量化后值）
2. flush 正确：3 个点 → `buf_eb.size()==3, buf_d.size()==3`
3. `recv` + `recv_eb` 往返：压缩端 `qi_d, qi_e` → 解压端 `eb=recv_eb(qi_e)`, `d'=recv(pred, qi_d, eb)`, 验证误差
4. eb=0 路径 → unpred
5. 合规失败模拟
6. save/load 往返

## Phase 3: QoI 单元

**文件**: `tools/test/modules/test_qpet_qoi.cpp`

### QoI_XLin (qoi=0)

1. `interpret_eb(任意值, τ=0.001)` → 返回 `min(0.001, geb)`
2. `check_comply`: `fabs(orig-dec) ≤ τ`
3. `set_tol` 更新正确

### QoI_X2 (qoi=1)

1. `interpret_eb(0, τ=1)` → `eb = sqrt(1) - 0 = 1.0`
2. `interpret_eb(100, τ=1)` → `eb = sqrt(10001) - 100 ≈ 0.005`
3. `eb ≤ geb` capping
4. `check_comply`: `|3.1²-3²|=0.61≤1.0 → true`; `|3.5²-3²|=3.25>1.0 → false`
5. `set_geb` / `set_tol` 更新正确

## Phase 4: QpetBlockDecomp 单元

**文件**: 同上

1. 小数据 round-trip: 1D 16 点, 固定 eb, 压缩/解压, 验证误差
2. 合规回退: 强制某点 eb 极小 → 验证走 unpred
3. flush 正确: 验证 qi_vec 长度 = 2 * num, 前后半分离正确

## Phase 5: 端到端集成 (qoi=1)

**文件**: `tools/test/modules/test_qpet_integration.cpp`

测试数据: `x ∈ [0, 100]`, 1D 均匀采样, ~1M 点

压缩参数:
```cpp
conf.cmprAlgo = ALGO_LORENZO_REG;
conf.absEB    = 1.0;
conf.qoi      = 1;      // x² QoI
conf.qEB      = 1.0;    // τ = 1
conf.qR       = 32;
conf.lorenzo  = true;
```

验证:
1. `SZ_compress_LorenzoReg` + `SZ_decompress_LorenzoReg`
2. `cmpSize > 0`
3. 逐点 `|dec[i]² - orig[i]²| ≤ 1.0`
4. 统计违反点 = 0

## Phase 6: qoi=0 走 QpetQnt（等价标准 SZ3）

**文件**: 同上

```cpp
conf.qoi = 0;
conf.qEB = conf.absEB;  // QoI_XLin: eb ≡ absEB
```

走 QpetQnt + QpetBlockDecomp 完整流程，验证 `|dec[i] - orig[i]| ≤ conf.absEB`。

注意：`qoi=0` 不走传统 `LinearQnt`。始终用 `QpetQnt`，`QoI_XLin` 产生均匀 eb。验证 qi_eb 序列全为 0（EBLogQnt 量化后），但 `recv_eb(0) → geb`，数据恢复正确。

## Phase 7: SZDispatcher + 持久化

1. 通过 `SZ_compress_dispatcher` + `SZ_decompress_dispatcher`
2. 字节数组持久化往返
3. `conf.dims` 前后一致

## qoi=0 等价性

v1 中 `qoi=0` 走 QpetQnt + QoI_XLin（均一 eb），不走传统 LinearQnt。Phase 6 验证其与传统 SZ3 行为等价。
