cmake_minimum_required(VERSION 3.0.2)

project(trrntzip
  VERSION 0.9
  LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dist)
Dist(${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION})

include(CheckFunctionExists)

find_package(ZLIB 1.2.11 REQUIRED)

check_function_exists(fseeko HAVE_FSEEKO)
check_function_exists(fseeko64 HAVE_FSEEKO64)
check_function_exists(ftello HAVE_FTELLO)
check_function_exists(ftello64 HAVE_FTELLO64)
check_function_exists(fopen64 HAVE_FOPEN64)

# for code completion frameworks
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ADD_SUBDIRECTORY(src)

