cmake_minimum_required(VERSION 3.24)

project(khseg
  VERSION 0.1.0
  DESCRIPTION "Khmer word segmenter"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KHSEG_BUILD_TESTS "Build unit and CLI tests" ${PROJECT_IS_TOP_LEVEL})
option(KHSEG_BUILD_TOOLS "Build khseg, khseg-eval, khseg-bench, khseg-dict" ${PROJECT_IS_TOP_LEVEL})
option(KHSEG_BUILD_PYTHON "Build the pybind11 module" OFF)
option(KHSEG_BENCH_ICU "Compare against ICU BreakIterator in khseg-bench" OFF)
option(KHSEG_BUILD_FUZZ "Build libFuzzer targets (clang only)" OFF)
option(KHSEG_WERROR "Treat warnings as errors" OFF)
set(KHSEG_SANITIZE "" CACHE STRING "Semicolon list of sanitizers, e.g. address;undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(KhsegWarnings)
include(KhsegDependencies)

add_subdirectory(src)

if(KHSEG_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(KHSEG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(KHSEG_BUILD_PYTHON)
  add_subdirectory(python)
endif()

if(NOT SKBUILD)
  include(KhsegInstall)
endif()
