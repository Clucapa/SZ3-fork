# 统一 QoI 接口 — 测试计划

## 测试模块文件

```
tools/test/modules/
├── test_qpet_qoi.cpp          ← 已有（XLin, X2 单元测试）
├── test_qpet_eb_provider.cpp  ← 新建（EBProvider 测试）
└── test_qpet_regional.cpp     ← 新建（Regional QoI 测试）
```

---

## Phase 1: PointwiseEBProvider 单元测试

**文件**: `test_qpet_eb_provider.cpp`

### 1.1 基本构造 + advance

```cpp
float ebs[] = {0.1, 0.2, 0.3, 0.4, 0.5};
PointwiseEBProvider<float> provider(ebs, 5);

provider.precompress_block(5);           // no-op
EXPECT_FLOAT_EQ(provider.advance(0,0), 0.1f);  // 返回 ebs[0]
EXPECT_FLOAT_EQ(provider.advance(0,0), 0.2f);  // 返回 ebs[1]
EXPECT_FLOAT_EQ(provider.advance(0,0), 0.3f);  // 返回 ebs[2]
```

### 1.2 reset

```cpp
provider.reset();
EXPECT_FLOAT_EQ(provider.advance(0,0), 0.1f);  // 回到开头
```

### 1.3 decompress advance（无参版本）

```cpp
provider.precompress_block(5);
provider.advance(1.0, 0.95);   // compress version (ignored)
provider.advance();             // decompress version (just idx++)
// 两种 advance 均只推进 pos，值从 ebs 数组来
```

### 1.4 save/load 往返

```cpp
// save
uchar *buf = new uchar[1024];
uchar *p = buf;
provider.save(p);
size_t saved = p - buf;

// load
PointwiseEBProvider<float> loaded(nullptr, 0);
const uchar *cp = buf;
size_t rl = saved;
loaded.load(cp, rl);

// verify loaded behaves same
loaded.precompress_block(5);
for (int i = 0; i < 5; i++)
    EXPECT_FLOAT_EQ(loaded.advance(0,0), ebs[i]);
```

### 1.5 与 QpetBlockDecomp 模拟集成

```cpp
float ebs[4] = {0.01, 0.01, 0.02, 0.02};
PointwiseEBProvider<float> provider(ebs, 4);
auto qoi = QoI_XLin<float, 1>(0.01, 1.0);

provider.precompress_block(4);
T input[4] = {1.0, 1.5, 2.0, 2.5};
T pred[4]  = {0.9, 1.4, 1.8, 2.3};
for (int i = 0; i < 4; i++) {
    T eb = provider.advance(input[i], pred[i]);
    EXPECT_FLOAT_EQ(eb, ebs[i]);                   // eb matches precomputed
    T dec = /* simulate quantization */ input[i];
    EXPECT_TRUE(qoi.check_comply(input[i], dec));  // pointwise check
}
```

---

## Phase 2: RegionalMeanEBProvider 单元测试

**文件**: `test_qpet_regional.cpp`

### 2.1 基本 budget 跟踪

```cpp
auto qoi = QoI_RegionalMean<float, 1>(0.01, 1.0);
RegionalMeanEBProvider<float, 1> provider(&qoi);

provider.precompress_block(100);    // total budget = 0.01 * 100 = 1.0

T eb1 = provider.advance(1.0, 0.995);  // error += 0.005, rest = 99
EXPECT_NEAR(eb1, 0.01f, 1e-6);          // eb = 1.0/100 = 0.01 (前半段 *2)

T eb2 = provider.advance(2.0, 1.990);  // error += 0.01, rest = 98
// eb2 = (1.0 - |0.015|) / 98 * 2 (前半段)
// but if past half point:
// eb = (1.0 - |error|) / rest
```

### 2.2 预算耗尽边界

```cpp
auto qoi = QoI_RegionalMean<float, 1>(0.001, 1.0);
RegionalMeanEBProvider<float, 1> provider(&qoi);

provider.precompress_block(10);    // total = 0.01

// 模拟 5 个点各误差 0.002 → 已用 0.01，预算耗尽
for (int i = 0; i < 5; i++) {
    provider.advance(1.0, 0.998);   // each: error += 0.002
}
// 第 6 个点: remaining = 0, eb ≈ 0
T eb6 = provider.advance(1.0, 0.999);
EXPECT_NEAR(eb6, 0.0f, 1e-6);
```

### 2.3 geb 截断

```cpp
auto qoi = QoI_RegionalMean<float, 1>(0.1, 0.001);  // geb = 0.001
RegionalMeanEBProvider<float, 1> provider(&qoi);

provider.precompress_block(10);    // budget = 1.0, per-point would be 0.1
T eb = provider.advance(1.0, 1.0);
EXPECT_FLOAT_EQ(eb, 0.001f);       // capped by geb
```

### 2.4 save/load 往返

```cpp
// compress side
auto qoi1 = QoI_RegionalMean<float, 1>(0.01, 1.0);
RegionalMeanEBProvider<float, 1> p1(&qoi1);
p1.precompress_block(10);
p1.advance(1.0, 0.99);   // simulate some progress

uchar buf[64];
uchar *bp = buf;
p1.save(bp);
size_t saved = bp - buf;
EXPECT_LE(saved, sizeof(buf));

// decompress side
auto qoi2 = QoI_RegionalMean<float, 1>(0, 0);  // dummy init
RegionalMeanEBProvider<float, 1> p2(&qoi2);
const uchar *cbp = buf;
size_t rl = saved;
p2.load(cbp, rl);
p2.precompress_block(10);

EXPECT_EQ(qoi2.get_geb(), 1.0f);
// tol should be restored to 0.01
```

---

## Phase 3: RegionalMeanSqEBProvider 单元测试

**文件**: `test_qpet_regional.cpp`（同上）

### 3.1 基本 budget + f⁻¹ 反解

```cpp
auto qoi = QoI_RegionalMeanSq<float, 1>(1.0, 10.0);
RegionalMeanSqEBProvider<float, 1> provider(&qoi);

provider.precompress_block(10);    // total budget = 1.0 * 10 = 10.0 (in sq domain)

// Point 0: orig = 3.0
T eb = provider.advance(3.0, 2.995);
// eb_sq = 10.0 / 10 = 1.0
// eb = -3.0 + sqrt(9.0 + 1.0) = -3.0 + 3.1623 ≈ 0.1623
EXPECT_NEAR(eb, 0.1623f, 1e-3);

// After point 0: error += 3.0² - 2.995² ≈ 9 - 8.970 = 0.030
// Point 1: eb_sq = (10.0 - |0.030|) / 9 ≈ 1.108
T eb2 = provider.advance(5.0, 4.990);
// eb2 = -5.0 + sqrt(25.0 + 1.108) = -5.0 + 5.110 ≈ 0.110
EXPECT_NEAR(eb2, 0.110f, 1e-3);
```

### 3.2 与 QoI_X2 的一致性和差异

```cpp
// QoI_X2: 固定 tol = 1.0
auto x2 = QoI_X2<float, 1>(1.0, 10.0);
T eb_x2 = x2.interpret_eb(3.0);                  // -3 + sqrt(9+1) ≈ 0.1623

// RegionalMeanSq: 刚启动时 tol_effective = budget/rest = 1.0
auto rmq = QoI_RegionalMeanSq<float, 1>(1.0, 10.0);
rmq.precompress_block(1);                         // 单元素块
T eb_rm = rmq.interpret_eb(3.0);                 // eb_sq = 1.0/1 = 1.0

EXPECT_FLOAT_EQ(eb_x2, eb_rm);  // 两者相等（块大小=1时）
```

### 3.3 前半段 *2 策略

`RegionalMeanSq` 不沿用 `RegionalMean` 的前半段 *2 策略（与 QoZ 一致）。

验证：

```cpp
auto qoi = QoI_RegionalMeanSq<float, 1>(1.0, 10.0);
RegionalMeanSqEBProvider<float, 1> provider(&qoi);

provider.precompress_block(100);
// 前半段第 1 点: eb_sq = 1.0*100/100 = 1.0
T eb = provider.advance(1.0, 0.999);
T expected_sq = 1.0;
T expected_eb = -1.0 + sqrt(1.0 + expected_sq);
EXPECT_NEAR(eb, expected_eb, 1e-6);  // 没有 *2
```

---

## Phase 4: 端到端集成测试

**文件**: `test_qpet_regional.cpp`（或新建 `test_qpet_e2e.cpp`）

### 4.1 RegionalMean 完整压缩/解压

```cpp
Config conf;
conf.num = 100;
conf.dims = {100};
conf.blockSize = 50;
conf.absErrorBound = 1.0;    // geb
conf.qEB = 0.01;             // τ
conf.qoi = 10;               // RegionalMean

float data[100];
// fill with ramp

// compress via SZ_compress_dispatcher<float, 1>
// decompress via SZ_decompress_dispatcher<float, 1>

// verify: |avg(dec) - avg(orig)| ≤ τ
float orig_mean = /* sum(data) / 100 */;
float dec_mean  = /* sum(dec) / 100 */;
EXPECT_LE(fabs(dec_mean - orig_mean), conf.qEB + 1e-6);
```

### 4.2 RegionalMeanSq 完整压缩/解压

```cpp
conf.qoi = 11;               // RegionalMeanSq

// same flow

// verify: |avg(dec²) - avg(orig²)| ≤ τ
```

### 4.3 点态 (XLin, qoi=0) 回归

确认点态路径仍正常工作（EBProvider 改为 PointwiseEBProvider 后行为不变）：

```cpp
conf.qoi = 0;
conf.qEB = conf.absErrorBound;
// 压缩 + 解压
// 逐点 |dec[i] - orig[i]| ≤ conf.absErrorBound
```

---

## Phase 5: 边界和异常

### 5.1 空数据

```cpp
PointwiseEBProvider<float> provider(nullptr, 0);
provider.precompress_block(0);
// 不应崩溃
```

### 5.2 单元素块

```cpp
auto qoi = QoI_RegionalMean<float, 1>(0.01, 1.0);
RegionalMeanEBProvider<float, 1> provider(&qoi);
provider.precompress_block(1);
T eb = provider.advance(5.0, 5.0);
// eb = 0.01 * 1 / 1 = 0.01 (if no *2)
// 或 eb = 0.02 (if *2, 前半段)
```

### 5.3 多次 save/load 来回

```cpp
// 构造 → 处理几个点 → save → load → 继续处理
// 验证状态连续性
```

---

## 测试运行

```bash
# 编译
cmake -S . -B build && cmake --build build -j

# 运行
./test/bin/test_qpet_eb_provider
./test/bin/test_qpet_regional

# 全部测试
ctest --test-dir build
```
