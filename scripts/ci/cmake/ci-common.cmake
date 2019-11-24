if(NOT CTEST_BUILD_CONFIGURATION)
  set(CTEST_BUILD_CONFIGURATION Debug)
endif()

if(NOT DEFINED NCPUS)
  include(ProcessorCount)
  ProcessorCount(NCPUS)
endif()
math(EXPR N2CPUS "${NCPUS}*2")
if(NOT CTEST_BUILD_FLAGS)
  if(CTEST_CMAKE_GENERATOR STREQUAL "Unix Makefiles")
    set(CTEST_BUILD_FLAGS "-k -j${N2CPUS}")
  elseif(CTEST_CMAKE_GENERATOR STREQUAL "Ninja")
    set(CTEST_BUILD_FLAGS "-k0 -j${N2CPUs}")
  endif()
endif()
if(NOT CTEST_TEST_ARGS)
  set(CTEST_TEST_ARGS PARALLEL_LEVEL ${N2CPUS})
endif()

if(NOT dashboard_model)
  set(dashboard_model Experimental)
endif()
if(NOT dashboard_binary_name)
  set(dashboard_binary_name "build")
endif()
if(NOT dashboard_track)
  set(dashboard_track "Continuous Integration")
endif()
set(CTEST_GIT_COMMAND "/usr/bin/git")
if(NOT "$ENV{CI_COMMIT}" STREQUAL "")
  set(dashboard_git_branch "$ENV{CI_COMMIT}")
  set(CTEST_UPDATE_VERSION_ONLY ON)
endif()
if(NOT "$ENV{CI_SITE_NAME}" STREQUAL "")
  set(CTEST_SITE "$ENV{CI_SITE_NAME}")
endif()
if(NOT "$ENV{CI_BUILD_NAME}" STREQUAL "")
  set(CTEST_BUILD_NAME "$ENV{CI_BUILD_NAME}")
endif()
if(NOT "$ENV{CI_ROOT_DIR}" STREQUAL "")
  set(CTEST_DASHBOARD_ROOT "$ENV{CI_ROOT_DIR}")
endif()
if(NOT "$ENV{CI_SOURCE_DIR}" STREQUAL "")
  set(CTEST_SOURCE_DIRECTORY "$ENV{CI_SOURCE_DIR}")
endif()
if(NOT "$ENV{CI_BIN_DIR}" STREQUAL "")
  set(CTEST_BINARY_DIRECTORY "$ENV{CI_BIN_DIR}")
endif()

list(APPEND CTEST_UPDATE_NOTES_FILES "${CMAKE_CURRENT_LIST_FILE}")
include(${CMAKE_CURRENT_LIST_DIR}/../../dashboard/adios_common.cmake)
