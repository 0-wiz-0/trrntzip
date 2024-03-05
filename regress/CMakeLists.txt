set(XFAIL_TESTS
  sort-dupes.test
)

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

configure_file(nihtest.conf.in nihtest.conf @ONLY)
