# 集成层（Layer 3，每版本需重写）

## 原则

`conf.qoi` 始终启用 QpetQnt（包括 `qoi=0` 的 `QoI_XLin`）。不保留传统 `LinearQnt + BlockDecomp` 路径。

## eb 预计算

在 `SZ_compress_dispatcher`（`SZDispatcher.hpp`）中，`calAbsEB` 之后：

```cpp
template <class T, uint N>
size_t SZ_compress_dispatcher(Config &conf, const T *data, uchar *cmpData, size_t cmpCap) {
    assert(N == conf.N);
    calAbsEB(conf, data);

    // QPET eb 预计算（始终执行，qoi=0 时 QoI_XLin 返回 min(qEB, absEB)）
    auto qoi = GetQOI<T, N>(conf);
    conf.ebs.resize(conf.num);
    for (size_t i = 0; i < conf.num; ++i)
        conf.ebs[i] = qoi->interpret_eb(data[i]);

    // 后续 dispatch 不变（ALGO_LORENZO_REG 内部走 QpetQnt）
    // ...
}
```

## SZAlgoLorenzoReg.hpp

无 `if/else` 分支，始终组装 QpetQnt + QpetBlockDecomp：

### compress

```cpp
template <class T, uint N>
size_t SZ_compress_LorenzoReg(Config &conf, T *data, uchar *cmpData, size_t cmpCap) {
    assert(N == conf.N);
    assert(conf.cmprAlgo == ALGO_LORENZO_REG);
    calAbsEB(conf, data);

    auto qoi     = GetQOI<T, N>(conf);
    auto pred    = LorenzoPred<T, N, 1>(conf.absEB);
    auto qnt     = QpetQnt<T>(conf.quantbinCnt / 2,
                               conf.qEBase, conf.qELogB, conf.qR, conf.absEB);
    auto encoder = HuffmanEncoder<int>();
    auto lossless = Lossless_zstd();

    auto decomp  = QpetBlockDecomp<T, N, decltype(pred), decltype(qnt)>(
                        conf, pred, qnt, qoi);
    auto sz = make_compressor_sz_generic<T, N>(decomp, encoder, lossless);
    return sz->compress(conf, data, cmpData, cmpCap);
}
```

### decompress

```cpp
template <class T, uint N>
void SZ_decompress_LorenzoReg(const Config &conf, const uchar *cmpData,
                               size_t cmpSize, T *decData) {
    auto qoi     = GetQOI<T, N>(conf);
    auto pred    = LorenzoPred<T, N, 1>(conf.absEB);
    auto qnt     = QpetQnt<T>(conf.quantbinCnt / 2,
                               conf.qEBase, conf.qELogB, conf.qR, conf.absEB);
    auto encoder = HuffmanEncoder<int>();
    auto lossless = Lossless_zstd();

    auto decomp  = QpetBlockDecomp<T, N, decltype(pred), decltype(qnt)>(
                        conf, pred, qnt, qoi);
    auto sz = make_compressor_sz_generic<T, N>(decomp, encoder, lossless);
    sz->decompress(conf, cmpData, cmpSize, decData);
}
```

## qoi=0 的等效性

`QoI_XLin::interpret_eb(x)` 对所有点返回 `min(qEB, absEB)`，即均匀 eb。`EBLogQnt` 量化后 qi_eb 全为 0（因 eb == geb 或 ≥ eb_base 且 ≤ geb，取 floor → id=0 → eb 被覆写为 geb, qi_eb=0）。Huffman 编码时所有 qi_eb=0 → 极短编码，几乎无额外开销。解压端 `recv_eb(0) → geb`。行为上等价于标准 SZ3 的单一全局 eb，但格式统一。

## 预测器选择复用

v1 硬编码 `LorenzoPred<T,N,1>`。后续可提取为辅助函数，供 QPET 路径按 `conf.lorenzo/conf.lorenzo2/conf.regression` 选择。

## 与之前方案的关键差异

| 项 | 旧方案 | 新方案 (Q3) |
|------|--------|------------|
| qoi=0 | 传统 LinearQnt+BlockDecomp | QpetQnt+QoI_XLin |
| SZAlgoLorenzoReg | if/else 双路径 | 单路径 |
| 文件格式 | 两种 | 始终 [qi_eb\|qi_data] |

## 文件修改清单

| 文件 | 操作 |
|------|------|
| `SZDispatcher.hpp` | `#include "SZ3/qoi/QoIIf.hpp"`, 新增 eb 预计算 |
| `SZAlgoLorenzoReg.hpp` | 替换为 QpetQnt + QpetBlockDecomp（无分支） |
| `Config.hpp` | 新增字段（见 `02_config_mods.md`） |
