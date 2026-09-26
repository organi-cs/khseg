# Runs a khseg tool on an input file and compares stdout with an expected file.
# Variables: EXE, ARGS (list), INPUT, EXPECTED, ACTUAL, [EXPECT_RC]
string(REPLACE "^^" ";" ARGS "${ARGS}")
if(NOT DEFINED EXPECT_RC)
  set(EXPECT_RC 0)
endif()
execute_process(
  COMMAND ${EXE} ${ARGS}
  INPUT_FILE ${INPUT}
  OUTPUT_FILE ${ACTUAL}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "exit code ${rc}, expected ${EXPECT_RC}")
endif()
if(DEFINED EXPECTED)
  execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${EXPECTED} ${ACTUAL} RESULT_VARIABLE diff)
  if(NOT diff EQUAL 0)
    file(READ ${ACTUAL} got)
    message(FATAL_ERROR "output differs from ${EXPECTED}:\n${got}")
  endif()
endif()
