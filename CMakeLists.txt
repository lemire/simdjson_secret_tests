cmake_minimum_required(VERSION 3.14)
project(simdjson_unpadded_fuzzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ON)

# Download CPM.cmake at configure time
set(CPM_DOWNLOAD_VERSION 0.38.3)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  message(STATUS "Downloading CPM.cmake")
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
  )
endif()
include(${CPM_DOWNLOAD_LOCATION})

# Fetch simdjson from the PR branch (experimental parse_unpadded)
# Using the branch so we test exactly what the PR proposes.
CPMAddPackage(
  NAME simdjson
  GITHUB_REPOSITORY simdjson/simdjson
  GIT_TAG lemire/nopad
  OPTIONS
    "SIMDJSON_BUILD_TESTS OFF"
    "SIMDJSON_DEVELOPMENT_CHECKS OFF"
    "SIMDJSON_ENABLE_NAN_INF ON"   # include the NaN/inf paths in testing if present
)

if(NOT TARGET simdjson)
  # Fallback alias used by some configurations
  if(TARGET simdjson::simdjson)
    add_library(simdjson ALIAS simdjson::simdjson)
  endif()
endif()

add_executable(unpadded_fuzzer
  fuzzer.cpp
)

target_link_libraries(unpadded_fuzzer PRIVATE simdjson)

if(ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(unpadded_fuzzer PRIVATE
      -fsanitize=address
      -fsanitize=undefined
      -fno-omit-frame-pointer
      -g
    )
    target_link_options(unpadded_fuzzer PRIVATE
      -fsanitize=address
      -fsanitize=undefined
    )
    message(STATUS "Sanitizers enabled for unpadded_fuzzer (ASAN + UBSAN)")
  else()
    message(WARNING "Sanitizers requested but not supported for this compiler")
  endif()
endif()

# Also build a small helper to print simdjson version/capabilities if useful
target_compile_definitions(unpadded_fuzzer PRIVATE
  SIMDJSON_FUZZER=1
)

# Convenience: allow running directly from build dir
set_target_properties(unpadded_fuzzer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
