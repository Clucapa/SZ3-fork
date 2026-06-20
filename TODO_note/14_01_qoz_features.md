# 1401: QoZ features not yet implemented in SZ3

## QoI classes

| QoZ class | SZ3 status | Description |
|-----------|-----------|-------------|
| `Isoline` (id=4) | **TODO 1402** | topology-preserving QoI, isovalue crossing prevention |
| `FX_P` (id=15) | not done | piecewise FX: two arbitrary f(x) strings, threshold dispatch |
| `FX_abs` (id=17) | not done | `｜f(x)｜` preservation using SymEngine |
| `FX_New` (id=21) | not done | updated FX with SymEngine simplify support |
| `RegionalFX` (id=16) | not done | block-level aggregate for arbitrary f(x), statistical budget distribution |
| `SquaredError` (not in QoIInfo) | not done | pointwise `sqrt(3·tol/N)` uniform EB from global MSE constraint |

## Predictors (not QOI-related)

| QoZ class | Description |
|-----------|-------------|
| `PolyRegressionPredictor` | polynomial regression 1D-4D with pre-computed coefficient matrices |
| `ZeroPredictor` | trivial predictor: always predict 0 |
| `MetaLorenzoPredictor` | SZ2-style raw pointer Lorenzo, 3D only, used by SZFastFrontend |
| `MetaRegressionPredictor` | SZ2-style regression, 3D only |

## Quantizers

| QoZ class | SZ3 status | Description |
|-----------|-----------|-------------|
| `VariableEBLinearQuantizer` | N/A by design | takes per-point eb param in quantize_and_overwrite(data,pred,eb); SZ3 uses EBProvider pattern instead |

## Frontend/Compressor architecture

| QoZ class | Description | SZ3 equivalent |
|-----------|-------------|---------------|
| `SZQoIFrontend` | dual-quantization (data + EB channels) | SZ3 uses EBProvider + single quantizer |
| `SZQoIInterpolationCompressor` | QoI-aware interp with dual quantizer | SZ3 uses InterpDecomp + QpetInterpDecomp |
| `SZFastFrontend` | SZ2-style 3D-only fast path | no SZ3 equivalent |

## Interpolation tuning

QoZ has extensive per-block interp tuning that SZ3 lacks:
- `INTERP_ALGO_QUAD` (quadratic interp)
- `Interp_Meta` struct (per-block algorithm/direction/spline type)
- Cubic spline type selection (natural vs not-a-knot)
- `adjInterp` / `regressiveInterp` / `multiDimInterp` / `mdCrossInterp`
- `freezeDimTest` / `dynamicDimCoeff`
- `blockwiseTuning` with targets RD/CR/SSIM/AC
- `levelwisePredictionSelection`

## Utilities

| QoZ feature | Description |
|-------------|-------------|
| `Metrics.hpp` | PSNR + SSIM + blockwise profiling (mean/var/range) |
| `Transform.hpp` | sigmoid/logit/tanh/arctanh transforms |
| `CoeffRegression.hpp` | Gauss elimination, matrix ops for PolyRegression |
| `QuantOptimization.hpp` | quantization interval estimation from histogram |
| `MetaDef.hpp` | SZ2-style constants for fast frontend |
| `QoIEncoder.hpp` | dual Huffman encoder for EB + data indices |
