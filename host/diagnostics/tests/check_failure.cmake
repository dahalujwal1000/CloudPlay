execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${MOCK_DIR}" "LD_PRELOAD=" "${PROBE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  TIMEOUT 10)
if(NOT result STREQUAL "1")
  message(FATAL_ERROR "Expected probe failure exit 1, got ${result}: ${output}${error}")
endif()
string(FIND "${error}" "\"reason\":\"${EXPECT_REASON}\"" reason_index)
if(reason_index EQUAL -1)
  message(FATAL_ERROR "Expected ${EXPECT_REASON}: ${output}${error}")
endif()
