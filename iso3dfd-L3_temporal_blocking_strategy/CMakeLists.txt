# ===========================================================================
# iso3dfd — 3D изотропный конечноразностный решатель
# Версия: L3 temporal blocking (loop tiling: spatial + temporal + nested)
#
# Сборка: icpx + Intel MPI + OpenMP (без offload, без VTune)
#
#   1) source /opt/intel/oneapi/setvars.sh --force
#   2) cmake -S . -B build -DVERIFY_RESULTS=0 -DTARGET_ARCH=Westmere
#   3) cmake --build build -j$(nproc)
#
# Управление temporal blocking:
#   ISO3DFD_TB_STEPS=N — число временных шагов в волновом фронте (N=0: выкл.)
#   Ограничение: b3 >= 8*N + 64 (8 слоёв на шаг + 64 рабочий блок)
#
#   Пример: ISO3DFD_TB_STEPS=4 ./build/iso3dfd 256 256 256 64 64 128 2000 pure_omp
# ===========================================================================

# ВАЖНО: выбор компилятора ДО project()!
find_program(ICPX_BIN icpx)
if(ICPX_BIN)
    set(CMAKE_CXX_COMPILER ${ICPX_BIN})
else()
    message(FATAL_ERROR "icpx не найден. source /opt/intel/oneapi/setvars.sh --force")
endif()

cmake_minimum_required(VERSION 3.20)

project(iso3dfd
    VERSION 2.0
    LANGUAGES CXX
    DESCRIPTION "3D isotropic finite-difference solver — L3 temporal blocking"
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# По умолчанию Release — максимальная скорость
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(VERIFY_RESULTS "Run result verification" OFF)
set(TARGET_ARCH "Generic" CACHE STRING "Target architecture: Westmere / ICX / Generic")

# ── Архитектурные флаги ───────────────────────────────────────────────────
# Westmere (X5675): SSE4.2, 128-bit, 4 FP32/оп, нет FMA, нет AVX
# ICX (Ice Lake): AVX2, 256-bit, 8 FP32/оп, есть FMA
if(TARGET_ARCH STREQUAL "Westmere")
    set(ARCH_FLAGS "-xSSE4.2")
elseif(TARGET_ARCH STREQUAL "ICX")
    set(ARCH_FLAGS "-xCORE-AVX2")
else()
    set(ARCH_FLAGS "")
endif()

# ── Флаги компиляции ─────────────────────────────────────────────────────
# Базовые флаги (применяются ко всем типам сборки)
# -fiopenmp: OpenMP 5.1
# ${ARCH_FLAGS}: SSE4.2 или AVX2 в зависимости от TARGET_ARCH
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp ${ARCH_FLAGS}")

# Release — максимальная оптимизация
# -O3: агрессивная оптимизация
# -qopt-report=0: отключить отчёт векторизации (включить =5 для отладки)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -qopt-report=0")

# Debug — только для отладки temporal blocking (не для бенчмарков)
# -g: отладочные символы
# -O0: без оптимизации (для точной отладки wave-front)
# -qopt-report=5: полный отчёт векторизации
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -qopt-report=5")

# Intel MPI
find_package(MPI REQUIRED)

# OpenMP
find_package(OpenMP REQUIRED)

# ── Бинарник ─────────────────────────────────────────────────────────────
# Состав файлов не изменился — temporal blocking добавляется в существующие
# модули без создания новых .cpp файлов:
#   iso3dfd_solver.cpp  — compute_temporal_block() (~50 строк)
#   iso3dfd_driver.cpp  — расширенный halo 8→8N слоёв (~15 строк)
#   iso3dfd_grid.cpp   — увеличенный буфер halo (~10 строк)
#   iso3dfd.cpp         — парсинг ISO3DFD_TB_STEPS (~5 строк)
add_executable(iso3dfd
    src/iso3dfd.cpp
    src/iso3dfd_driver.cpp
    src/iso3dfd_solver.cpp
    src/iso3dfd_grid.cpp
)

if(VERIFY_RESULTS)
    target_sources(iso3dfd PRIVATE src/iso3dfd_verify.cpp)
    target_compile_definitions(iso3dfd PRIVATE VERIFY_RESULTS=1)
endif()

target_include_directories(iso3dfd
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iso3dfd
    PRIVATE
        MPI::MPI_CXX
        OpenMP::OpenMP_CXX
)

install(TARGETS iso3dfd
    RUNTIME DESTINATION bin
)

message(STATUS "────────────────────────────────────────")
message(STATUS "  iso3dfd ${PROJECT_VERSION} — L3 temporal blocking")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  CXX_FLAGS:       ${CMAKE_CXX_FLAGS}")
message(STATUS "  TARGET_ARCH:     ${TARGET_ARCH}")
message(STATUS "  MPI:             ${MPI_CXX_COMPILER}")
message(STATUS "  OpenMP:          ${OpenMP_CXX_FLAGS}")
message(STATUS "  VERIFY_RESULTS:  ${VERIFY_RESULTS}")
message(STATUS "  TB control:      ISO3DFD_TB_STEPS=N (env, default=0)")
message(STATUS "  TB constraint:   b3 >= 8*N + 64")
message(STATUS "────────────────────────────────────────")
