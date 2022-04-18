cmake_minimum_required(VERSION 3.0.2)

project(trrntzip
  VERSION 0.9
  LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dist)
Dist(${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION})

find_package(ZLIB 1.2.2 REQUIRED)

# for code completion frameworks
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ADD_SUBDIRECTORY(src)

