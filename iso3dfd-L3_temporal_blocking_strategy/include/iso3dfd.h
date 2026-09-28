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

// =======================================================================
// Temporal blocking (loop tiling — temporal)
// =======================================================================

// Число шагов в L3 wave-front (0 = отключено).
// Управляется через env ISO3DFD_TB_STEPS (runtime, не compile-time).
//
// Ограничение: b3 >= 8*N + 2*kHalfLength + 1
//   N=0:  b3 >= 17  (фактически — обычный режим)
//   N=4:  b3 >= 49  (минимум для 4 шагов, b3=64 — комфортно)
//   N=8:  b3 >= 81  (минимум для 8 шагов, b3=128 — комфортно)
//
// Halo = kHalfLength * (N + 1) слоёв с каждой стороны:
//   N=0:  halo = 8
//   N=4:  halo = 40
//   N=8:  halo = 72
//
// Рабочий набор L3 (на поток):
//   (2*kHalfLength + N) * b1 * b2 * 4 байт
//   N=4, b1=64, b2=64: 20 * 64 * 64 * 4 = 320 КБ << 12 МБ L3 ✓
//   N=8, b1=64, b2=64: 24 * 64 * 64 * 4 = 384 КБ << 12 МБ L3 ✓
#define TB_STEPS_DEFAULT 0

#endif // ISO3DFD_H
