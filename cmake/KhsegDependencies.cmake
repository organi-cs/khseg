include(FetchContent)

if(KHSEG_BUILD_TOOLS)
  FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
    SYSTEM)
  set(CLI11_PRECOMPILED OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(cli11)
endif()

if(KHSEG_BUILD_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
    SYSTEM)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if(KHSEG_BUILD_PYTHON)
  find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)
  FetchContent_Declare(pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
    GIT_SHALLOW TRUE
    SYSTEM)
  set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(pybind11)
endif()
