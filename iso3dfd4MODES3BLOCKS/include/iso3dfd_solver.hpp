#ifndef ISO3DFD_SOLVER_HPP
#define ISO3DFD_SOLVER_HPP

// Вычисление одной итерации: prev -> next
// use_omp = false → последовательная версия
// use_omp = true  → OpenMP с cache-blocking (b1,b2,b3)
void compute_iteration(const float* __restrict__ prev,
                       float*       __restrict__ next,
                       const float* __restrict__ vel,
                       int n1, int n2, int n3,
                       int b1, int b2, int b3,
                       const float* coeff,
                       bool use_omp);

#endif // ISO3DFD_SOLVER_HPP
