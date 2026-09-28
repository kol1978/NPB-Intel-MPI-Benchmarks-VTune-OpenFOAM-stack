#ifndef ISO3DFD_SOLVER_HPP
#define ISO3DFD_SOLVER_HPP

// =======================================================================
// Вычисление одной итерации: prev -> next
//
// use_omp = false → последовательная версия (baseline, без оптимизаций)
// use_omp = true  → OpenMP v2 с тремя оптимизациями:
//
//   1. X-blocking (b1) — ограничение X-диапазона для размещения 2D LC в L2
//      Раньше b1 не использовался, X всегда был полный (240) → 2D LC = 1 МБ
//      Теперь b1=64 → 2D LC = 17 × 64 × b2_l2 × 4 ≤ 256 КБ
//
//   2. L2 micro-block (b2_l2) — 2D-срез помещается в L2 (private, 256 КБ)
//      Вычисляется: b2_l2 = L2_SIZE × 0.8 / ((2×h+1) × b1 × 4)
//      Для b1=64: b2_l2 = 48 (2D LC = 204 КБ < 256 КБ, запас 20%)
//      Переопределение через env: ISO3DFD_B2_L2=48
//
//   3. Z-streaming — регистровые буферы front[9]/back[8] типа __m128
//      На каждом шаге по Z: 1 новый read из памяти вместо 16
//      Сокращение DRAM-трафика: ~200 → ~136 байт/точка
//      AI: 0.42 → ~0.46
//
//   4. 4×1 vector folding — SSE4.2 интринсики, обработка по 4 float по X
//      front/back — массивы __m128 (4 float = 128 бит)
//      X-соседи: сдвинутые loadu (contiguous, автопрефетч)
//      Y-соседи: loadu из соседних Y-строк (strided)
//      Z-соседи: из регистрового кольцевого буфера
//
// Структура циклов v2:
//   L3-блок (bz, by, bx)  → L2-подблок (by2) → omp for (y, bx) →
//   X-chunks (×4, SIMD)  → Z-stream (front/back shift)
//
// Параметры через env:
//   ISO3DFD_B2_L2    — размер L2 подблока по Y (по умолчанию: авто)
//   ISO3DFD_TB_STEPS — temporal blocking шаги (по умолчанию: 0, заготовка)
//
// Архитектура кэша X5675 (Westmere-EP):
//   L1d: 32 KB, 8-way, private per core
//   L2:  256 KB, 8-way, private per core, non-inclusive
//   L3:  12 MB, 16-way, shared per socket (6 cores), inclusive
//
// Для temporal blocking (этап 2): L3 budget = 12 MB / 6 = 2 MB/поток
//   При N=8: рабочий набор = 393 KB/поток (22% от бюджета) → безопасно
// =======================================================================

// =======================================================================
// Temporal blocking (loop tiling — temporal): wave-front в L3
//
// Выполняет tb_steps временных шагов за один проход по Z.
// На каждом шаге interior сужается на kHalfLength с каждой стороны
// (волновой фронт движется снаружи внутрь по Z).
//
// Границы interior:
//   Шаг 0: z ∈ [halo, nz_total - halo)
//   Шаг s: z ∈ [halo + s*h, nz_total - halo - s*h)
//
// После tb_steps шагов результат в одном из буферов (prev или next,
// в зависимости от чётности tb_steps). Driver делает swap после вызова.
//
// Ограничение: b3 >= 8*tb_steps + 2*kHalfLength + 1
//
// Halo: kHalfLength * (tb_steps + 1) слоёв с каждой стороны
//
// Параметры через env:
//   ISO3DFD_TB_STEPS — temporal blocking шаги (по умолчанию: 0, отключено)
// =======================================================================

void compute_iteration(const float* __restrict__ prev,
                       float*       __restrict__ next,
                       const float* __restrict__ vel,
                       int n1, int n2, int n3,
                       int b1, int b2, int b3,
                       const float* coeff,
                       bool use_omp);

void compute_temporal_block(const float* __restrict__ prev,
                             float*       __restrict__ next,
                             const float* __restrict__ vel,
                             int n1, int n2, int nz_total, int nz_local,
                             int b1, int b2, int b3,
                             int halo, int tb_steps,
                             const float* coeff,
                             bool use_omp);

#endif // ISO3DFD_SOLVER_HPP
