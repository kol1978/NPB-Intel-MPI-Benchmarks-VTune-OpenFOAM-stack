#ifndef ISO3DFD_H
#define ISO3DFD_H

// Порядок стенсиля: 16-й (радиус = 8 точек)
#define kHalfLength 8

// Параметры по умолчанию
#define DEFAULT_DXYZ 50.0f
#define DEFAULT_DT   0.002f

// =======================================================================
// Оптимизации v2: Z-streaming + L2 micro-block + SSE4.2 (4x1 folding)
// =======================================================================

// SIMD ширина для SSE4.2 (128 бит = 4 float)
#define SIMD_WIDTH 4

// L2 cache: 256 KB на Westmere (private per core)
#define L2_CACHE_BYTES (256 * 1024)

// Резерв для L2: используем 80% под рабочий набор, 20% — overhead
#define L2_RESERVE_FACTOR 0.8f

// Temporal blocking: число шагов в L3 (0 = отключено)
// При N=8: рабочий набор = (17+7) * 64 * 64 * 4 = 393 KB << 12 MB L3
// Управляется через env ISO3DFD_TB_STEPS
#define TB_STEPS_DEFAULT 0

#endif // ISO3DFD_H
