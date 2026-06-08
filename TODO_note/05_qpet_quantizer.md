# QpetQnt 量化器（Layer 0+1，三步合一，可移植）

## 文件

```
include/SZ3/quantizer/
├── Quantizer.hpp     (修改: 新增 QpetQntIf)
├── EBLogQnt.hpp      (新增: log 尺度量化 eb)
└── QpetQnt.hpp       (新增: 三步合一)
```

## QpetQntIf<Ti, To> (Layer 0)

在 `Quantizer.hpp` 中，`QuantizerIf` 之后新增：

```cpp
template <class Ti, class To>
class QpetQntIf : public QuantizerIf<Ti, To> {
public:
    // 带逐点 eb 的量化 (步骤①②)
    virtual To qnt_overwrite(Ti &d, Ti pred, Ti eb) = 0;

    // 带逐点 eb 的恢复
    virtual Ti recv(Ti pred, To qi, Ti eb) = 0;

    // 恢复 eb (从 qi_eb → double)
    virtual Ti recv_eb(To qe) = 0;

    // 步骤③: flush 本块的 {qi_eb, qi_data} 到 out
    virtual void flush(std::vector<To> &out) = 0;

    // 清空缓冲 (块开始前调用)
    virtual void clear_bufs() = 0;
};
```

## EBLogQnt<T> (Layer 1, 独立类)

**文件**: `EBLogQnt.hpp`, 继承 `QuantizerIf<T, int>`

```cpp
template <class T>
class EBLogQnt : public concepts::QuantizerIf<T, int> {
    double base;       // 最小可分辨 eb
    double lbase;      // 对数底
    int    r;          // 量化级数
    T      geb;        // 全局 eb

    // pre/post/force_save_unpred: 空实现 (eb 量化无 unpred)

    // 量化 (floor → 安全)
    int qnt_overwrite(T &eb) {
        if (eb <= base || eb >= geb) { eb = geb; return 0; }
        int id = (int)(log2(eb / base) / log2(lbase));
        if (id == 0) { eb = geb; return 0; }
        id = std::min(id, r);
        eb = pow(lbase, id) * base;   // 覆写, 量化后 eb ≤ 原始 eb
        return id;
    }

    // 恢复
    T recv(T pred, int qi) { return recv_eb(qi); }
    T recv_eb(int qi) {
        return qi ? pow(lbase, qi) * base : geb;
    }

    std::pair<int, int> get_out_range() { return {0, r}; }
};
```

`qnt_overwrite(T& eb)` 完成 `01_plan.md` 数据流中的**步骤①**：log 尺度有损变换 eb，floor 保证量化后 eb ≤ 原始 eb（残差为负）。

## QpetQnt<T> (Layer 1, 三步合一)

**文件**: `QpetQnt.hpp`, 继承 `QpetQntIf<T, int>`

```cpp
template <class T>
class QpetQnt : public concepts::QpetQntIf<T, int> {
    EBLogQnt<T> eb_log;
    std::vector<T>   unpred;     // 不可预测数据
    std::vector<int> buf_eb;     // qi_eb 缓冲
    std::vector<int> buf_d;      // qi_data 缓冲
    size_t idx = 0;              // decompress 用
    int    rd;                   // data quant radius

    // ---------- QuantizerIf 兼容层 ----------
    int  qnt_overwrite(T &d, T pred) override { return 0; }
    T    recv(T pred, int qi) override { return 0; }
    int  force_save_unpred(T ori) override { unpred.push_back(ori); return 0; }

    // ---------- QpetQntIf 核心 ----------

    // 步骤①②: 量化 eb + 量化残差
    int qnt_overwrite(T &d, T pred, T eb) override {
        if (eb == 0) { unpred.push_back(d); buf_eb.push_back(0); buf_d.push_back(0); return 0; }
        // ① log 有损变换 eb
        int qe = eb_log.qnt_overwrite(eb);   // eb 被覆写为量化值
        buf_eb.push_back(qe);
        // ② 用量化后 eb' 量化残差
        T diff = d - pred;
        int qi = (int)(fabs(diff) / eb) + 1;
        if (qi < rd * 2) {
            qi >>= 1; int hi = qi; qi <<= 1;
            int qs;
            if (diff < 0) { qi = -qi; qs = rd - hi; }
            else          { qs = rd + hi; }
            T dec = pred + qi * eb;
            if (fabs(dec - d) > eb) { unpred.push_back(d); buf_d.push_back(0); return 0; }
            d = dec;
            buf_d.push_back(qs);
            return qs;
        }
        unpred.push_back(d);
        buf_d.push_back(0);
        return 0;
    }

    // 恢复
    T recv(T pred, int qi, T eb) override {
        if (qi) return pred + 2 * (qi - rd) * eb;
        return unpred[idx++];
    }

    // 恢复 eb
    T recv_eb(int qe) override { return eb_log.recv_eb(qe); }

    // 步骤③: flush
    void flush(std::vector<int> &out) override {
        out.insert(out.end(), buf_eb.begin(), buf_eb.end());
        out.insert(out.end(), buf_d.begin(),  buf_d.end());
        buf_eb.clear(); buf_d.clear();
    }

    void clear_bufs() override { buf_eb.clear(); buf_d.clear(); }

    std::pair<int, int> get_out_range() override {
        auto r = eb_log.get_out_range();
        return {0, std::max(rd * 2, r.second)};
    }

    // save/load: rd, unpred, eb_log
};
```

## 设计要点

- `EBLogQnt` 是独立类，可单独单元测试
- `QpetQnt` 持有 `EBLogQnt` 实例，在 `qnt_overwrite` 内完成步骤①②，在 `flush` 内完成步骤③
- `buf_eb` 和 `buf_d` 按序 push，`flush` 时将本块数据拼接为 `[qi_eb..|qi_data..]`
- `recv_eb` 和 `recv` 分离，解压端显式调用，逻辑透明
- 量化的 `get_out_range()` 返回 `max(rd*2, eb_log.r)`，保证 Huffman stateNum 覆盖所有索引
