#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
SZ3="$DIR/../test/bin/sz3"
ENCODER="$DIR/../test/bin/qoi_encoder"

echo "=== QOI 流程校验测试 ==="
echo

# ---------- generate data ----------
echo ">>> 生成测试数据 (3×sin @32% + 4% noise, 10000 float)"
python3 -c "
import struct, math, random
random.seed(42)
with open('$DIR/testdata.bin', 'wb') as f:
    for i in range(10000):
        x = i * 0.1
        v = (0.32 * math.sin(1.3 * x + 0.5) +
             0.32 * math.sin(2.7 * x + 1.2) +
             0.32 * math.sin(5.1 * x + 2.8) +
             0.04 * (random.random() * 2 - 1))
        f.write(struct.pack('<f', v))
"
echo "  生成完成"
echo

# ---------- helper: interpret hex qoi as signed int32 ----------
qoi_dec() { python3 -c "v=int('$1',16); print(v if v<0x80000000 else v-0x100000000)"; }

# ---------- helper: run sz3 compress + decompress ----------
do_roundtrip() {
    local label="$1" ini="$2"
    echo "--- $label ---"
    $SZ3 -f -i "$DIR/testdata.bin" -z "$DIR/out.sz" -1 10000 -c "$ini" 2>/dev/null
    $SZ3 -f -z "$DIR/out.sz" -o "$DIR/dec.bin" -1 10000 2>/dev/null
    echo "  压缩/解压完成"
}

# ---------- clean ----------
cleanup() { rm -f "$DIR"/{out.sz,dec.bin,my.ini} 2>/dev/null; }

echo "=========================================="
echo "1) Nibble — SumQoI(sqr+cubic), qEB=0.01"
echo "=========================================="
cleanup
QOI_HEX=$($ENCODER "sqr+cubic" 2>/dev/null | sed -n 's/.*qoi.*= 0x//p')
QOI=$(qoi_dec "$QOI_HEX")
cat > "$DIR/my.ini" <<EOF
[QoISettings]
qoi = $QOI
qoiEB = 0.01
qoiQuantbinCnt = 65536
EOF
do_roundtrip "nibble" "$DIR/my.ini"
python3 -c "
import struct
with open('$DIR/testdata.bin','rb') as f: orig = [x[0] for x in struct.iter_unpack('<f', f.read())]
with open('$DIR/dec.bin','rb') as f: dec = [x[0] for x in struct.iter_unpack('<f', f.read())]
errs = [abs(o*o + o*o*o - d*d - d*d*d) for o,d in zip(orig,dec)]
maxe = max(errs)
print(f'  max |f(o)-f(d)| = {maxe:.6g}  (qEB=0.01)  {\"PASS\" if maxe <= 0.010001 else \"FAIL\"}')"

echo
echo "=========================================="
echo "2) Regional — SumQoI(sqr+cubic) 聚合均值"
echo "=========================================="
cleanup
QOI_HEX=$($ENCODER --regional "sqr+cubic" 2>/dev/null | sed -n 's/.*qoi.*= 0x//p')
QOI=$(qoi_dec "$QOI_HEX")
cat > "$DIR/my.ini" <<EOF
[QoISettings]
qoi = $QOI
qoiEB = 1e-6
qoiQuantbinCnt = 65536
EOF
do_roundtrip "regional" "$DIR/my.ini"
python3 -c "
import struct
with open('$DIR/testdata.bin','rb') as f: orig = [x[0] for x in struct.iter_unpack('<f', f.read())]
with open('$DIR/dec.bin','rb') as f: dec = [x[0] for x in struct.iter_unpack('<f', f.read())]
agg = abs(sum(o*o + o*o*o - d*d - d*d*d for o,d in zip(orig,dec))) / len(orig)
print(f'  |mean(f(o)-f(d))| = {agg:.6g}  (qEB=1e-6)  {\"PASS\" if agg <= 1.0001e-6 else \"FAIL\"}')"

echo
echo "=========================================="
echo "3) Isoline — sqr, isoline(meb=0.01)"
echo "=========================================="
cleanup
QOI_HEX=$($ENCODER "iso6(sqr, -5, 5, 3, 0.01)" 2>/dev/null | sed -n 's/.*qoi.*= 0x//p')
QOI=$(qoi_dec "$QOI_HEX")
PARAM=$($ENCODER "iso6(sqr, -5, 5, 3, 0.01)" 2>/dev/null | sed -n 's/.*qoiParams.*= \"\(.*\)\"/\1/p')
cat > "$DIR/my.ini" <<EOF
[QoISettings]
qoi = $QOI
qoiEB = 1.0
qoiParams = $PARAM
qoiQuantbinCnt = 65536
EOF
do_roundtrip "isoline" "$DIR/my.ini"
python3 -c "
import struct
with open('$DIR/testdata.bin','rb') as f: orig = [x[0] for x in struct.iter_unpack('<f', f.read())]
with open('$DIR/dec.bin','rb') as f: dec = [x[0] for x in struct.iter_unpack('<f', f.read())]
min_v, max_v, n_iso, meb = -5, 5, 3, 0.01
iso_step = (max_v - min_v) / n_iso
thresh = [min_v + i*iso_step for i in range(n_iso+1)]
bad = 0
for o,d in zip(orig,dec):
    so, sd = o*o, d*d
    for t in thresh:
        if (so - t) * (sd - t) < 0:
            bad += 1; break
print(f'  isoline crossings: {bad}/{len(orig)}  (0 expected)  {\"PASS\" if bad == 0 else \"FAIL\"}')"

echo
echo "=========================================="
echo "4) Conv — Laplacian [1,-2,1], tol=0.005"
echo "=========================================="
cleanup
QOI_HEX=$($ENCODER "conv(1,-2,1,0.005)" 2>/dev/null | sed -n 's/.*qoi.*= 0x//p')
QOI=$(qoi_dec "$QOI_HEX")
PARAM=$($ENCODER "conv(1,-2,1,0.005)" 2>/dev/null | sed -n 's/.*qoiParams.*= \"\(.*\)\"/\1/p')
cat > "$DIR/my.ini" <<EOF
[QoISettings]
qoi = $QOI
qoiEB = 1.0
qoiParams = $PARAM
qoiQuantbinCnt = 65536
EOF
do_roundtrip "conv" "$DIR/my.ini"
python3 -c "
import struct
with open('$DIR/testdata.bin','rb') as f: orig = [x[0] for x in struct.iter_unpack('<f', f.read())]
with open('$DIR/dec.bin','rb') as f: dec = [x[0] for x in struct.iter_unpack('<f', f.read())]
kernel = [1, -2, 1]; tol = 0.005; c = 1
max_err = 0
for i in range(c, len(orig)-c):
    co = sum(kernel[j] * orig[i-c+j] for j in range(3))
    cd = sum(kernel[j] * dec[i-c+j] for j in range(3))
    max_err = max(max_err, abs(co-cd))
print(f'  max conv err = {max_err:.6g}  (tol=0.005)  {\"PASS\" if max_err <= 0.005001 else \"FAIL\"}')"

echo
cleanup
rm -f "$DIR/testdata.bin"
echo "=== 全部完成 ==="
