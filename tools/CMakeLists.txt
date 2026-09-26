add_executable(khseg-cli khseg/main.cpp)
set_target_properties(khseg-cli PROPERTIES OUTPUT_NAME khseg)
target_link_libraries(khseg-cli PRIVATE khseg::khseg CLI11::CLI11)
khseg_configure_target(khseg-cli)

install(TARGETS khseg-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(khseg-eval khseg-eval/main.cpp)
target_link_libraries(khseg-eval PRIVATE khseg::khseg CLI11::CLI11)
khseg_configure_target(khseg-eval)
install(TARGETS khseg-eval RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(khseg-dict khseg-dict/main.cpp)
target_link_libraries(khseg-dict PRIVATE khseg::khseg CLI11::CLI11)
khseg_configure_target(khseg-dict)
install(TARGETS khseg-dict RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(khseg-bench khseg-bench/main.cpp)
target_link_libraries(khseg-bench PRIVATE khseg::khseg CLI11::CLI11)
if(WIN32)
  target_link_libraries(khseg-bench PRIVATE psapi)
endif()
khseg_configure_target(khseg-bench)

# ICU baseline. Point ICU_ROOT at an ICU install, e.g. the unzipped
# icu4c-*-Win64-MSVC2022.zip from the ICU release page.
if(KHSEG_BENCH_ICU)
  find_package(ICU REQUIRED COMPONENTS uc data)
  add_executable(khseg-icu khseg-icu/main.cpp)
  target_link_libraries(khseg-icu PRIVATE khseg::khseg CLI11::CLI11 ICU::uc ICU::data)
  khseg_configure_target(khseg-icu)
  if(WIN32)
    # Copy the ICU DLLs next to the tool so it runs without PATH changes.
    get_filename_component(_icu_lib_dir "${ICU_UC_LIBRARY}" DIRECTORY)
    file(GLOB _icu_dlls "${_icu_lib_dir}/../bin64/icu*.dll" "${_icu_lib_dir}/../bin/icu*.dll")
    foreach(_dll IN LISTS _icu_dlls)
      add_custom_command(TARGET khseg-icu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dll} $<TARGET_FILE_DIR:khseg-icu>)
    endforeach()
  endif()
endif()
