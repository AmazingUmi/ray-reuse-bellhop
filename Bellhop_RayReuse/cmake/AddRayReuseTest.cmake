function(add_rayreuse_test target_name test_name source_file)
  add_executable("${target_name}" "${source_file}")
  target_include_directories(
    "${target_name}"
    PRIVATE
      "${PROJECT_SOURCE_DIR}/tests"
  )
  target_link_libraries(
    "${target_name}"
    PRIVATE
      bellhop_rayreuse_core
      bellhop_rayreuse_project_options
  )
  set_target_properties(
    "${target_name}"
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )

  add_test(NAME "${test_name}" COMMAND "${target_name}")
  if(ARGN)
    set_tests_properties("${test_name}" PROPERTIES LABELS "${ARGN}")
  endif()
endfunction()
