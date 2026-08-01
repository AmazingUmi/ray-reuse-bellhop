cmake_minimum_required(VERSION 3.24)

project(
  BellhopF2CPP
  VERSION 0.1.0
  DESCRIPTION "C++20 single-frequency Bellhop reference implementation"
  LANGUAGES CXX
)

include(CTest)

option(
  F2CPP_ENABLE_SANITIZERS
  "Enable address and undefined-behaviour sanitizers in supported debug builds"
  OFF
)
option(
  F2CPP_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors"
  OFF
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bellhop_f2cpp_project_options INTERFACE)
target_compile_features(bellhop_f2cpp_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  target_compile_options(
    bellhop_f2cpp_project_options
    INTERFACE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
  )

  if(F2CPP_WARNINGS_AS_ERRORS)
    target_compile_options(bellhop_f2cpp_project_options INTERFACE -Werror)
  endif()
endif()

if(F2CPP_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(
      bellhop_f2cpp_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(
      bellhop_f2cpp_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
  else()
    message(FATAL_ERROR "F2CPP_ENABLE_SANITIZERS is unsupported by this compiler")
  endif()
endif()

add_library(
  bellhop_f2cpp_core
  STATIC
    src/acoustics/attenuation.cpp
    src/acoustics/boundary_acoustics.cpp
    src/field/beam_epsilon.cpp
    src/field/cartesian_cerveny_influence.cpp
    src/field/frequency_projector.cpp
    src/field/pressure_scaling.cpp
    src/io/environment_parser.cpp
    src/io/shd_writer.cpp
    src/model/c_linear_frequency_ssp.cpp
    src/model/c_linear_ssp.cpp
    src/model/environment.cpp
    src/model/launch_fan_planner.cpp
    src/model/simulation_case.cpp
    src/cache/ray_path_cache.cpp
    src/field/frequency_workspace.cpp
    src/ray/flat_boundary_reflection.cpp
    src/ray/geometry_tracer.cpp
    src/ray/ray_stepper.cpp
    src/solver/single_frequency_solver.cpp
)
target_include_directories(
  bellhop_f2cpp_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  bellhop_f2cpp_core
  PUBLIC
    bellhop_f2cpp_project_options
)

add_executable(bellhop_f2cpp app/main.cpp)
target_link_libraries(
  bellhop_f2cpp
  PRIVATE
    bellhop_f2cpp_core
    bellhop_f2cpp_project_options
)
set_target_properties(
  bellhop_f2cpp
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(BUILD_TESTING)
  include(cmake/AddF2CPPTest.cmake)

  add_f2cpp_test(
    bellhop_f2cpp_tests
    f2cpp.unit.core_types
    tests/unit/core_types_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_launch_fan_tests
    f2cpp.unit.launch_fan_planner
    tests/unit/launch_fan_planner_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_attenuation_tests
    f2cpp.unit.attenuation
    tests/unit/attenuation_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_boundary_acoustics_tests
    f2cpp.unit.boundary_acoustics
    tests/unit/boundary_acoustics_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_beam_epsilon_tests
    f2cpp.unit.beam_epsilon
    tests/unit/beam_epsilon_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_pressure_scaling_tests
    f2cpp.unit.pressure_scaling
    tests/unit/pressure_scaling_test.cpp
    unit
  )
  add_f2cpp_test(
    bellhop_f2cpp_frequency_projector_tests
    f2cpp.component.frequency_projector
    tests/component/frequency_projector_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_environment_parser_tests
    f2cpp.component.environment_parser
    tests/component/environment_parser_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_shd_writer_tests
    f2cpp.component.shd_writer
    tests/component/shd_writer_test.cpp
    component
  )
  target_compile_definitions(
    bellhop_f2cpp_environment_parser_tests
    PRIVATE
      F2CPP_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_f2cpp_test(
    bellhop_f2cpp_cartesian_cerveny_influence_tests
    f2cpp.component.cartesian_cerveny_influence
    tests/component/cartesian_cerveny_influence_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_single_frequency_solver_tests
    f2cpp.component.single_frequency_solver
    tests/component/single_frequency_solver_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_c_linear_ssp_tests
    f2cpp.component.c_linear_ssp
    tests/component/c_linear_ssp_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_ray_stepper_tests
    f2cpp.component.ray_stepper
    tests/component/ray_stepper_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_geometry_tracer_tests
    f2cpp.component.geometry_tracer
    tests/component/geometry_tracer_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_geometry_tracer_ssp_interface_tests
    f2cpp.component.geometry_tracer_ssp_interface
    tests/component/geometry_tracer_ssp_interface_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_flat_boundary_reflection_tests
    f2cpp.component.flat_boundary_reflection
    tests/component/flat_boundary_reflection_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_geometry_tracer_reflection_tests
    f2cpp.component.geometry_tracer_reflection
    tests/component/geometry_tracer_reflection_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_geometry_tracer_munk_tests
    f2cpp.component.geometry_tracer_munk
    tests/component/geometry_tracer_munk_test.cpp
    component
  )
  add_f2cpp_test(
    bellhop_f2cpp_numerical_contract_tests
    f2cpp.unit.numerical_contract
    tests/unit/numerical_contract_test.cpp
    unit
  )
  add_executable(
    bellhop_f2cpp_geometry_oracle_probe
    tests/tools/geometry_oracle_probe.cpp
  )
  target_include_directories(
    bellhop_f2cpp_geometry_oracle_probe
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )
  target_link_libraries(
    bellhop_f2cpp_geometry_oracle_probe
    PRIVATE
      bellhop_f2cpp_core
      bellhop_f2cpp_project_options
  )
  add_test(NAME f2cpp.cli.help COMMAND bellhop_f2cpp --help)
  set_tests_properties(f2cpp.cli.help PROPERTIES LABELS "unit")

  add_test(NAME f2cpp.cli.missing_root COMMAND bellhop_f2cpp)
  set_tests_properties(
    f2cpp.cli.missing_root
    PROPERTIES
      LABELS "unit"
      WILL_FAIL TRUE
  )
endif()
