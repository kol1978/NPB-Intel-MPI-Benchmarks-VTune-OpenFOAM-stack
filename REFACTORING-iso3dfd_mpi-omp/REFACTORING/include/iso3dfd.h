//==============================================================
// Copyright © 2020 Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#ifndef ISO3DFD_H
#define ISO3DFD_H

#include <omp.h>
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <string>

// ── Константы ──
constexpr size_t kHalfLength = 8;       // Половина ширины стенсила (16-й порядок)
constexpr float dt = 0.001f;             // Шаг по времени
constexpr float dxyz = 50.0f;           // Шаг сетки
constexpr size_t kMaxTeamSizeLimit = 1024;

// ── Макросы стенсила (CPU + BASELINE) ──
#define STENCIL_LOOKUP(n) \
  value += ptr_prev[ix + n] * coeff[n] - ptr_prev[ix - n] * coeff[n]; \
  value += ptr_prev[ix + n * n1] * coeff[n] - ptr_prev[ix - n * n1] * coeff[n]; \
  value += ptr_prev[ix + n * dimn1n2] * coeff[n] - ptr_prev[ix - n * dimn1n2] * coeff[n]

// ── Макросы стенсила для OPT2/OPT3 (с front/back-регистрами) ──
#define STENCIL_LOOKUP_Z(n) \
  value += front[n] * coeff[n] - back[n - 1] * coeff[n]; \
  value += ptr_prev_base[gid + n] * coeff[n] - ptr_prev_base[gid - n] * coeff[n]; \
  value += ptr_prev_base[gid + n * n1] * coeff[n] - ptr_prev_base[gid - n * n1] * coeff[n]

// ── Объявления функций (utils.cpp) ──
void Usage(const std::string& programName);
void Initialize(float* ptr_prev, float* ptr_next, float* ptr_vel,
                size_t n1, size_t n2, size_t n3);
void PrintStats(double time, size_t n1, size_t n2, size_t n3,
                size_t num_iterations);
bool WithinEpsilon(float* output, float* reference, size_t dim_x,
                   size_t dim_y, size_t dim_z, size_t radius,
                   const int zadjust = 0, const float delta = 0.01f);
bool ValidateInput(size_t n1, size_t n2, size_t n3,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block, size_t num_iterations);

#ifdef USE_MPI
void InitializeLocal(float *ptr_prev, float *ptr_next, float *ptr_vel,
                     size_t n1, size_t n2, size_t n3_alloc,
                     size_t z_start_global, size_t n3_local,
                     int rank, int nprocs);
#endif

// ── Верификация (iso3dfd_verify.cpp) ──
#ifdef VERIFY_RESULTS
#ifndef USE_MPI
bool VerifyResults(float *next_base, float *prev_base, float *vel_base,
                   float *coeff, size_t n1, size_t n2,
                   size_t n3, size_t num_iterations,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block);
#else
bool VerifyResults(float *next_base, float *prev_base, float *vel_base,
                   float *coeff, size_t n1, size_t n2,
                   size_t n3, size_t num_iterations,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block,
                   int rank, int nprocs,
                   size_t n3_local, size_t z_start_global,
                   size_t n3_user);
#endif
#endif

#endif // ISO3DFD_H
