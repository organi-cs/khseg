include(GoogleTest)

add_executable(khseg-tests
  unit/test_version.cpp
  unit/test_utf8.cpp
  unit/test_charclass.cpp
  unit/test_cluster.cpp
  unit/test_dat.cpp
  unit/test_dictionary.cpp
  unit/test_eval.cpp
  unit/test_format.cpp
  unit/test_invariants.cpp
  unit/test_normalize.cpp
  unit/test_pretokenize.cpp
  unit/test_segmenter.cpp
  unit/test_viterbi.cpp)
target_link_libraries(khseg-tests PRIVATE khseg::khseg GTest::gtest_main)
target_compile_definitions(khseg-tests PRIVATE
  KHSEG_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  KHSEG_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
khseg_configure_target(khseg-tests)
gtest_discover_tests(khseg-tests DISCOVERY_MODE PRE_TEST)

# Golden-file tests for the command line tools.
function(khseg_golden name)
  cmake_parse_arguments(G "" "INPUT;EXPECTED;RC;TOOL" "ARGS" ${ARGN})
  if(NOT G_TOOL)
    set(G_TOOL khseg-cli)
  endif()
  # A list would be split into separate cmake arguments, so join it with ^^.
  list(JOIN G_ARGS "^^" joined_args)
  set(defs -DEXE=$<TARGET_FILE:${G_TOOL}> "-DARGS=${joined_args}"
    -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/cli/${G_INPUT}
    -DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/cli-out/${name}.out)
  if(G_EXPECTED)
    list(APPEND defs -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/cli/${G_EXPECTED})
  endif()
  if(DEFINED G_RC)
    list(APPEND defs -DEXPECT_RC=${G_RC})
  endif()
  add_test(NAME cli.${name} COMMAND ${CMAKE_COMMAND} ${defs} -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/golden.cmake)
endfunction()

if(KHSEG_BUILD_TOOLS)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/cli-out)
  khseg_golden(mixed_space INPUT mixed.txt EXPECTED mixed.nodict.space.txt)
  khseg_golden(mixed_sep INPUT mixed.txt EXPECTED mixed.nodict.sep.txt ARGS --sep |)
  khseg_golden(mixed_zwsp INPUT mixed.txt EXPECTED mixed.txt ARGS --zwsp)
  khseg_golden(mixed_json INPUT mixed.txt EXPECTED mixed.nodict.json.txt ARGS --json)
  khseg_golden(mixed_json_bytes INPUT mixed.txt EXPECTED mixed.nodict.jsonbytes.txt ARGS --json --offsets byte)
  khseg_golden(mixed_clusters INPUT mixed.txt EXPECTED mixed.nodict.clusters.txt ARGS -f clusters)
  khseg_golden(bad_utf8_replace INPUT bad_utf8.txt EXPECTED bad_utf8.replaced.txt)
  khseg_golden(bad_utf8_strict INPUT bad_utf8.txt RC 3 ARGS --strict-utf8)
  khseg_golden(bad_option INPUT mixed.txt RC 1 ARGS --no-such-option)
  khseg_golden(missing_dict INPUT mixed.txt RC 2 ARGS --dict no-such-dict.tsv)

  set(sample_dict ${PROJECT_SOURCE_DIR}/data/sample/dict.tsv)
  foreach(algo viterbi fmm bmm bimm)
    khseg_golden(sample_${algo} INPUT ../../data/sample/raw.txt EXPECTED ../../data/sample/gold.txt
      ARGS --dict ${sample_dict} --algo ${algo})
  endforeach()

  khseg_golden(sample_threads INPUT ../../data/sample/raw.txt EXPECTED ../../data/sample/gold.txt
    ARGS --dict ${sample_dict} -j 3)
  khseg_golden(mixed_json_threads INPUT mixed.txt EXPECTED mixed.nodict.json.txt ARGS --json -j 2)

  set(sample_gold ${PROJECT_SOURCE_DIR}/data/sample/gold.txt)
  khseg_golden(eval_sample TOOL khseg-eval INPUT mixed.txt EXPECTED eval_sample.txt
    ARGS --gold ${sample_gold} --dict ${sample_dict} --no-merge-unknown --bootstrap 200 --dump-errors 5)
  khseg_golden(eval_pred TOOL khseg-eval INPUT mixed.txt EXPECTED eval_pred.txt
    ARGS --gold ${sample_gold} --pred ${CMAKE_CURRENT_SOURCE_DIR}/cli/eval_pred_input.txt --bootstrap 0)
  khseg_golden(eval_sweep TOOL khseg-eval INPUT mixed.txt EXPECTED eval_sweep.txt
    ARGS --gold ${sample_gold} --dict ${sample_dict} --sweep unk-cost=10:14:2)
endif()
