if(NOT DEFINED F2CPP_EXECUTABLE OR NOT DEFINED FIXTURE_DIR)
  message(FATAL_ERROR "CLI lifecycle test requires executable and fixtures")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(test_dir "$ENV{TMPDIR}/bellhop_f2cpp_cli_${suffix}")
if("$ENV{TMPDIR}" STREQUAL "")
  set(test_dir "/tmp/bellhop_f2cpp_cli_${suffix}")
endif()
file(MAKE_DIRECTORY "${test_dir}")
set(root "${test_dir}/case")

function(run_case fixture expected_product forbidden_product success_marker)
  file(COPY_FILE "${FIXTURE_DIR}/${fixture}" "${root}.env" ONLY_IF_DIFFERENT)
  file(WRITE "${root}.shd.tmp" "stale shd temporary output")
  file(WRITE "${root}.ray.tmp" "stale ray temporary output")
  execute_process(
    COMMAND "${F2CPP_EXECUTABLE}" "${root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "CLI lifecycle run failed: ${stderr}")
  endif()
  if(NOT EXISTS "${root}.${expected_product}")
    message(FATAL_ERROR "missing expected .${expected_product} output")
  endif()
  if(EXISTS "${root}.${forbidden_product}")
    message(FATAL_ERROR "stale .${forbidden_product} output was retained")
  endif()
  if(EXISTS "${root}.${expected_product}.tmp" OR
     EXISTS "${root}.${forbidden_product}.tmp")
    message(FATAL_ERROR "temporary output leaked after successful CLI run")
  endif()
  file(READ "${root}.prt" print_log)
  string(FIND "${print_log}" "${success_marker}" marker_index)
  if(marker_index EQUAL -1)
    message(FATAL_ERROR "success marker missing from PRT")
  endif()
endfunction()

run_case("cli_coherent.env" "shd" "ray"
         "Bellhop F2CPP completed successfully")
run_case("cli_ray.env" "ray" "shd"
         "Bellhop F2CPP ray trace completed successfully")
run_case("cli_coherent.env" "shd" "ray"
         "Bellhop F2CPP completed successfully")

file(WRITE "${root}.env" "invalid environment\n")
file(SHA256 "${root}.shd" old_shd_hash)
execute_process(
  COMMAND "${F2CPP_EXECUTABLE}" "${root}"
  RESULT_VARIABLE failure_result
  OUTPUT_VARIABLE failure_stdout
  ERROR_VARIABLE failure_stderr)
if(failure_result EQUAL 0)
  message(FATAL_ERROR "invalid ENV unexpectedly succeeded")
endif()
file(SHA256 "${root}.shd" new_shd_hash)
if(NOT old_shd_hash STREQUAL new_shd_hash)
  message(FATAL_ERROR "failed run modified the previous valid SHD")
endif()
if(EXISTS "${root}.shd.tmp" OR EXISTS "${root}.ray.tmp")
  message(FATAL_ERROR "failed CLI run leaked a temporary output")
endif()
file(READ "${root}.prt" failure_log)
string(FIND "${failure_log}" "FATAL ERROR" fatal_index)
if(fatal_index EQUAL -1)
  message(FATAL_ERROR "failed CLI run did not record FATAL ERROR")
endif()

file(REMOVE_RECURSE "${test_dir}")
