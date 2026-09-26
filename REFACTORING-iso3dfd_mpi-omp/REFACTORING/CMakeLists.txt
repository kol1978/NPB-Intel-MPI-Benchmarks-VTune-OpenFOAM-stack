cmake_minimum_required(VERSION 3.10)
project(iso3dfd LANGUAGES C CXX)

# ── Компиляторы Intel ──
set(CMAKE_C_COMPILER "icx" CACHE STRING "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "icpx" CACHE STRING "C++ compiler" FORCE)

# ── Опции ──
if(ENABLE_MPI)
    set(VERIFY_RESULTS_DEFAULT OFF)
else()
    set(VERIFY_RESULTS_DEFAULT ON)
endif()
OPTION(VERIFY_RESULTS "Use Results Validation" ${VERIFY_RESULTS_DEFAULT})
OPTION(USE_OPT1 "Select Optimized target code - version 1" OFF)
OPTION(USE_OPT2 "Select Optimized target code - version 2" OFF)
OPTION(USE_OPT3 "Select Optimized target code - version 3" OFF)
OPTION(NO_OFFLOAD "Run on CPU only" OFF)
OPTION(ENABLE_MPI "Enable MPI domain decomposition" OFF)
OPTION(ENABLE_PROFILING "Add debug/profiling flags for VTune" OFF)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")

if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()

set(SOURCES src/iso3dfd.cpp src/utils.cpp)

if(USE_OPT3)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_OPT3")
	message("-- Using Optimized target code - version 3")
elseif(USE_OPT2)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_OPT2")
	message("-- Using Optimized target code - version 2")
elseif(USE_OPT1)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_OPT1")
	message("-- Using Optimized target code - version 1")
elseif(NO_OFFLOAD)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_OFFLOAD")
	message("-- Running on CPU only with no offloading")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_BASELINE")
	message("-- Using Baseline target code")
endif()

if(ENABLE_MPI)
    set(MPI_ROOT "/opt/intel/oneapi/mpi/latest")
    set(MPI_INCLUDE_DIR "${MPI_ROOT}/include")
    set(MPI_LIB_DIR "${MPI_ROOT}/lib")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_MPI -I${MPI_INCLUDE_DIR}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_MPI -I${MPI_INCLUDE_DIR}")

    message(STATUS "MPI domain decomposition enabled")
    message(STATUS "  MPI include: ${MPI_INCLUDE_DIR}")
    message(STATUS "  MPI lib:     ${MPI_LIB_DIR}/libmpi.so")
endif()

if(ENABLE_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling")
    message(STATUS "Profiling flags enabled for VTune")
endif()

if(VERIFY_RESULTS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVERIFY_RESULTS")
	set(SOURCES ${SOURCES} src/iso3dfd_verify.cpp)
endif(VERIFY_RESULTS)

# ── Сборка ──
add_executable(iso3dfd ${SOURCES})

target_include_directories(iso3dfd PRIVATE include)

if(ENABLE_MPI)
    target_link_libraries(iso3dfd PRIVATE ${MPI_LIB_DIR}/libmpi.so)
    set_target_properties(iso3dfd PROPERTIES
        BUILD_RPATH "${MPI_LIB_DIR}"
        INSTALL_RPATH "${MPI_LIB_DIR}"
    )
endif()

add_custom_target(run
	COMMAND iso3dfd 256 256 256 16 8 64 100
	WORKING_DIRECTORY ${CMAKE_PROJECT_DIR}
)
