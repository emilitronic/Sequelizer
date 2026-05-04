cmake_minimum_required(VERSION 3.23)
project(Sequelizer VERSION 0.1.0)

set(CMAKE_C_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)

# HDF5 dependency for Fast5 file I/O
if(APPLE)
    set(HDF5_ROOT "/opt/homebrew/opt/hdf5")
    if (HDF5_ROOT)
        include_directories("${HDF5_ROOT}/include")
        link_directories("${HDF5_ROOT}/lib")
        set(CMAKE_REQUIRED_INCLUDES ${HDF5_ROOT}/include)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HDF5 REQUIRED hdf5)
    include_directories(${HDF5_INCLUDE_DIRS})
    link_directories(${HDF5_LIBRARY_DIRS})
endif()

# argp-standalone dependency for command line parsing
if(APPLE)
    set(ARGP_ROOT "/opt/homebrew/opt/argp-standalone")
    if (ARGP_ROOT)
        include_directories("${ARGP_ROOT}/include")
        link_directories("${ARGP_ROOT}/lib")
        set(CMAKE_REQUIRED_INCLUDES ${ARGP_ROOT}/include)
    endif()
else()
    find_library(OPENBLAS_LIBRARY NAMES openblas REQUIRED)
endif()

# Source files
set(SEQUELIZER_SOURCES
    src/sequelizer_subcommands.c
    src/sequelizer_fast5.c
    src/sequelizer_convert.c
    src/sequelizer_plot.c
    src/sequelizer_seqgen.c
    src/sequelizer_bcall.c
    src/core/fast5_io.c
    src/core/fast5_utils.c
    src/core/fast5_stats.c
    src/core/fast5_convert.c
    src/core/plot_utils.c
    src/core/seqgen_utils.c
    src/core/seqgen_models.c
    src/core/seq_tensor.c
    src/core/seq_utils.c
    src/core/util.c
    src/core/kmer_model_loader.c
)

# Common include directories
set(SEQUELIZER_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

# Sequelizer static library
add_library(sequelizer_static STATIC ${SEQUELIZER_SOURCES})
target_include_directories(sequelizer_static PUBLIC ${SEQUELIZER_INCLUDE_DIRS})

# Sequelizer executable
add_executable(sequelizer src/sequelizer.c)
target_include_directories(sequelizer PRIVATE ${SEQUELIZER_INCLUDE_DIRS})
if(APPLE)
  target_link_libraries(sequelizer PRIVATE sequelizer_static m hdf5 argp)
else()
  target_link_libraries(sequelizer PRIVATE sequelizer_static m ${HDF5_LIBRARIES} ${OPENBLAS_LIBRARY})
endif()

# Test executables
add_executable(test_kmer_loader test/test_kmer_loader.c src/core/kmer_model_loader.c)
target_include_directories(test_kmer_loader PRIVATE ${SEQUELIZER_INCLUDE_DIRS})
if(APPLE)
  target_link_libraries(test_kmer_loader PRIVATE sequelizer_static m hdf5 argp)
else()
  target_link_libraries(test_kmer_loader PRIVATE sequelizer_static m ${HDF5_LIBRARIES} ${OPENBLAS_LIBRARY})
endif()

add_executable(test_seqgen test/test_seqgen.c)
target_include_directories(test_seqgen PRIVATE ${SEQUELIZER_INCLUDE_DIRS})
target_link_libraries(test_seqgen PRIVATE sequelizer_static m)

add_executable(test_seqgen_models test/test_seqgen_models.c)
target_include_directories(test_seqgen_models PRIVATE ${SEQUELIZER_INCLUDE_DIRS})
target_link_libraries(test_seqgen_models PRIVATE sequelizer_static m)

# Install
install(TARGETS sequelizer RUNTIME DESTINATION bin)
