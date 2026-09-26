include(GenerateExportHeader)

add_library(khseg
  cluster.cpp
  utf8.cpp
  version.cpp)
add_library(khseg::khseg ALIAS khseg)

set(_gen ${PROJECT_BINARY_DIR}/generated/include)
configure_file(${PROJECT_SOURCE_DIR}/include/khseg/version.hpp.in ${_gen}/khseg/version.hpp @ONLY)
generate_export_header(khseg
  BASE_NAME KHSEG
  EXPORT_FILE_NAME ${_gen}/khseg/export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(khseg PUBLIC KHSEG_STATIC_DEFINE)
endif()

target_include_directories(khseg PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${_gen}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(khseg PUBLIC cxx_std_20)
set_target_properties(khseg PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
khseg_configure_target(khseg)
