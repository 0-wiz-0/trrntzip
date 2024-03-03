cmake_minimum_required(VERSION 3.5)

project(trrntzip
  VERSION 1.2
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

# Look for Python and nihtest for testing

if (NOT PYTHONBIN)
  find_package (Python COMPONENTS Interpreter)
  set (PYTHONBIN ${Python_EXECUTABLE})
else()
  message("Python set to ${PYTHONBIN}")
endif()

find_program(NIHTEST nihtest)
set(NIHTEST_REQUIRED_VERSION "1.5")

option(RUN_REGRESS "Run regression tests" ON)

if(RUN_REGRESS)
  if (NOT NIHTEST OR NOT PYTHONBIN)
    message(WARNING "-- nihtest or Python interpreter not found, regression testing disabled")
    set(RUN_REGRESS OFF)
  else()
    execute_process(COMMAND ${NIHTEST} --version OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NIHTEST_VERSION_RAW)
    string(REGEX REPLACE "[^ ]* " "" NIHTEST_VERSION ${NIHTEST_VERSION_RAW})
    if (${NIHTEST_VERSION} VERSION_LESS ${NIHTEST_REQUIRED_VERSION})
      message(WARNING "-- nihtest ${NIHTEST_VERSION} too old, at least ${NIHTEST_REQUIRED_VERSION} required, regression testing disabled")
      set(RUN_REGRESS OFF)
    endif()
  endif()
endif()

# for testing
set(srcdir ${CMAKE_CURRENT_SOURCE_DIR}/regress)
set(abs_srcdir ${CMAKE_CURRENT_SOURCE_DIR}/regress)
set(top_builddir ${CMAKE_BINARY_DIR})
enable_testing()

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(regress)
