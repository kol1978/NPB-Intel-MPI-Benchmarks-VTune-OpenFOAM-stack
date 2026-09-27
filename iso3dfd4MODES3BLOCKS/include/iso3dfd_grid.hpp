#ifndef ISO3DFD_GRID_HPP
#define ISO3DFD_GRID_HPP

// Инициализация коэффициентов стенсиля 16-го порядка
void InitializeCoefficients(float* coeff, float dxyz);

// Выделение выровненной памяти (64 байта для cache-line)
float* Allocate(int size);

// Освобождение
void Free(float* ptr);

// Инициализация сетки: нули + импульс в центре
void Initialize(float* prev, float* next, float* vel,
                 int n1, int n2, int n3, int half_length);

// Вывод параметров
void PrintStats(int n1, int n2, int n3,
                int b1, int b2, int b3, int niter);

// Вывод результатов и производительности
void PrintSummary(int n1, int n2, int n3,
                  int niter, float time_s);

#endif // ISO3DFD_GRID_HPP
