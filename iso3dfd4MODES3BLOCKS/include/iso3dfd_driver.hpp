#ifndef ISO3DFD_DRIVER_HPP
#define ISO3DFD_DRIVER_HPP

// Главный цикл решателя с MPI-декомпозицией по Z
//
// prev, next, vel — локальные массивы ранга (с halo)
// n1, n2, nz_total — размеры локального массива (nz_total = nz_local + 2*kHalfLength)
// nz_local — расчётная зона (без halo)
// niter — число итераций
// rank, nprocs — MPI-параметры
// use_omp — флаг OpenMP
//
void run_driver(float* prev, float* next, float* vel,
                int n1, int n2, int nz_total, int nz_local,
                int b1, int b2, int b3,
                int niter, int rank, int nprocs,
                const float* coeff, bool use_omp);

#endif // ISO3DFD_DRIVER_HPP
