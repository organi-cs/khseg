include(GoogleTest)

add_executable(khseg-tests
  unit/test_version.cpp
  unit/test_utf8.cpp
  unit/test_charclass.cpp
  unit/test_cluster.cpp)
target_link_libraries(khseg-tests PRIVATE khseg::khseg GTest::gtest_main)
target_compile_definitions(khseg-tests PRIVATE
  KHSEG_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
khseg_configure_target(khseg-tests)
gtest_discover_tests(khseg-tests DISCOVERY_MODE PRE_TEST)
