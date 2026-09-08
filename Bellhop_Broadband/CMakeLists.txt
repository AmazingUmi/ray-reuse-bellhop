cmake_minimum_required(VERSION 3.24)

project(
  BellhopBroadband
  VERSION 0.1.0
  DESCRIPTION "C++20 Bellhop Broadband execution framework"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(
  BROADBAND_ENABLE_SANITIZERS
  "Enable address and undefined-behaviour sanitizers in supported debug builds"
  OFF
)
option(
  BROADBAND_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors"
  OFF
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bellhop_broadband_project_options INTERFACE)
target_compile_features(bellhop_broadband_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  target_compile_options(
    bellhop_broadband_project_options
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

  if(BROADBAND_WARNINGS_AS_ERRORS)
    target_compile_options(bellhop_broadband_project_options INTERFACE -Werror)
  endif()
endif()

if(BROADBAND_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(
      bellhop_broadband_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(
      bellhop_broadband_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
  else()
    message(FATAL_ERROR "BROADBAND_ENABLE_SANITIZERS is unsupported by this compiler")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
  set_source_files_properties(
    src/acoustics/attenuation.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-builtin-pow"
  )
endif()

add_library(
  bellhop_broadband_core
  STATIC
    src/acoustics/attenuation.cpp
    src/acoustics/boundary_acoustics.cpp
    src/field/beam_epsilon.cpp
    src/field/cartesian_cerveny_influence.cpp
    src/field/geometric_gaussian_influence.cpp
    src/field/geometric_hat_influence.cpp
    src/field/simple_gaussian_influence.cpp
    src/field/arrival_accumulator.cpp
    src/field/arrival_workspace.cpp
    src/field/broadband_arrival_workspace.cpp
    src/field/frequency_projector.cpp
    src/field/fused_intensity_workspace.cpp
    src/field/fused_pressure_workspace.cpp
    src/field/pressure_scaling.cpp
    src/field/ray_centered_cerveny_influence.cpp
    src/io/command_line.cpp
    src/io/ray_prefix_writer.cpp
    src/io/ray_writer.cpp
    src/io/environment_parser.cpp
    src/io/shd_writer.cpp
    src/model/boundary_geometry.cpp
    src/model/c_linear_frequency_ssp.cpp
    src/model/c_linear_ssp.cpp
    src/model/cubic_spline_ssp.cpp
    src/model/cubic_spline_frequency_ssp.cpp
    src/model/environment.cpp
    src/model/launch_fan_planner.cpp
    src/model/n2_linear_frequency_ssp.cpp
    src/model/n2_linear_ssp.cpp
    src/model/pchip_frequency_ssp.cpp
    src/model/pchip_ssp.cpp
    src/model/quadrilateral_frequency_ssp.cpp
    src/model/quadrilateral_ssp.cpp
    src/model/simulation_case.cpp
    src/model/sound_speed_evaluator.cpp
    src/numerics/cubic_spline_coefficients.cpp
    src/numerics/pchip_coefficients.cpp
    src/cache/ray_path_cache.cpp
    src/field/frequency_workspace.cpp
    src/io/arrival_writer.cpp
    src/io/eigenray_writer.cpp
    src/ray/flat_boundary_reflection.cpp
    src/ray/geometry_tracer.cpp
    src/ray/ray_stepper.cpp
    src/solver/nonreuse_solver.cpp
    src/solver/arrival_solver.cpp
    src/solver/reuse_range_para_solver.cpp
    src/solver/eigenray_solver.cpp
    src/solver/reuse_freq_para_solver.cpp
    src/solver/ray_trace_product.cpp
    src/solver/reuse_serial_solver.cpp
    src/solver/single_frequency_solver.cpp
)
target_include_directories(
  bellhop_broadband_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  bellhop_broadband_core
  PUBLIC
    bellhop_broadband_project_options
)

add_executable(bellhop_broadband app/main.cpp)
target_compile_definitions(
  bellhop_broadband
  PRIVATE
    BELLHOP_BROADBAND_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
  bellhop_broadband
  PRIVATE
    bellhop_broadband_core
    bellhop_broadband_project_options
)
set_target_properties(
  bellhop_broadband
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

install(
  TARGETS bellhop_broadband
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
  FILES README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

if(BUILD_TESTING)
  include(cmake/AddBroadbandTest.cmake)

  add_broadband_test(
    bellhop_broadband_tests
    broadband.unit.core_types
    tests/unit/core_types_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_launch_fan_tests
    broadband.unit.launch_fan_planner
    tests/unit/launch_fan_planner_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_command_line_tests
    broadband.unit.command_line
    tests/unit/command_line_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_attenuation_tests
    broadband.unit.attenuation
    tests/unit/attenuation_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_boundary_acoustics_tests
    broadband.unit.boundary_acoustics
    tests/unit/boundary_acoustics_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_beam_epsilon_tests
    broadband.unit.beam_epsilon
    tests/unit/beam_epsilon_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_pressure_scaling_tests
    broadband.unit.pressure_scaling
    tests/unit/pressure_scaling_test.cpp
    unit
  )
  add_broadband_test(
    bellhop_broadband_frequency_projector_tests
    broadband.component.frequency_projector
    tests/component/frequency_projector_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_arrival_workspace_tests
    broadband.component.arrival_workspace
    tests/component/arrival_workspace_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_broadband_arrival_workspace_tests
    broadband.component.broadband_arrival_workspace
    tests/component/broadband_arrival_workspace_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_arrival_eigenray_solver_tests
    broadband.component.arrival_eigenray_solver
    tests/component/arrival_eigenray_solver_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_boundary_geometry_tests
    broadband.component.boundary_geometry
    tests/component/boundary_geometry_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_environment_parser_tests
    broadband.component.environment_parser
    tests/component/environment_parser_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_shd_writer_tests
    broadband.component.shd_writer
    tests/component/shd_writer_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_ray_writer_tests
    broadband.component.ray_writer
    tests/component/ray_writer_test.cpp
    component
  )
  target_compile_definitions(
    bellhop_broadband_environment_parser_tests
    PRIVATE
      BROADBAND_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_broadband_test(
    bellhop_broadband_cartesian_cerveny_influence_tests
    broadband.component.cartesian_cerveny_influence
    tests/component/cartesian_cerveny_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_ray_centered_cerveny_influence_tests
    broadband.component.ray_centered_cerveny_influence
    tests/component/ray_centered_cerveny_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometric_hat_influence_tests
    broadband.component.geometric_hat_influence
    tests/component/geometric_hat_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometric_gaussian_influence_tests
    broadband.component.geometric_gaussian_influence
    tests/component/geometric_gaussian_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_simple_gaussian_influence_tests
    broadband.component.simple_gaussian_influence
    tests/component/simple_gaussian_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_irregular_receiver_influence_tests
    broadband.component.irregular_receiver_influence
    tests/component/irregular_receiver_influence_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_single_frequency_solver_tests
    broadband.component.single_frequency_solver
    tests/component/single_frequency_solver_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_nonreuse_solver_tests
    broadband.nonreuse.solver
    tests/component/nonreuse_solver_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_reuse_serial_solver_tests
    broadband.reuse.serial.solver
    tests/component/reuse_serial_solver_test.cpp
    "component;reuse;broadband.reuse.serial"
  )
  add_broadband_test(
    bellhop_broadband_reuse_range_para_solver_tests
    broadband.reuse.range.solver
    tests/component/reuse_range_para_solver_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_range_cc_parity_tests
    broadband.reuse.range.cc_parity
    tests/component/range_cc_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_range_cc_intensity_parity_tests
    broadband.reuse.range.cc_intensity_parity
    tests/component/range_cc_intensity_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_range_rc_parity_tests
    broadband.reuse.range.rc_parity
    tests/component/range_rc_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_range_hat_parity_tests
    broadband.reuse.range.hat_parity
    tests/component/range_hat_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_range_geometric_gaussian_parity_tests
    broadband.reuse.range.geometric_gaussian_parity
    tests/component/range_geometric_gaussian_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_fused_arrival_parity_tests
    broadband.component.fused_arrival_parity
    tests/component/fused_arrival_parity_test.cpp
    "component;reuse;arrival"
  )
  add_broadband_test(
    bellhop_broadband_range_simple_gaussian_parity_tests
    broadband.reuse.range.simple_gaussian_parity
    tests/component/range_simple_gaussian_parity_test.cpp
    "component;reuse;broadband.reuse.range"
  )
  add_broadband_test(
    bellhop_broadband_reuse_freq_para_solver_tests
    broadband.reuse.frequency.solver
    tests/component/reuse_freq_para_solver_test.cpp
    "component;reuse;broadband.reuse.frequency"
  )
  add_broadband_test(
    bellhop_broadband_multi_source_product_tests
    broadband.component.multi_source_product
    tests/component/multi_source_product_test.cpp
    "component;reuse"
  )
  add_broadband_test(
    bellhop_broadband_multi_source_writer_tests
    broadband.component.multi_source_writer
    tests/component/multi_source_writer_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_c_linear_ssp_tests
    broadband.component.c_linear_ssp
    tests/component/c_linear_ssp_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_pchip_ssp_tests
    broadband.component.pchip_ssp
    tests/component/pchip_ssp_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_n2_linear_ssp_tests
    broadband.component.n2_linear_ssp
    tests/component/n2_linear_ssp_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_cubic_spline_ssp_tests
    broadband.component.cubic_spline_ssp
    tests/component/cubic_spline_ssp_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_quadrilateral_ssp_tests
    broadband.component.quadrilateral_ssp
    tests/component/quadrilateral_ssp_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_sound_speed_evaluator_tests
    broadband.component.sound_speed_evaluator
    tests/component/sound_speed_evaluator_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_ray_stepper_tests
    broadband.component.ray_stepper
    tests/component/ray_stepper_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometry_tracer_tests
    broadband.component.geometry_tracer
    tests/component/geometry_tracer_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometry_tracer_ssp_interface_tests
    broadband.component.geometry_tracer_ssp_interface
    tests/component/geometry_tracer_ssp_interface_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_flat_boundary_reflection_tests
    broadband.component.flat_boundary_reflection
    tests/component/flat_boundary_reflection_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometry_tracer_reflection_tests
    broadband.component.geometry_tracer_reflection
    tests/component/geometry_tracer_reflection_test.cpp
    component
  )
  add_broadband_test(
    bellhop_broadband_geometry_tracer_munk_tests
    broadband.component.geometry_tracer_munk
    tests/component/geometry_tracer_munk_test.cpp
    component
  )
  add_executable(
    bellhop_broadband_geometry_oracle_probe
    tests/tools/geometry_oracle_probe.cpp
  )
  target_include_directories(
    bellhop_broadband_geometry_oracle_probe
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )
  target_link_libraries(
    bellhop_broadband_geometry_oracle_probe
    PRIVATE
      bellhop_broadband_core
      bellhop_broadband_project_options
  )
  add_test(NAME broadband.cli.help COMMAND bellhop_broadband --help)
  set_tests_properties(broadband.cli.help PROPERTIES LABELS "unit")

  add_test(NAME broadband.cli.version COMMAND bellhop_broadband --version)
  set_tests_properties(
    broadband.cli.version
    PROPERTIES
      LABELS "unit"
      PASS_REGULAR_EXPRESSION "Bellhop Broadband ${PROJECT_VERSION}"
  )

  add_test(NAME broadband.cli.missing_root COMMAND bellhop_broadband)
  set_tests_properties(
    broadband.cli.missing_root
    PROPERTIES
      LABELS "unit"
      WILL_FAIL TRUE
  )
endif()

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "bellhop-broadband")
set(CPACK_PACKAGE_VENDOR "Bellhop Broadband")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(
  CPACK_PACKAGE_FILE_NAME
  "bellhop-broadband-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
include(CPack)
