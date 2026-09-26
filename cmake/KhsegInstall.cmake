include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS khseg
  EXPORT khsegTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/khseg DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_BINARY_DIR}/generated/include/khseg
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT khsegTargets
  NAMESPACE khseg::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/khseg)

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/khsegConfig.cmake.in
  ${PROJECT_BINARY_DIR}/khsegConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/khseg)
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/khsegConfigVersion.cmake
  COMPATIBILITY SameMinorVersion)
install(FILES
  ${PROJECT_BINARY_DIR}/khsegConfig.cmake
  ${PROJECT_BINARY_DIR}/khsegConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/khseg)
