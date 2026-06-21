# SZ3 — Error-bounded Lossy Compression with QOI

本仓库在 [SZ3 v3.3.2](https://github.com/szcompressor/SZ3) 基础上维护 **QOI（Quantity of Interest）约束压缩** 插件。支持逐点函数约束、等值线约束、区域聚合约束、滑动窗口卷积约束。

更多文档见 `wiki/`：

- [wiki/sz3.md](wiki/sz3.md) — SZ3 原始 README
- [wiki/qpet.md](wiki/qpet.md) — QPET 插件实现与接口说明
- [wiki/qoi.md](wiki/qoi.md) — QOI 编码格式与 encoder 语法
- [wiki/test.md](wiki/test.md) — 测试清单与 CI

---

## 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

依赖：CMake ≥ 3.14、C++17、zstd。encoder 编译可选 SymEngine 支持（安装后 cmake 自动启用）。

---

## 快速使用

### 1. Encoder CLI — 将 QOI 表达式编码为 (qoi, qoiParams)

```bash
# 点态 nibble
./test/bin/qoi_encoder "sqr+abs"
# 输出: qoi = 0x00000081  qoiParams = (空)

# 卷积约束
./test/bin/qoi_encoder "conv(1,-2,1,0.5)"
# 输出: qoi = 0x8EFFFFFC  qoiParams = AAAAAAAA8D8AAAAAAAAAwAAAAAAAAPA/LUMc6+I2Gj8=

# 区域聚合
./test/bin/qoi_encoder --regional "sqr+cubic"
# 输出: qoi = 0xFFFFFFDE  qoiParams = (空)

# 等值线（Isoline）
./test/bin/qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"
```

### 2. C++ API — 函数调用压缩

```cpp
#include "SZ3/api/SZ3.hpp"

SZ3::Config conf;
conf.setDims({1000});               // 1D, 1000 点
conf.cmprAlgo = SZ3::ALGO_LORENZO_REG;
conf.qoi  = 0x12;                   // nibble模式, 低28bit=nibbles[2,1]=sqr+cubic → SumQoI
conf.qEB = 0.01;                    // 约束误差界

// 区域聚合：约束 avg(f) 偏差 ≤ qEB
// ./test/bin/qoi_encoder --regional "sqr+cubic"
// conf.qoi = 0xFFFFFFDE;           // ~raw=0x21, bit29-28=00→Regional nibble, 子编码=SumQoI(sqr+cubic)

// 等值线：约束 f(x) 不跨越等值线
// ./test/bin/qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"
// conf.qoi = 0x60000001;           // 高4bit=0x6→Isoline, 子nibble[1]=sqr
// conf.qoiParams = base64_decode("AAAAAAAAFMAAAAAAAAAUQAAAAAAAAAhAexSuR+F6hD8=");  // double[4]: [min=-5, max=5, count=3, meb=0.01]

// 卷积：约束滑动窗口卷积偏差 ≤ tol
// ./test/bin/qoi_encoder "conv(1,-2,1,0.0001)"
// conf.qoi = 0x8EFFFFFC;           // ~raw=0x71000003, 高nibble=0x7→Conv, d=1(1D), w=3
// conf.qoiParams = base64_decode("AAAAAAAA8D8AAAAAAAAAwAAAAAAAAPA/LUMc6+I2Gj8=");  // double[4]: [k0=1, k1=-2, k2=1, tol=0.0001]

size_t cmpSize = SZ_compress(conf, inputData, compressedBuf, bufCapacity);
double *dec = SZ_decompress(conf, compressedBuf, cmpSize);
```

上述 demo 的运行及校验见 [wiki/test_api.cpp](wiki/test_api.cpp)，编译运行：
```bash
g++ -std=c++17 -O2 -Iinclude -Ibuild/include -DSZ3_USE_SKA_HASH=1 -fopenmp \
    wiki/test_api.cpp -o /tmp/test_api \
    -Ltest/lib -lzstd -lgomp -ltinyexpr -fopenmp -lpthread
LD_LIBRARY_PATH=test/lib /tmp/test_api
```

### 3. SZ3 命令行压缩 — 通过 config 文件

sz3 CLI 不支持 `-qoi` 等 flag，qoi 参数通过 `-c <config.ini>` 传入（`[QoISettings]` 段）。

```bash
# nibble（SumQoI: sqr + cubic, qEB=0.01）
./test/bin/qoi_encoder "sqr+cubic"
# → qoi = 0x00000012  qoiParams = ""
cat > /tmp/my.ini << 'EOF'
[QoISettings]
qoi = 0x00000012
qoiEB = 0.01
qoiQuantbinCnt = 65536
EOF
./test/bin/sz3 -f -i input.bin -z output.sz3 -1 10000 -c /tmp/my.ini

# regional
./test/bin/qoi_encoder --regional "sqr+cubic"
# → qoi = 0xFFFFFFDE  qoiParams = ""
cat > /tmp/my.ini << 'EOF'
[QoISettings]
qoi = 0xFFFFFFDE
qoiEB = 0.02
qoiQuantbinCnt = 65536
EOF
./test/bin/sz3 -f -i input.bin -z output.sz3 -1 10000 -c /tmp/my.ini

# isoline
./test/bin/qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"
# → qoi = 0x60000001  qoiParams = AAAAAAAAFMAAAAAAAAAUQAAAAAAAAAhAexSuR+F6hD8=
cat > /tmp/my.ini << 'EOF'
[QoISettings]
qoi = 0x60000001
qoiEB = 1.0
qoiParams = AAAAAAAAFMAAAAAAAAAUQAAAAAAAAAhAexSuR+F6hD8=
qoiQuantbinCnt = 65536
EOF
./test/bin/sz3 -f -i input.bin -z output.sz3 -1 10000 -c /tmp/my.ini

# 卷积
./test/bin/qoi_encoder "conv(1,-2,1,0.0001)"
# → qoi = 0x8EFFFFFC  qoiParams = AAAAAAAA8D8AAAAAAAAAwAAAAAAAAPA/LUMc6+I2Gj8=
cat > /tmp/my.ini << 'EOF'
[QoISettings]
qoi = 0x8EFFFFFC
qoiEB = 1.0
qoiParams = AAAAAAAA8D8AAAAAAAAAwAAAAAAAAPA/LUMc6+I2Gj8=
qoiQuantbinCnt = 65536
EOF
./test/bin/sz3 -f -i input.bin -z output.sz3 -1 10000 -c /tmp/my.ini
```

上述 demo 的运行及校验见 [wiki/test_cli.sh](wiki/test_cli.sh)，运行：
```bash
bash wiki/test_cli.sh
```

---

## 测试

```bash
./test/bin/e2e --full --encoder-path=./test/bin/qoi_encoder
```

详细测试清单见 [wiki/test.md](wiki/test.md)。
