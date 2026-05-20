cmake_minimum_required(VERSION 3.14)

project(dagwood
  VERSION 0.1.0
  DESCRIPTION "Dagwood shell orchestration"
  LANGUAGES C
)

set(DAGWOOD_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/dagwood-version.h.in"
  "${DAGWOOD_VERSION_HEADER_DIR}/dagwood-version.h"
  @ONLY
)

option(DAGWOOD_BUILD_TESTS "Build test executable" ON)
option(DAGWOOD_ENABLE_ASAN "Enable AddressSanitizer (debug builds)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(DAGWOOD_COMPILE_OPTIONS -Wall -Wextra -D_POSIX_C_SOURCE=200809L)

set(LCL_BUILD_CLI OFF CACHE BOOL "Don't build LCL CLI" FORCE)
set(LCL_BUILD_SHARED OFF CACHE BOOL "Don't build shared LCL" FORCE)

set(LCL_BUILD_IO ON CACHE BOOL "Build Lcl IO extension" FORCE)
set(LCL_BUILD_TIME ON CACHE BOOL "Build Lcl time extension" FORCE)
set(LCL_BUILD_PROCESS ON CACHE BOOL "Build Lcl process extension" FORCE)
set(LCL_BUILD_EXPECT_LIB ON CACHE BOOL "Build Lcl expect library" FORCE)
set(LCL_BUILD_SH_LIB ON CACHE BOOL "Build Lcl sh library" FORCE)
set(LCL_BUILD_MACRO_LIB ON CACHE BOOL "Build Lcl macro library" FORCE)

include(FetchContent)
FetchContent_Declare(
  lcl
  GIT_REPOSITORY https://github.com/bholten/lcl.git
  GIT_TAG        c1ef73daa6a2a2dab8f36823fe9027527e274542
  GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(lcl)

if(DAGWOOD_ENABLE_ASAN)
  list(APPEND DAGWOOD_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
  set(DAGWOOD_LINK_OPTIONS -fsanitize=address,undefined)
endif()

include(CheckSymbolExists)
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(posix_spawn_file_actions_addchdir_np spawn.h HAVE_SPAWN_ADDCHDIR_NP)
if(HAVE_SPAWN_ADDCHDIR_NP)
  list(APPEND DAGWOOD_COMPILE_OPTIONS -DHAVE_SPAWN_ADDCHDIR_NP)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EmbedFile)

set(DAGWOOD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
dagwood_embed_file(
  INPUT  lib/dagwood.lcl
  OUTPUT "${DAGWOOD_GEN_DIR}/dagwood_dsl.h"
  SYMBOL lib_dagwood_lcl
  TARGET generate_dsl_header
)

set(DAGWOOD_SOURCES
  src/lcl_interp.c
  src/main.c
  src/dag.c
  src/graph.c
  src/task.c
  src/project.c
  src/shell.c
  src/s_arr.c
  src/t_arr.c
  src/t_map.c
  src/ts_map.c
  src/t_layers.c
  src/p_map.c
)

add_executable(dagwood ${DAGWOOD_SOURCES})
set_target_properties(dagwood PROPERTIES OUTPUT_NAME dag)

target_compile_options(dagwood PRIVATE ${DAGWOOD_COMPILE_OPTIONS})

target_include_directories(dagwood PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DAGWOOD_GEN_DIR}
  ${DAGWOOD_VERSION_HEADER_DIR}
  ${lcl_SOURCE_DIR}/packages/lcl-io/include
)

target_link_libraries(dagwood PRIVATE lcl_static lcl_io)

add_dependencies(dagwood generate_dsl_header)

if(DAGWOOD_ENABLE_ASAN)
  target_link_options(dagwood PRIVATE ${DAGWOOD_LINK_OPTIONS})
endif()

if(DAGWOOD_BUILD_TESTS)
  set(DAGWOOD_DATA_SOURCES
    src/s_arr.c
    src/t_arr.c
    src/t_map.c
    src/ts_map.c
    src/t_layers.c
    src/p_map.c
    src/dag.c
    src/task.c
    src/project.c
    src/shell.c
  )

  add_executable(test_data_structures
    test/test_data_structures.c
    ${DAGWOOD_DATA_SOURCES}
  )

  target_compile_options(test_data_structures PRIVATE ${DAGWOOD_COMPILE_OPTIONS})

  target_include_directories(test_data_structures PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  if(DAGWOOD_ENABLE_ASAN)
    target_link_options(test_data_structures PRIVATE ${DAGWOOD_LINK_OPTIONS})
  endif()

  enable_testing()

  add_test(NAME unit COMMAND test_data_structures)

  add_test(
    NAME integration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/integration.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  # Point the integration suite at this build's binary so ctest picks up
  # whichever flavor (regular, ASan, etc.) is configured here.
  set_tests_properties(integration PROPERTIES
    ENVIRONMENT "DAGWOOD=$<TARGET_FILE:dagwood>"
  )
endif()

install(TARGETS dagwood RUNTIME DESTINATION bin)
