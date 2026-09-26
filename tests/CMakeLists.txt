include(GoogleTest)

add_executable(khseg-tests
  unit/test_version.cpp)
target_link_libraries(khseg-tests PRIVATE khseg::khseg GTest::gtest_main)
khseg_configure_target(khseg-tests)
gtest_discover_tests(khseg-tests DISCOVERY_MODE PRE_TEST)
