set(XFAIL_TESTS
)
if(WIN32)
  set(XFAIL_TESTS ${XFAIL_TESTS}
    cli-q.test
    cli-s.test
    directories-2-d.test
    recurse.test
    recursive.test
  )
endif()

if(RUN_REGRESS)
  file(GLOB TEST_CASES ${CMAKE_CURRENT_SOURCE_DIR}/*.test)
  foreach(FULL_CASE IN LISTS TEST_CASES)
    get_filename_component(CASE ${FULL_CASE} NAME)
    add_test(NAME ${CASE} COMMAND ${NIHTEST} -v ${CASE})
    set_tests_properties(${CASE} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()

  foreach(CASE ${XFAIL_TESTS})
    set_tests_properties(${CASE} PROPERTIES WILL_FAIL TRUE)
  endforeach()
endif()

set(TEST_BINARY_PATH ${PROJECT_BINARY_DIR}/src)
foreach (cfg ${CMAKE_CONFIGURATION_TYPES})
  set(TEST_BINARY_PATH "${TEST_BINARY_PATH}\n\t${PROJECT_BINARY_DIR}/src/${cfg}")
endforeach()
configure_file(nihtest.conf.in nihtest.conf @ONLY)
