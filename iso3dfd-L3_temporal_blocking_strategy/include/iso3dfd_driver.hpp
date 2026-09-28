#ifndef ISO3DFD_DRIVER_HPP
#define ISO3DFD_DRIVER_HPP

// Главный цикл решателя с MPI-декомпозицией по Z
//
// prev, next, vel — локальные массивы ранга (с halo)
// n1, n2, nz_total — размеры локального массива (nz_total = nz_local + 2*halo)
// nz_local — расчётная зона (без halo)
// b1, b2, b3 — размеры блока cache-blocking (spatial tiling)
// niter — число итераций
// rank, nprocs — MPI-параметры
// coeff — коэффициенты стенсиля
// use_omp — флаг OpenMP
// tb_steps — temporal blocking: N шагов в L3 wave-front (0 = отключено)
// halo — размер halo по Z (kHalfLength без TB, kHalfLength*(N+1) с TB)
//
void run_driver(float* prev, float* next, float* vel,
                int n1, int n2, int nz_total, int nz_local,
                int b1, int b2, int b3,
                int niter, int rank, int nprocs,
                const float* coeff, bool use_omp,
                int tb_steps, int halo);

#endif // ISO3DFD_DRIVER_HPP
