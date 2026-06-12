# 点态 QoI（XLin, X2）+ PointwiseEBProvider

## QoI_XLin

`f(x) = x`，对应 `qoi=0`，无 QoI 要求时的线性模拟。

### interpret_eb

```cpp
T interpret_eb(T x) const override {
    return std::min(static_cast<T>(tol), geb);
}
```

与 x 无关，所有点 eb 相同。当 `qEB ≥ absEB` 时等价标准 SZ3 统一 eb。

### check_comply

```cpp
bool check_comply(T orig, T dec) const override {
    return fabs(orig - dec) <= tol;
}
```

### block hooks

均为空实现（点态无块级状态）：

```cpp
void precompress_block(size_t) override {}
void update_tolerance(T, T) override {}
void postcompress_block() override {}
```

### 构造参数

```
tol (double) — 容差 τ
geb (T)     — 全局 eb 上限
```

---

## QoI_X2

`f(x) = x²`，对应 `qoi=1`。

### interpret_eb

由 `|(x ± eb)² - x²| ≤ τ` 解出：

```cpp
T interpret_eb(T x) const override {
    T eb = -fabs(x) + sqrt(x * x + tol);
    return std::min(eb, geb);
}
```

### check_comply

```cpp
bool check_comply(T orig, T dec) const override {
    return fabs(orig * orig - dec * dec) <= tol;
}
```

### block hooks

均为空实现。

### 构造参数

同上。

---

## PointwiseEBProvider

包装预计算 `ebs[]` 数组，QpetBlockDecomp 的统一 eb 入口。

### 类定义

```cpp
template <typename T>
class PointwiseEBProvider : public EBProvider<T> {
public:
    PointwiseEBProvider(const T *ebs, size_t n)
        : ebs(ebs), size(n), pos(0) {}

    void precompress_block(size_t) override {}       // no-op
    void postcompress_block() override {}            // no-op

    T advance(T, T) override {                       // compress: 忽略 orig/dec
        return ebs[pos++];
    }
    void advance() override { pos++; }               // decompress: 仅推进

    void reset() override { pos = 0; }

    void save(uchar *&c) const override {
        write(size, c);
        for (size_t i = 0; i < size; i++) write(ebs[i], c);
    }
    void load(const uchar *&c, size_t &rl) override {
        read(size, c, rl);
        for (size_t i = 0; i < size; i++) read(ebs[i], c, rl);
    }

private:
    const T *ebs;          // 外部预计算数组指针
    size_t size;
    size_t pos;
};
```

### 构造时机

在 `SZDispatcher` 中 precompute ebs[] 后构造：

```cpp
// SZDispatcher (pointwise 路径)
conf.ebs.resize(conf.num);
auto qoi = GetQOI<T, N>(conf);
for (size_t i = 0; i < conf.num; ++i)
    conf.ebs[i] = qoi->interpret_eb(data[i]);

auto provider = PointwiseEBProvider<T>(conf.ebs.data(), conf.ebs.size());
```

### save/load 行为

序列化 `ebs[]` 全数组。解压端从压缩数据恢复 `ebs[]`，`PointwiseEBProvider::advance()` 读取与压缩端相同的 eb 值。

### 与标准 SZ3 数据传输的相似性

`save()` 写入的 `(size, ebs[])` 与标准 SZ3 的 Config save 中保存的 `ebs` 字段格式一致，可按相同方式读写。

---

## QpetBlockDecomp 使用示例

```cpp
// compress
do {
    provider->precompress_block(block_num_elements);
    Block_iter::foreach(blk, [&](T *c, const std::array<size_t, N> &ix) {
        T ori = *c;
        T prd = pwb->predict(blk, c, ix);
        T eb  = provider->advance(ori, *c);     // ← 统一接口（pointwise 忽略前两参）
        int qe = qnt.qnt_eb(eb);
        int qd = qnt.qnt_overwrite(*c, prd, eb);
    });
    provider->postcompress_block();
} while (blk.next());

// decompress
do {
    provider->precompress_block(block_num_elements);
    Block_iter::foreach(blk, [&](T *c, const std::array<size_t, N> &ix) {
        T eb = qnt.recv_eb(*(qeb++));            // eb 从压缩数据恢复
        T prd = pwb->predict(blk, c, ix);
        *c = qnt.recv(prd, *(qd++), eb);
        provider->advance();                     // 仅推进，无 eb 计算
    });
    provider->postcompress_block();
} while (blk.next());
```
