cmake_minimum_required(VERSION 3.5.0)

# Extract package name and version
find_package(ros_industrial_cmake_boilerplate REQUIRED)
extract_package_metadata(pkg)
project(${pkg_extracted_name} VERSION ${pkg_extracted_version} LANGUAGES CXX)

if(WIN32)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# System dependencies are found with CMake's conventions
find_package(Eigen3 REQUIRED)
find_package(console_bridge REQUIRED)
find_package(tesseract_collision REQUIRED)
find_package(tesseract_kinematics REQUIRED)
find_package(tesseract_scene_graph REQUIRED)
find_package(tesseract_state_solver REQUIRED)
find_package(tesseract_srdf REQUIRED)
find_package(tesseract_urdf REQUIRED)
find_package(tesseract_common REQUIRED)

if(NOT TARGET console_bridge::console_bridge)
  add_library(console_bridge::console_bridge INTERFACE IMPORTED)
  set_target_properties(console_bridge::console_bridge PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                                  ${console_bridge_INCLUDE_DIRS})
  set_target_properties(console_bridge::console_bridge PROPERTIES INTERFACE_LINK_LIBRARIES ${console_bridge_LIBRARIES})
else()
  get_target_property(CHECK_INCLUDE_DIRECTORIES console_bridge::console_bridge INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT ${CHECK_INCLUDE_DIRECTORIES})
    set_target_properties(console_bridge::console_bridge PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                                    ${console_bridge_INCLUDE_DIRS})
  endif()
endif()

# Load variable for clang tidy args, compiler options and cxx version
tesseract_variables()

initialize_code_coverage(ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})
set(COVERAGE_EXCLUDE
    /usr/*
    /opt/*
    ${CMAKE_CURRENT_LIST_DIR}/test/*
    /*/gtest/*
    /*/bullet/LinearMath/*
    /*/bullet/BulletCollision/*)
add_code_coverage_all_targets(EXCLUDE ${COVERAGE_EXCLUDE} ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})

# Create interface for core
add_library(${PROJECT_NAME} src/environment.cpp src/utils.cpp)
target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC Eigen3::Eigen
         tesseract::tesseract_common
         tesseract::tesseract_collision_core
         tesseract::tesseract_collision_bullet
         tesseract::tesseract_collision_fcl
         tesseract::tesseract_scene_graph
         tesseract::tesseract_state_solver_ofkt
         tesseract::tesseract_srdf
         tesseract::tesseract_urdf
         tesseract::tesseract_kinematics_kdl
         tesseract::tesseract_kinematics_opw)
target_compile_options(${PROJECT_NAME} PRIVATE ${TESSERACT_COMPILE_OPTIONS_PRIVATE})
target_compile_options(${PROJECT_NAME} PUBLIC ${TESSERACT_COMPILE_OPTIONS_PUBLIC})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${TESSERACT_COMPILE_DEFINITIONS})
target_clang_tidy(${PROJECT_NAME} ENABLE ${TESSERACT_ENABLE_CLANG_TIDY})
target_cxx_version(${PROJECT_NAME} PUBLIC VERSION ${TESSERACT_CXX_VERSION})
target_code_coverage(
  ${PROJECT_NAME}
  ALL
  EXCLUDE ${COVERAGE_EXCLUDE}
  ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})
target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                                  "$<INSTALL_INTERFACE:include>")

configure_package(NAMESPACE tesseract TARGETS ${PROJECT_NAME})

# Mark cpp header files for installation
install(
  DIRECTORY include/${PROJECT_NAME}
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN ".svn" EXCLUDE)

if(TESSERACT_ENABLE_TESTING)
  enable_testing()
  add_run_tests_target(ENABLE ${TESSERACT_ENABLE_RUN_TESTING})
  add_subdirectory(test)
endif()

if(TESSERACT_ENABLE_BENCHMARKING)
  add_subdirectory(test/benchmarks)
endif()

if(TESSERACT_PACKAGE)
  tesseract_cpack(
    VERSION ${pkg_extracted_version}
    MAINTAINER <https://github.com/ros-industrial-consortium/tesseract>
    DESCRIPTION ${pkg_extracted_description}
    LICENSE_FILE ${CMAKE_CURRENT_LIST_DIR}/../LICENSE
    README_FILE ${CMAKE_CURRENT_LIST_DIR}/../README.md
    LINUX_DEPENDS
      "${TESSERACT_PACKAGE_PREFIX}tesseract-collision"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-geometry"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-kinematics"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-scene-graph"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-state-solver"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-common"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-urdf"
      "libconsole-bridge-dev"
      "libeigen3-dev"
    WINDOWS_DEPENDS
      "${TESSERACT_PACKAGE_PREFIX}tesseract-collision"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-geometry"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-kinematics"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-scene-graph"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-state-solver"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-common"
      "${TESSERACT_PACKAGE_PREFIX}tesseract-urdf"
      "console_bridge"
      "Eigen3")
endif()
