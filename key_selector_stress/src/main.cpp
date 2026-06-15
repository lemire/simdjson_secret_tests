#include "test_framework.hpp"
#include "simdjson.h"

#include <iostream>

#if SIMDJSON_SUPPORTS_CONCEPTS
void register_many_keys_tests();
void register_long_keys_tests();
void register_for_each_tests();
void register_static_deserialization_tests();
void register_matcher_tests();
void register_adversarial_tests();
#endif

int main() {
  std::cout << "key_selector stress tests for simdjson PR #2776\n";
  std::cout << "simdjson version: " << SIMDJSON_VERSION << "\n";

#if !SIMDJSON_SUPPORTS_CONCEPTS
  std::cerr << "SIMDJSON_SUPPORTS_CONCEPTS is not enabled — need C++20.\n";
  return 2;
#else
  register_matcher_tests();
  register_for_each_tests();
  register_long_keys_tests();
  register_many_keys_tests();
  register_static_deserialization_tests();
  register_adversarial_tests();

  auto& r = stress::global_runner();
  std::cout << "\n=== summary: " << r.passed << " passed, " << r.failed << " failed ===\n";
  return r.failed == 0 ? 0 : 1;
#endif
}