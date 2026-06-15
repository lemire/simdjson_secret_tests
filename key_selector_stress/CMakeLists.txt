cmake_minimum_required(VERSION 3.25)
project(key_selector_stress LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Enable ASAN+UBSAN" ON)
option(ENABLE_REFLECTION "Enable C++26 static reflection tests" ON)

# Generate stress headers before configure/build.
set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
add_custom_target(generate_stress_headers ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_headers.py"
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_adversarial_tests.py"
  BYPRODUCTS
    "${GENERATED_DIR}/many_keys_10.hpp"
    "${GENERATED_DIR}/many_keys_250.hpp"
    "${GENERATED_DIR}/long_keys.hpp"
    "${GENERATED_DIR}/reflection_struct_250.hpp"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating key_selector stress-test headers"
)

# Download CPM.cmake
set(CPM_DOWNLOAD_VERSION 0.38.3)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
    ${CPM_DOWNLOAD_LOCATION}
  )
endif()
include(${CPM_DOWNLOAD_LOCATION})

set(SIMDJSON_OPTIONS
  "SIMDJSON_BUILD_TESTS OFF"
  "SIMDJSON_DEVELOPMENT_CHECKS OFF"
)

if(ENABLE_REFLECTION)
  set(CMAKE_CXX_STANDARD 26)
  list(APPEND SIMDJSON_OPTIONS "SIMDJSON_STATIC_REFLECTION ON")
  message(STATUS "C++26 static reflection tests enabled")
endif()

CPMAddPackage(
  NAME simdjson
  GITHUB_REPOSITORY simdjson/simdjson
  GIT_TAG lemire/selector7
  OPTIONS ${SIMDJSON_OPTIONS}
)

add_executable(key_selector_stress_tests
  src/main.cpp
  src/test_matchers.cpp
  src/test_for_each_scenarios.cpp
  src/test_long_keys.cpp
  src/test_many_keys.cpp
  src/test_static_deserialization.cpp
  src/test_adversarial.cpp
)

add_dependencies(key_selector_stress_tests generate_stress_headers)

target_include_directories(key_selector_stress_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${GENERATED_DIR}"
)

target_link_libraries(key_selector_stress_tests PRIVATE simdjson)

# Large key_selector tables (200–250 similar keys) need a higher constexpr budget.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(key_selector_stress_tests PRIVATE
    -fconstexpr-ops-limit=268435456
    -fconstexpr-depth=1024
  )
endif()

if(ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(key_selector_stress_tests PRIVATE
      -fsanitize=address
      -fsanitize=undefined
      -fno-omit-frame-pointer
      -g
    )
    target_link_options(key_selector_stress_tests PRIVATE
      -fsanitize=address
      -fsanitize=undefined
    )
    message(STATUS "Sanitizers enabled (ASAN + UBSAN)")
  endif()
endif()

set_target_properties(key_selector_stress_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)