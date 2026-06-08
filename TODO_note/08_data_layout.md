# 数据布局与存储格式

## 格式始终统一

无论 `qoi=0` 还是 `qoi=1`，文件格式始终为 `[qi_eb | qi_data]`。`qoi=0` 时 `QoI_XLin` 产生均匀 eb，`EBLogQnt` 量化后所有 qi_eb=0，Huffman 编码开销极小。

## quant_inds 内存布局

单 `vector<int>`，块内分离、块间拼接：

```
[block0_qi_eb[0..M-1] | block1_qi_eb[0..M-1] | ... |
 block0_qi_data[0..M-1] | block1_qi_data[0..M-1] | ...]
```

总长度 = `2 * conf.num`。前 `conf.num` 个是 qi_eb，后 `conf.num` 个是 qi_data。

## Huffman 编码（A 方案：统一）

所有 qi_eb + qi_data 共用一个 Huffman 编码器：

```cpp
auto out_range = decomp.get_out_range();  // (0, max(rd*2, r_eb))
encoder.preprocess_encode(quant_inds, out_range.second);
```

### 为什么不分离

- **理论熵差** < 0.5 bit/symbol（合并树 65537 叶子 vs 分离树 33+65536 叶子）
- **频率决定编码长度**：eb 值小 (0..32) 不自动拿短码；高频 data 符号（如 radius 附近的零误差符号）拿最短码
- **Zstd 均等化**：Huffman 输出的字节流无论怎么组织，进 Zstd 后通过字典匹配抵消剩余差异
- **零改动 SZGenericCompressor**：不需要修改 Encoder 接口或 Compressor 的 `compress()` 逻辑

### 如果有疑虑

v2 可切换到双 Huffman 而不影响 L0/L1/L2 的模块代码——只需修改 `SZAlgoLorenzoReg.hpp` 的组装部分。

## 压缩文件字节级格式

```
[Config::save()] [Decomp::save()] [Encoder::save()]
[qi_cnt] [Encoder::encode(qi_vec)]
←─────────── Lossless_zstd 压缩 ───────────→
```

`Decomp::save()` 内包含：
1. `fallback_pred.save()` — LorenzoPred 序列化
2. `pred.save()` — 主预测器序列化
3. `qnt.save()` — QpetQnt 序列化 (rd, unpred, eb_log[base, lbase, r])

## 解压端数据切分

`decompress` 得到 `qi_vec`（长度 = `2 * conf.num`），切分为两个指针：

```cpp
int *qeb = &qi_vec[0];                         // qi_eb[0..num-1]
int *qd  = &qi_vec[conf.num];                  // qi_data[0..num-1]
```

然后按块遍历：

```cpp
do {
    foreach (元素): eb = qnt.recv_eb(*(qeb++)); *c = qnt.recv(pred, *(qd++), eb);
} while (blk.next());
```

## 合规失败的回退

当 `!qoi->check_comply(ori, dec)` 时：

1. `*c = ori`（恢复原始数据）
2. `qnt.qnt_overwrite(*c, 0, 0.0)`：`eb=0` → 内部 `eb_log.qnt_overwrite(eb)` → `qi_eb=0, eb=geb`；数据走 unpred → `qi_data=0`
3. 解压端：`qi_eb=0` → `recv_eb(0)` → `geb`；`qi_data=0` → `recv(pred,0,eb)` → 从 `unpred[]` 取回原始数据

## 块结构

块边界不保留在 quant_inds 中（与标准 SZ3 一致）。块结构仅存在于 predictor 的序列化元数据中（每块的回归系数等），由 `pred.save()` / `pred.load()` 处理。
