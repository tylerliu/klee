cmake_minimum_required(VERSION 3.13)
project(trace_ir_instrs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

################################################################################
# Find LLVM
################################################################################
include(${CMAKE_SOURCE_DIR}/../cmake/string_to_list.cmake)
include(${CMAKE_SOURCE_DIR}/../cmake/find_llvm.cmake)
set(NEEDED_LLVM_VARS
  LLVM_PACKAGE_VERSION
  LLVM_VERSION_MAJOR
  LLVM_VERSION_MINOR
  LLVM_VERSION_PATCH
  LLVM_DEFINITIONS
  LLVM_ENABLE_EH
  LLVM_ENABLE_RTTI
  LLVM_INCLUDE_DIRS
  LLVM_LIBRARY_DIRS
  TARGET_TRIPLE
)

foreach (vname ${NEEDED_LLVM_VARS})
  message(STATUS "${vname}: \"${${vname}}\"")
  if (NOT (DEFINED "${vname}"))
    message(FATAL_ERROR "${vname} was not defined")
  endif()
endforeach()

set(OPTIONAL_LLVM_VARS
  LLVM_BUILD_MAIN_SRC_DIR
)
foreach (vname ${OPTIONAL_LLVM_VARS})
  if (${vname})
    message(STATUS "${vname}: \"${${vname}}\"")
  endif()
endforeach()

################################################################################
# Find bitcode compiler
################################################################################
include("${CMAKE_SOURCE_DIR}/../cmake/find_bitcode_compiler.cmake")
message(STATUS "LLVMCC: ${LLVMCC}")
if (NOT EXISTS "${LLVMCC}")
  message(FATAL_ERROR "Cannot find C bitcode compiler \"${LLVMCC}\"")
endif()
message(STATUS "LLVMCXX: ${LLVMCXX}")
if (NOT EXISTS "${LLVMCXX}")
  message(FATAL_ERROR "Cannot find C++ bitcode compiler \"${LLVMCXX}\"")
endif()

# Set the compiler to LLVM's clang/clang++ if specified
if(DEFINED LLVMCC)
  set(CMAKE_C_COMPILER ${LLVMCC} CACHE FILEPATH "C compiler" FORCE)
endif()
if(DEFINED LLVMCXX)
  set(CMAKE_CXX_COMPILER ${LLVMCXX} CACHE FILEPATH "C++ compiler" FORCE)
endif()

# Print LLVM version and location for debugging
message(STATUS "Using LLVM version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_LIBRARY_DIRS: ${LLVM_LIBRARY_DIRS}")

add_library(TraceIRInstrs MODULE TraceIRInstrs.cpp)
set_target_properties(TraceIRInstrs PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)

target_include_directories(TraceIRInstrs PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(TraceIRInstrs PRIVATE ${LLVM_DEFINITIONS})
target_link_directories(TraceIRInstrs PRIVATE ${LLVM_LIBRARY_DIRS})

# Optional: build the runtime
add_library(TraceRuntime STATIC TraceRuntime.cpp)
set_target_properties(TraceRuntime PROPERTIES LINKER_LANGUAGE C)

# Note: Set LLVM_DIR to your LLVM build's lib/cmake/llvm directory if not found automatically. 