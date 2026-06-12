# 统一 QoI 接口设计 — 总览

## 动机

点态 QoI（XLin, X2）与 Regional QoI（平均值、均方值）使用**同一套 eb 提供接口**，由 `EBProvider` functor 封装 eb 来源，使 `QpetBlockDecomp` 与 eb 来源解耦。

## 核心设计

### 接口层

```
Layer 0 (零 SZ3 依赖, <cmath> + <algorithm> + T/size_t)
├── QoIIf<T,N>      — QoI 算法接口（interpret_eb, check_comply, block hooks）
└── EBProvider<T>   — eb 源统一读取接口（next, reset, save/load）

Layer 1 (继承 Layer 0，独立实现)
├── QoIXLin<T,N>    — 点态线性 f(x)=x       (qoi=0)
├── QoIX2<T,N>      — 点态平方 f(x)=x²      (qoi=1)
├── RegionalMean<T,N> — 区域均值              (qoi=10)
├── RegionalMeanSq<T,N> — 区域均方           (qoi=11)
├── PointwiseEBProvider<T>   — 包装预计算 ebs[]
└── RegionalEBProvider<T,N>  — 在线 budget 跟踪
```

### EBProvider — 统一 eb 入口

`QpetBlockDecomp` **不再直接访问 `conf.ebs[]`**，始终通过 `EBProvider` 获取 eb：

```
QpetBlockDecomp 压缩循环:
  do {
    provider->precompress_block(blk_size)
    foreach point:
      eb = provider->advance(orig, dec)   ← 统一入口
      // qnt.qnt_eb(eb); qnt.qnt_overwrite(...)
    provider->postcompress_block()
  } while (blk.next())
```

### 点态与 Regional 对比

| 方面 | 点态 (XLin/X2) | Regional (Mean/MeanSq) |
|---|---|---|
| eb 来源 | 预计算 `ebs[]` | 在线 budget 跟踪 |
| `EBProvider.advance()` | 返回 `ebs[pos++]`，忽略参 | 用 orig 算 eb，调用 `update_tolerance(orig,dec)`，pos++ |
| 预计算时机 | 构造时一次性算完 | 不预计算，逐点在线算 |
| save/load | 序列化 ebs 数组 | 只序列化 τ, geb |
| `precompress_block` | no-op | 初始化累加器 |
| `check_comply` | `\|f(orig)-f(dec)\| ≤ τ` | 恒 true（预算跟踪保证） |
| 解压端 | eb 从 `recv_eb(qi_eb)` 恢复 | 同上 |

### EBProvider 接口

```cpp
template <typename T>
class EBProvider {
public:
    virtual ~EBProvider() = default;

    virtual void precompress_block(size_t num_elements) = 0;
    virtual T advance(T orig, T dec) = 0;   // compress: 返回 eb + 更新预算
    virtual void advance() = 0;              // decompress: 仅推进
    virtual void postcompress_block() = 0;
    virtual void reset() = 0;

    virtual void save(uchar *&c) const = 0;
    virtual void load(const uchar *&c, size_t &remaining_length) = 0;
};
```

### QoIIf 接口（扩展后）

```cpp
template <class T, uint N>
class QoIIf {
public:
    virtual ~QoIIf() = default;

    // Core
    virtual T interpret_eb(T x) const = 0;
    virtual bool check_comply(T orig, T dec) const = 0;

    // Block hooks（点态 no-op, regional 实现）
    virtual void precompress_block(size_t num_elements) {}
    virtual void update_tolerance(T orig, T dec) {}
    virtual void postcompress_block() {}

    // Config
    virtual T get_geb() const = 0;
    virtual void set_geb(T) = 0;
    virtual void set_tol(double) = 0;

    int id = 0;
};
```

### 数据流变化

```
压缩:
  data → SZDispatcher:
    ├─ pointwise: QoI→interpret_eb(data) 逐点 → ebs[] → PointwiseEBProvider(ebs)
    └─ regional: 不预计算 → RegionalEBProvider(qoi)

  QpetBlockDecompress:
    provider.precompress_block(n)
    foreach point:
      eb = provider.advance(orig, dec)   // pointwise: 读数组; regional: 算 budget
      qe = qnt.qnt_eb(eb)
      qd = qnt.qnt_overwrite(*c, prd, eb)
    provider.postcompress_block()

解压:
  conf.load(...) → 恢复 QoI 参数
  EBProvider: load(c, rl) → 恢复 provider 状态
  循环同压缩（eb 从 recv_eb(qi_eb) 来，provider.advance() 只推进计数器）
```

### 文件修改/新增清单

| 文件 | 操作 | 依赖 |
|---|---|---|
| `qoi/QoI.hpp` | 修改：新增 `precompress_block`, `update_tolerance`, `postcompress_block` 默认空方法 | Layer 0 |
| `qoi/EBProvider.hpp` | **新建**：`EBProvider<T>` 抽象接口 | Layer 0 |
| `qoi/QoIXLin.hpp` | 修改极小：加上空的 block hooks 继承 | Layer 1 |
| `qoi/QoIX2.hpp` | 同上 | Layer 1 |
| `qoi/PointwiseEBProvider.hpp` | **新建**：包装预计算 ebs[] | Layer 1 |
| `qoi/RegionalMean.hpp` | **新建**：区域均值 QoI + 其 EBProvider | Layer 1 |
| `qoi/RegionalMeanSq.hpp` | **新建**：区域均方 QoI + 其 EBProvider | Layer 1 |
| `qoi/QoIIf.hpp` | 修改：`GetQOI` 增加 regional 分支 | Layer 1 |
| `decomposition/QpetBlockDecomp.hpp` | 修改：`conf.ebs[off++]` → `provider->advance(orig, dec)` | Layer 2 |
| `api/impl/SZDispatcher.hpp` | 修改：regional 时跳过 ebs[] 预计算 | Layer 3 |

### 模块独立性

所有 Layer 0+1 文件只依赖 `<cmath>`, `<algorithm>`, `<cstring>`, `SZ3/def.hpp`（仅 `uchar`, `uint` 定义）。

新 SZ3 升级时移植范围：
- 复制 `qoi/`（10 个文件）
- 适配 `QpetBlockDecomp.hpp`（~10 行改动）
- 适配 `SZDispatcher.hpp`（~5 行改动）
