# Regional Mean（区域均值）

## 约束定义

```
| (1/n) · Σ(dec_i - orig_i) | ≤ τ
⇒ | Σ(dec_i - orig_i) | ≤ τ · n
```

即解压后整个块的平均值与原始块的平均值之差不超过 τ。

## QoI_RegionalMean

对应 `qoi=10`。

### 构造参数

```
tol (double) — τ, 每元素容差
geb (T)     — 全局 eb 上限
```

### 内部状态

```cpp
double aggregated_tolerance;   // 总预算 = tol × n
double error;                   // 累计误差 Σ(orig - dec)
int rest_elements;              // 块内剩余未处理元素数
int block_elements;             // 块内总元素数
```

### precompress_block

块开始时初始化预算：

```cpp
void precompress_block(size_t num_elements) override {
    rest_elements = num_elements;
    block_elements = num_elements;
    aggregated_tolerance = tol * num_elements;
    error = 0;
}
```

### interpret_eb

```
eb = (aggregated_tolerance - |error|) / rest_elements;
if (rest_elements > 0.5 * block_elements)
    eb *= 2;                     // 前半段更宽松
return min(eb, geb);
```

QoZ 实现中前半段 `*2` 的原因是：前半段还不知道实际误差情况，给更宽松的预算避免过早触发过小 eb；后半段按剩余预算平均分配，更精确。

### update_tolerance

每点量化后更新误差与剩余计数：

```cpp
void update_tolerance(T orig, T dec) override {
    error += orig - dec;
    rest_elements--;
}
```

### check_comply

恒 true——预算由 `interpret_eb` 和 `update_tolerance` 闭环控制，且 eb 已向下取整量化，轻微越界可接受。

```cpp
bool check_comply(T orig, T dec) const override { return true; }
```

### postcompress_block

可选择块结束时验证：

```cpp
void postcompress_block() override {
    // assert(fabs(error) <= aggregated_tolerance + 1e-10);
}
```

---

## RegionalMeanEBProvider

包装 `QoI_RegionalMean`，从 budget 在线提供 eb。

### 类定义

```cpp
template <typename T, uint N>
class RegionalMeanEBProvider : public EBProvider<T> {
public:
    RegionalMeanEBProvider(QoI_RegionalMean<T, N> *qoi)
        : qoi(qoi), pos(0) {}

    void precompress_block(size_t n) override {
        qoi->precompress_block(n);
        pos = 0;
    }

    T advance(T orig, T dec) override {
        T eb = qoi->interpret_eb(orig);    // 将 orig 传入
        qoi->update_tolerance(orig, dec);
        pos++;
        return eb;
    }

    void advance() override { pos++; }     // decompress: 仅推进

    void postcompress_block() override { qoi->postcompress_block(); }

    void reset() override {
        qoi->precompress_block(0);
        pos = 0;
    }

    void save(uchar *&c) const override {
        write(qoi->get_geb(), c);
        double tol = /* 需从 qoi 获取 tol */;
        write(tol, c);
        // 注意: ebs[] 不序列化（解压时从 qi_eb 恢复）
    }

    void load(const uchar *&c, size_t &rl) override {
        T geb; read(geb, c, rl);
        double tol; read(tol, c, rl);
        qoi->set_geb(geb);
        qoi->set_tol(tol);
    }

private:
    QoI_RegionalMean<T, N> *qoi;
    size_t pos;
};
```

### save/load 特性

与传统 scheme 不同，Regional QoI 的 `save()` 只写 QoI 参数（τ, geb），**不写 ebs[] 数组**。解压端从压缩数据的 `qi_eb` 序列恢复 eb，不需要预计算 ebs[]。

```
压缩数据布局（与点态一致）:
  [ qi_eb_0 | qi_eb_1 | ... | qi_eb_{n-1} | qi_data_0 | ... | qi_data_{n-1} ]
                                                                                 ↑ 解压时通过 recv_eb(qi_eb) → eb
```

### 构造时机

```cpp
// SZDispatcher (regional 路径)
auto qoi = std::make_shared<QoI_RegionalMean<T, N>>(conf.qEB, conf.absErrorBound);
auto provider = RegionalMeanEBProvider<T, N>(qoi.get());
// 不预计算 conf.ebs[]
```

---

## 与 QoZ 的差异对照

| 项 | QoZ `QoI_RegionalAverage` | 本设计 |
|---|---|---|
| eb 公式 | `(agg_tol - \|error\|) / rest`，前半段 *2 | 相同 |
| update_tolerance | `error += orig - dec` | 相同 |
| check_compliance | 恒 true | 相同 |
| 接口使用 | 在 Interp 压缩器中 inline 调用，但 `update_tolerance` 被注释掉 | 通过 `EBProvider::advance()` 统一调用 |
| save/load | 通过 QoI 旧接口 | 通过 `EBProvider::save/load`，只传 τ, geb |
