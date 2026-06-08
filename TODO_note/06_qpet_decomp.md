# QpetBlockDecomp 编排器（Layer 2，可移植）

## 文件

`include/SZ3/decomposition/QpetBlockDecomp.hpp`

## 模板参数

```cpp
template <class T, uint N, class Pred, class Qnt>
class QpetBlockDecomp : public concepts::DecompIf<T, int, N>
```

其中 `Pred` 满足 `PredictorIf<T,N>`，`Qnt` 满足 `QpetQntIf<T,int>`。

## 构造

```cpp
QpetBlockDecomp(const Config &conf, Pred pred, Qnt qnt,
                std::shared_ptr<concepts::QoIIf<T,N>> qoi)
    : pred(pred), qnt(qnt), qoi(qoi),
      fallback_pred(LorenzoPred<T, N, 1>(conf.absEB)) {}
```

持有: `pred`, `qnt`, `qoi`, `fallback_pred`。

## compress()

```cpp
std::vector<int> compress(const Config &conf, T *data) override {
    auto dpad = std::make_shared<block_data<T,N>>(data, conf.dims, pred.get_pad(), true);
    auto blk  = dpad->block_iter(conf.blockSize);
    std::vector<int> qis;
    qis.reserve(conf.num * 2);

    do {
        concepts::PredictorIf<T,N> *pwb = &pred;
        if (!pred.precompress(blk)) pwb = &fallback_pred;
        pwb->precompress_block_commit();

        Block_iter::foreach(blk, [&](T *c, const std::array<size_t,N> &ix) {
            T eb  = conf.ebs[offset];               // 预计算
            T prd = pwb->predict(blk, c, ix);
            T ori = *c;
            qnt.qnt_overwrite(*c, prd, eb);         // 步骤①②: 量化 eb + 数据; *c 被覆写

            if (!qoi->check_comply(ori, *c)) {       // 合规失败 → 标记 unpred
                *c = ori;
                qnt.qnt_overwrite(*c, (T)0, (T)0);   // eb=0 → qi_eb=0, qi_data=0
            }
        });

        qnt.flush(qis);    // 步骤③: 块结束时 flush {qi_eb, qi_data}
    } while (blk.next());

    return qis;
}
```

**输出格式**: 每块 flush 追加 `[qi_eb.. | qi_data..]`，最终 `qis` 长度 = `2 * conf.num`。

## decompress()

```cpp
T *decompress(const Config &conf, std::vector<int> &qis, T *dec) override {
    int *qeb = &qis[0];                          // qi_eb 区起始
    int *qd  = &qis[conf.num];                   // qi_data 区起始

    auto dpad = std::make_shared<block_data<T,N>>(dec, conf.dims, pred.get_pad(), false);
    auto blk  = dpad->block_iter(conf.blockSize);
    do {
        concepts::PredictorIf<T,N> *pwb = &pred;
        if (!pred.predecompress(blk)) pwb = &fallback_pred;
        Block_iter::foreach(blk, [&](T *c, const std::array<size_t,N> &ix) {
            T eb  = qnt.recv_eb(*(qeb++));       // 恢复 eb
            T prd = pwb->predict(blk, c, ix);
            *c = qnt.recv(prd, *(qd++), eb);     // 恢复 data
        });
    } while (blk.next());

    return dec;
}
```

**解压端数据获取**: `qis` 前 `conf.num` 个是 qi_eb，后 `conf.num` 个是 qi_data。

## save / load

```cpp
void save(uchar *&c) override {
    fallback_pred.save(c);
    pred.save(c);
    qnt.save(c);           // qnt.save 内含 eb_log.save + rd + unpred
}

void load(const uchar *&c, size_t &rl) override {
    fallback_pred.load(c, rl);
    pred.load(c, rl);
    qnt.load(c, rl);
}
```

## get_out_range()

```cpp
std::pair<int, int> get_out_range() override {
    return qnt.get_out_range();   // = (0, max(rd*2, eb_log.r))
}
```

## 关键设计

- **flush 在块边界调用**：块循环结束后 `qnt.flush(qis)` 导出本块的 qi_eb + qi_data。Decomposition 知道块边界，Qnt 不需要。
- **recv_eb 分离**：解压时显式调用 `qnt.recv_eb(qe)` 恢复 eb，再传 `qnt.recv(pred, qd, eb)` 恢复 data。逻辑透明。
- **合规回退**：`check_comply` 失败 → 恢复原始数据、eb=0、qi_eb=0、qi_data=0（unpred）。解压端 `recv(qi=0)` → 从 `unpred` 取回。
- **输出格式**：始终 `[qi_eb.. | qi_data..]`，单 Huffman + 单 Zstd（见 `08_data_layout.md`）。
