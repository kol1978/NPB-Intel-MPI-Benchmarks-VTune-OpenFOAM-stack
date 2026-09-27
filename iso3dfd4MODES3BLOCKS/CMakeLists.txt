# ===========================================================================
# iso3dfd — 3D изотропный конечноразностный решатель
# Сборка: icpx + Intel MPI + OpenMP (без offload)
#
#   source /opt/intel/oneapi/setvars.sh --force
#
#   1) Расчётная сборка (Release, без верификации):
#      cmake -S . -B build -DVERIFY_RESULTS=0 -DTARGET_ARCH=Westmere
#      cmake --build build -j$(nproc)
#
#   2) Профилировочная сборка (VTune/APS):
#      cmake -S . -B build-vtune -DVERIFY_RESULTS=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo
#      cmake --build build-vtune -j$(nproc)
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
    VERSION 1.0
    LANGUAGES CXX
    DESCRIPTION "3D isotropic finite-difference solver with two-level cache blocking"
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(VERIFY_RESULTS "Run result verification" OFF)
set(TARGET_ARCH "Generic" CACHE STRING "Target architecture: Westmere / ICX / Generic")

# ── Архитектурные флаги ───────────────────────────────────────────────────
if(TARGET_ARCH STREQUAL "Westmere")
    set(ARCH_FLAGS "-xSSE4.2")
elseif(TARGET_ARCH STREQUAL "ICX")
    set(ARCH_FLAGS "-xCORE-AVX2")
else()
    set(ARCH_FLAGS "")
endif()

# ── Флаги компиляции по типам сборки ──────────────────────────────────────
# Базовые флаги (применяются ко всем типам сборки)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp")

# Release — максимальная скорость
set(CMAKE_CXX_FLAGS_RELEASE "-O3 ${ARCH_FLAGS}")

# RelWithDebInfo — для VTune/APS (оптимизация + отладочная информация)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -gline-tables-only -fdebug-info-for-profiling")

# Debug — полный отладочный режим
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Intel MPI
find_package(MPI REQUIRED)

# OpenMP
find_package(OpenMP REQUIRED)

# Бинарник
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
message(STATUS "  iso3dfd ${PROJECT_VERSION}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  CXX_FLAGS:       ${CMAKE_CXX_FLAGS}")
message(STATUS "  TARGET_ARCH:     ${TARGET_ARCH}")
message(STATUS "  MPI:             ${MPI_CXX_COMPILER}")
message(STATUS "  OpenMP:          ${OpenMP_CXX_FLAGS}")
message(STATUS "  VERIFY_RESULTS:  ${VERIFY_RESULTS}")
message(STATUS "────────────────────────────────────────")
