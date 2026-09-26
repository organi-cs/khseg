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
