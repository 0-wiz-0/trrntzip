cmake_minimum_required(VERSION 3.5)

project(trrntzip
  VERSION 1.1
  LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dist)
Dist(${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION})

find_package(ZLIB 1.2.2 REQUIRED)

include(CheckSymbolExists)

set(CMAKE_REQUIRED_DEFINITIONS -D_FILE_OFFSET_BITS=64 #[[this suffices for
  modern platforms, but add the obsolete macros below for old systems, too]]
  -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE)
check_symbol_exists(fseeko stdio.h HAVE_FSEEKO)
check_symbol_exists(fseeko64 stdio.h HAVE_FSEEKO64)
check_symbol_exists(ftello stdio.h HAVE_FTELLO)
check_symbol_exists(ftello64 stdio.h HAVE_FTELLO64)
check_symbol_exists(fopen64 stdio.h HAVE_FOPEN64)

add_definitions(${CMAKE_REQUIRED_DEFINITIONS})
foreach(def HAVE_FSEEKO HAVE_FSEEKO64 HAVE_FTELLO HAVE_FTELLO64 HAVE_FOPEN64)
  if(${def})
    add_definitions(-D${def})
  endif()
endforeach()

# for code completion frameworks
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ADD_SUBDIRECTORY(src)

