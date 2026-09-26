cmake_minimum_required(VERSION 3.5)
project(iso3dfd)

OPTION(VERIFY_RESULTS "Use Results Validation" ON)
OPTION(USE_OPT1 "Select Optimized target code - version 1" OFF)
OPTION(USE_OPT2 "Select Optimized target code - version 2" OFF)
OPTION(USE_OPT3 "Select Optimized target code - version 3" OFF)
OPTION(NO_OFFLOAD "Run on CPU host only" OFF)
OPTION(ENABLE_MPI "Enable MPI domain decomposition" OFF)
OPTION(ENABLE_PROFILING "Add debug/profiling flags for VTune" OFF)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")

if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()

if(ENABLE_MPI)
    find_package(MPI REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_MPI")
    message("-- MPI domain decomposition enabled")
endif()

if(ENABLE_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fdebug-info-for-profiling")
    message("-- Profiling flags enabled")
endif()

set(SOURCES iso3dfd.cpp utils.cpp)

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

if(VERIFY_RESULTS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVERIFY_RESULTS")
	set(SOURCES ${SOURCES} iso3dfd_verify.cpp)
endif(VERIFY_RESULTS)


add_executable (iso3dfd ${SOURCES})

if(ENABLE_MPI)
    target_link_libraries(iso3dfd PRIVATE MPI::MPI_CXX)
endif()

add_custom_target (run 
	COMMAND iso3dfd 256 256 256 16 8 64 100
	WORKING_DIRECTORY ${CMAKE_PROJECT_DIR}
)
