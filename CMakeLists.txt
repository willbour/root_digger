cmake_minimum_required(VERSION 3.00)
project(root_digger)

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -fno-omit-frame-pointer" CACHE INTERNAL "")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer" CACHE INTERNAL "")
set(CMAKE_C_FLAGS_RELEASE "-O3 -fno-omit-frame-pointer" CACHE INTERNAL "")

find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags
        WORKING_DIRECTORY "${local_dir}"
        OUTPUT_VARIABLE RD_BUILD_VERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "-g.+" "" RD_BUILD_VERSION ${RD_BUILD_VERSION})
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY "${local_dir}"
        OUTPUT_VARIABLE RD_BUILD_COMMIT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

string(TIMESTAMP RD_BUILD_DATE "%Y-%m-%d %H:%M:%S")

set(LOCALDEPS_DIR ${PROJECT_BINARY_DIR}/localdeps)
add_subdirectory(lib)
include_directories(${LOCALDEPS_DIR}/include)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
add_subdirectory(src)

find_library(ASAN_FOUND asan)
if(ASAN_FOUND)
    add_subdirectory(test)
    add_subdirectory(benchmark)
endif()

