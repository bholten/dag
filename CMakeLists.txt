cmake_minimum_required(VERSION 3.14)

project(dagwood
  VERSION 0.1.0
  DESCRIPTION "Dagwood shell orchestration"
  LANGUAGES C
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

add_subdirectory(submodules/lcl)

if(DAGWOOD_ENABLE_ASAN)
  list(APPEND DAGWOOD_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
  set(DAGWOOD_LINK_OPTIONS -fsanitize=address,undefined)
endif()

set(DSL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/lib/dagwood.lcl)
set(DSL_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/dagwood_dsl.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/generated)

add_custom_command(
  OUTPUT ${DSL_HEADER}
  COMMAND xxd -i lib/dagwood.lcl > ${DSL_HEADER}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${DSL_SOURCE}
  COMMENT "Generating embedded DSL header from lib/dagwood.lcl"
)

add_custom_target(generate_dsl_header DEPENDS ${DSL_HEADER})

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

target_compile_options(dagwood PRIVATE ${DAGWOOD_COMPILE_OPTIONS})

target_include_directories(dagwood PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/submodules/lcl/packages/lcl-io/include
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
endif()

install(TARGETS dagwood RUNTIME DESTINATION bin)
