cmake_minimum_required(VERSION 3.24)

project(
  BellhopRayReuse
  VERSION 0.1.0
  DESCRIPTION "C++20 broadband Bellhop ray-reuse implementation"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(
  RAYREUSE_ENABLE_SANITIZERS
  "Enable address and undefined-behaviour sanitizers in supported debug builds"
  OFF
)
option(
  RAYREUSE_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors"
  OFF
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bellhop_rayreuse_project_options INTERFACE)
target_compile_features(bellhop_rayreuse_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  target_compile_options(
    bellhop_rayreuse_project_options
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

  if(RAYREUSE_WARNINGS_AS_ERRORS)
    target_compile_options(bellhop_rayreuse_project_options INTERFACE -Werror)
  endif()
endif()

if(RAYREUSE_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(
      bellhop_rayreuse_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(
      bellhop_rayreuse_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
  else()
    message(FATAL_ERROR "RAYREUSE_ENABLE_SANITIZERS is unsupported by this compiler")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
  set_source_files_properties(
    src/acoustics/attenuation.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-builtin-pow"
  )
endif()

add_library(
  bellhop_rayreuse_core
  STATIC
    src/acoustics/attenuation.cpp
    src/acoustics/boundary_acoustics.cpp
    src/field/beam_epsilon.cpp
    src/field/cartesian_cerveny_influence.cpp
    src/field/geometric_gaussian_influence.cpp
    src/field/geometric_hat_influence.cpp
    src/field/simple_gaussian_influence.cpp
    src/field/arrival_workspace.cpp
    src/field/frequency_projector.cpp
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
    src/solver/broadband_nonreuse_solver.cpp
    src/solver/arrival_solver.cpp
    src/solver/eigenray_solver.cpp
    src/solver/parallel_ray_reuse_solver.cpp
    src/solver/ray_trace_product.cpp
    src/solver/serial_ray_reuse_solver.cpp
    src/solver/single_frequency_solver.cpp
)
target_include_directories(
  bellhop_rayreuse_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  bellhop_rayreuse_core
  PUBLIC
    bellhop_rayreuse_project_options
)

add_executable(bellhop_rayreuse app/main.cpp)
target_compile_definitions(
  bellhop_rayreuse
  PRIVATE
    RAYREUSE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
  bellhop_rayreuse
  PRIVATE
    bellhop_rayreuse_core
    bellhop_rayreuse_project_options
)
set_target_properties(
  bellhop_rayreuse
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

install(
  TARGETS bellhop_rayreuse
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
  FILES README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

if(BUILD_TESTING)
  include(cmake/AddRayReuseTest.cmake)

  add_rayreuse_test(
    bellhop_rayreuse_tests
    rayreuse.unit.core_types
    tests/unit/core_types_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_launch_fan_tests
    rayreuse.unit.launch_fan_planner
    tests/unit/launch_fan_planner_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_command_line_tests
    rayreuse.unit.command_line
    tests/unit/command_line_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_attenuation_tests
    rayreuse.unit.attenuation
    tests/unit/attenuation_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_boundary_acoustics_tests
    rayreuse.unit.boundary_acoustics
    tests/unit/boundary_acoustics_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_beam_epsilon_tests
    rayreuse.unit.beam_epsilon
    tests/unit/beam_epsilon_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_pressure_scaling_tests
    rayreuse.unit.pressure_scaling
    tests/unit/pressure_scaling_test.cpp
    unit
  )
  add_rayreuse_test(
    bellhop_rayreuse_frequency_projector_tests
    rayreuse.component.frequency_projector
    tests/component/frequency_projector_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_arrival_workspace_tests
    rayreuse.component.arrival_workspace
    tests/component/arrival_workspace_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_arrival_eigenray_solver_tests
    rayreuse.component.arrival_eigenray_solver
    tests/component/arrival_eigenray_solver_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_boundary_geometry_tests
    rayreuse.component.boundary_geometry
    tests/component/boundary_geometry_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_environment_parser_tests
    rayreuse.component.environment_parser
    tests/component/environment_parser_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_shd_writer_tests
    rayreuse.component.shd_writer
    tests/component/shd_writer_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_ray_writer_tests
    rayreuse.component.ray_writer
    tests/component/ray_writer_test.cpp
    component
  )
  target_compile_definitions(
    bellhop_rayreuse_environment_parser_tests
    PRIVATE
      RAYREUSE_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_rayreuse_test(
    bellhop_rayreuse_cartesian_cerveny_influence_tests
    rayreuse.component.cartesian_cerveny_influence
    tests/component/cartesian_cerveny_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_ray_centered_cerveny_influence_tests
    rayreuse.component.ray_centered_cerveny_influence
    tests/component/ray_centered_cerveny_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometric_hat_influence_tests
    rayreuse.component.geometric_hat_influence
    tests/component/geometric_hat_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometric_gaussian_influence_tests
    rayreuse.component.geometric_gaussian_influence
    tests/component/geometric_gaussian_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_simple_gaussian_influence_tests
    rayreuse.component.simple_gaussian_influence
    tests/component/simple_gaussian_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_irregular_receiver_influence_tests
    rayreuse.component.irregular_receiver_influence
    tests/component/irregular_receiver_influence_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_single_frequency_solver_tests
    rayreuse.component.single_frequency_solver
    tests/component/single_frequency_solver_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_broadband_nonreuse_solver_tests
    rayreuse.component.broadband_nonreuse_solver
    tests/component/broadband_nonreuse_solver_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_serial_ray_reuse_solver_tests
    rayreuse.reuse.serial_solver
    tests/component/serial_ray_reuse_solver_test.cpp
    "component;reuse"
  )
  add_rayreuse_test(
    bellhop_rayreuse_parallel_ray_reuse_solver_tests
    rayreuse.parallel.solver
    tests/component/parallel_ray_reuse_solver_test.cpp
    "component;parallel;reuse"
  )
  add_rayreuse_test(
    bellhop_rayreuse_multi_source_product_tests
    rayreuse.component.multi_source_product
    tests/component/multi_source_product_test.cpp
    "component;reuse;parallel"
  )
  add_rayreuse_test(
    bellhop_rayreuse_multi_source_writer_tests
    rayreuse.component.multi_source_writer
    tests/component/multi_source_writer_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_c_linear_ssp_tests
    rayreuse.component.c_linear_ssp
    tests/component/c_linear_ssp_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_pchip_ssp_tests
    rayreuse.component.pchip_ssp
    tests/component/pchip_ssp_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_n2_linear_ssp_tests
    rayreuse.component.n2_linear_ssp
    tests/component/n2_linear_ssp_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_cubic_spline_ssp_tests
    rayreuse.component.cubic_spline_ssp
    tests/component/cubic_spline_ssp_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_quadrilateral_ssp_tests
    rayreuse.component.quadrilateral_ssp
    tests/component/quadrilateral_ssp_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_sound_speed_evaluator_tests
    rayreuse.component.sound_speed_evaluator
    tests/component/sound_speed_evaluator_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_ray_stepper_tests
    rayreuse.component.ray_stepper
    tests/component/ray_stepper_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometry_tracer_tests
    rayreuse.component.geometry_tracer
    tests/component/geometry_tracer_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometry_tracer_ssp_interface_tests
    rayreuse.component.geometry_tracer_ssp_interface
    tests/component/geometry_tracer_ssp_interface_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_flat_boundary_reflection_tests
    rayreuse.component.flat_boundary_reflection
    tests/component/flat_boundary_reflection_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometry_tracer_reflection_tests
    rayreuse.component.geometry_tracer_reflection
    tests/component/geometry_tracer_reflection_test.cpp
    component
  )
  add_rayreuse_test(
    bellhop_rayreuse_geometry_tracer_munk_tests
    rayreuse.component.geometry_tracer_munk
    tests/component/geometry_tracer_munk_test.cpp
    component
  )
  add_executable(
    bellhop_rayreuse_geometry_oracle_probe
    tests/tools/geometry_oracle_probe.cpp
  )
  target_include_directories(
    bellhop_rayreuse_geometry_oracle_probe
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )
  target_link_libraries(
    bellhop_rayreuse_geometry_oracle_probe
    PRIVATE
      bellhop_rayreuse_core
      bellhop_rayreuse_project_options
  )
  add_test(NAME rayreuse.cli.help COMMAND bellhop_rayreuse --help)
  set_tests_properties(rayreuse.cli.help PROPERTIES LABELS "unit")

  add_test(NAME rayreuse.cli.version COMMAND bellhop_rayreuse --version)
  set_tests_properties(
    rayreuse.cli.version
    PROPERTIES
      LABELS "unit"
      PASS_REGULAR_EXPRESSION "Bellhop RayReuse ${PROJECT_VERSION}"
  )

  add_test(NAME rayreuse.cli.missing_root COMMAND bellhop_rayreuse)
  set_tests_properties(
    rayreuse.cli.missing_root
    PROPERTIES
      LABELS "unit"
      WILL_FAIL TRUE
  )
endif()

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "bellhop-rayreuse")
set(CPACK_PACKAGE_VENDOR "Bellhop RayReuse")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(
  CPACK_PACKAGE_FILE_NAME
  "bellhop-rayreuse-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
include(CPack)
