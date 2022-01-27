cmake_minimum_required(VERSION 2.8.2)

set(CMAKE_VERBOSE_MAKEFILE ON)

project(jsc)

set(VERSION "9dAbcouwer")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-long-long -pedantic -Werror -Wno-unused-local-typedefs")

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)

set(MY_TIMEOUT 1500)

if (CMAKE_BUILD_TYPE STREQUAL "Coverage")
  # build the unit tests
  set(CMAKE_BUILD_TYPE "Test")
  set(CMAKE_MODULE_PATH ${CMAKE_HOME_DIRECTORY}/test)
  add_custom_target(ctest COMMAND ${CMAKE_CTEST_COMMAND})
  include(CodeCoverage)
  APPEND_COVERAGE_COMPILER_FLAGS()
  set(COVERAGE_EXCLUDES 
    '/usr/*' 
    '${CMAKE_CURRENT_SOURCE_DIR}/test/*'
    '${CMAKE_CURRENT_SOURCE_DIR}/build/*')
  MESSAGE( STATUS "COVERAGE_EXCLUDES: " ${COVERAGE_EXCLUDES} )
  setup_target_for_coverage(
    NAME coverage 
    EXECUTABLE ctest
    DEPENDENCIES ctest)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Performance")
  add_definitions(-DJPEG_ENABLE_PERFORMANCE_TESTS=1)
  
  # Download kodak set

  if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/corpus/kodak/RIGHTS.USE)
  else()
  
          configure_file(test/KodakCMakeLists.txt.in download/kodak/CMakeLists.txt)
          execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/download/kodak )
          if(result)
            message(FATAL_ERROR "CMake step for kodak failed: ${result}")
          endif()
          execute_process(COMMAND ${CMAKE_COMMAND} --build .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/download/kodak )
          if(result)
            message(FATAL_ERROR "Build step for kodak failed: ${result}")
          endif()
  endif()
  
  set(CMAKE_BUILD_TYPE "Test")
  
  # default timeout of 1500 for test may not be enough
  # for performance testing on slow systems
  set(MY_TIMEOUT 2000)
  
endif()  

if (CMAKE_BUILD_TYPE STREQUAL "CfsTest")
  configure_file(
      ${CMAKE_SOURCE_DIR}/configs/cfs/jsc_cfs_global_types.h 
      ${CMAKE_SOURCE_DIR}/include/jsc/jsc_conf_global_types.h)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/test/cfs)
  set(CMAKE_BUILD_TYPE "Test")
else()
    configure_file(
      ${CMAKE_SOURCE_DIR}/test/jsc_test_global_types.h 
      ${CMAKE_SOURCE_DIR}/include/jsc/jsc_conf_global_types.h)
endif()


if (CMAKE_BUILD_TYPE STREQUAL "Test")

  # Copy testing headers
  
  configure_file(
      ${CMAKE_SOURCE_DIR}/test/jsc_test_private.h 
      ${CMAKE_SOURCE_DIR}/include/jsc/jsc_conf_private.h)

  set(CMAKE_BUILD_TYPE "Debug")
  add_definitions(-DJSC_PRIVATE=)
  set(ENV{CTEST_OUTPUT_ON_FAILURE} TRUE)

  include(CTest)
  enable_testing()
    
  #============================================================================
  # Download imageio
  #============================================================================

  if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/imageio/basic.h)
  else()
  
          configure_file(test/ImageioCMakeLists.txt.in download/imageio/CMakeLists.txt)
          execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/download/imageio )
          if(result)
            message(FATAL_ERROR "CMake step for imageio failed: ${result}")
          endif()
          execute_process(COMMAND ${CMAKE_COMMAND} --build .
            RESULT_VARIABLE result
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/download/imageio )
          if(result)
            message(FATAL_ERROR "Build step for imageio failed: ${result}")
          endif()
  endif()
  
  set(IMAGEIO_SRCS
    ${CMAKE_CURRENT_BINARY_DIR}/imageio/basic.c
    ${CMAKE_CURRENT_BINARY_DIR}/imageio/imageio.c
  )
    
  include_directories(${CMAKE_CURRENT_BINARY_DIR}/imageio)
  link_directories(${CMAKE_CURRENT_BINARY_DIR}/imageio)

  set(IMAGEIO_SRCS
    ${CMAKE_CURRENT_BINARY_DIR}/imageio/basic.c
    ${CMAKE_CURRENT_BINARY_DIR}/imageio/imageio.c
  )
    
  include_directories(${CMAKE_CURRENT_BINARY_DIR}/imageio)
  link_directories(${CMAKE_CURRENT_BINARY_DIR}/imageio)
  
 
    
  #============================================================================
  # Download and unpack googletest at configure time
  #============================================================================
  
  configure_file(test/GoogleTestCMakeLists.txt.in googletest-download/CMakeLists.txt)
  
  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
  if(result)
    message(FATAL_ERROR "CMake step for googletest failed: ${result}")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
  if(result)
    message(FATAL_ERROR "Build step for googletest failed: ${result}")
  endif()
  
  # Prevent overriding the parent project's compiler/linker
  # settings on Windows
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  
  # Add googletest directly to our build. This defines
  # the gtest and gtest_main targets.
  add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
                   ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
                   EXCLUDE_FROM_ALL)
  
  # The gtest/gtest_main targets carry header search path
  # dependencies automatically when using CMake 2.8.11 or
  # later. Otherwise we have to add them here ourselves.
  if (CMAKE_VERSION VERSION_LESS 2.8.11)
    include_directories("${gtest_SOURCE_DIR}/include")
  endif()

  
  #==========================================================================
  # gtest binaries
  #==========================================================================
  add_executable(jsc_gtest
    include/jsc/jerror.h 
    include/jsc/jpeglib.h
    src/jsc_compress.c
    src/jcapimin.c 
    src/jcapistd.c 
    src/jccoefct.c 
    src/jccolor.c
    src/jcdctmgr.c 
    src/jchuff.c 
    src/jcinit.c src/jcmainct.c src/jcmarker.c src/jcmaster.c
    src/jcomapi.c src/jcparam.c src/jcprepct.c src/jcsample.c src/jdapimin.c
    src/jdapistd.c src/jscdatadst.c src/jdatasrc.c src/jdcoefct.c src/jdcolor.c
    src/jddctmgr.c src/jdhuff.c src/jdinput.c src/jdmainct.c src/jdmarker.c src/jdmaster.c
    src/jdmerge.c src/jdpostct.c src/jdsample.c src/jfdctflt.c
    src/jidctflt.c 
    src/jutils.c src/jmemmgr.c
    src/jscmemstatic.c
    test/jsc_gtest.cpp
    ${IMAGEIO_SRCS})  
  target_link_libraries(jsc_gtest gtest_main )
  add_test(NAME jsc_gtest_test COMMAND jsc_gtest)
  
  set_tests_properties(jsc_gtest_test PROPERTIES TIMEOUT ${MY_TIMEOUT}) 
  
  
endif()

