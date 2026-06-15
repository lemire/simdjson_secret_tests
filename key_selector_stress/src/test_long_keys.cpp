#include "test_framework.hpp"
#include "long_keys.hpp"

#if SIMDJSON_SUPPORTS_CONCEPTS

#include <string>

using namespace simdjson;

namespace {

bool test_long_keys_for_each_reversed() {
  const char* test_name = "test_long_keys_for_each_reversed";
  (void)test_name;
  auto json = stress::make_json(generated_long_keys::build_json_reversed());
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view a63, b63, blocks, pref, sn, jo, joe;
  uint64_t id{};
  using Sel = generated_long_keys::selector;
  auto result = obj.for_each<Sel>(a63, b63, blocks, pref, id, sn, jo, joe);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 8u);
  STRESS_ASSERT_SV_EQUAL(a63, "AVAL");
  STRESS_ASSERT_SV_EQUAL(b63, "BVAL");
  STRESS_ASSERT_SV_EQUAL(blocks, "BLOCKS");
  STRESS_ASSERT_SV_EQUAL(pref, "PREF");
  STRESS_ASSERT_EQUAL(id, 42u);
  STRESS_ASSERT_SV_EQUAL(sn, "lemire");
  STRESS_ASSERT_SV_EQUAL(jo, "JO");
  STRESS_ASSERT_SV_EQUAL(joe, "JOE");
  return true;
}

bool test_long_keys_joe_prefix_miss() {
  const char* test_name = "test_long_keys_joe_prefix_miss";
  (void)test_name;
  auto json = stress::make_json(generated_long_keys::build_json_joe_only());
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view a63, b63, blocks, pref, sn, jo, joe;
  uint64_t id{};
  using Sel = generated_long_keys::selector;
  auto result = obj.for_each<Sel>(a63, b63, blocks, pref, id, sn, jo, joe);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 1u);
  STRESS_ASSERT_SV_EQUAL(joe, "only");
  return true;
}

bool test_long_keys_matcher_boundaries() {
  const char* test_name = "test_long_keys_matcher_boundaries";
  (void)test_name;
  using Sel = generated_long_keys::selector;
  STRESS_ASSERT_EQUAL(Sel::max_key_len, 63u);

  // Truncated 63-char key (62 chars) must miss.
  std::string almost_a(62, 'a');
  STRESS_ASSERT_EQUAL(stress::probe_match_len<Sel>(almost_a), Sel::size());

  // Extended key (64 chars) must miss — length alone rejects before window compare.
  std::string too_long(64, 'a');
  STRESS_ASSERT_EQUAL(Sel::match_raw(too_long.data(), too_long.size()), Sel::size());

  // Keys differing only at interior offsets must not alias.
  std::string k0 = std::string(16, 'c') + std::string(16, 'd') + std::string(16, 'e') + std::string(15, 'f');
  std::string k1 = std::string(16, 'c') + std::string(16, 'd') + std::string(16, 'e') + std::string(14, 'f') + "g";
  STRESS_ASSERT_EQUAL(k0.size(), 63u);
  STRESS_ASSERT_EQUAL(k1.size(), 63u);
  STRESS_ASSERT(stress::probe_match_len<Sel>(k0) != stress::probe_match_len<Sel>(k1));
  return true;
}

bool test_long_keys_near_boundary_lengths() {
  const char* test_name = "test_long_keys_near_boundary_lengths";
  (void)test_name;
  using Sel = generated_long_keys::selector;
  for (std::size_t len : {16, 17, 31, 32, 33, 48, 63}) {
    std::string key(len, 'a');
    auto idx = stress::probe_match_len<Sel>(key);
    if (len == 63) {
      STRESS_ASSERT_EQUAL(idx, 0u);
    } else {
      STRESS_ASSERT_EQUAL(idx, Sel::size());
    }
  }
  return true;
}

} // namespace

void register_long_keys_tests() {
  STRESS_RUN(test_long_keys_for_each_reversed);
  STRESS_RUN(test_long_keys_joe_prefix_miss);
  STRESS_RUN(test_long_keys_matcher_boundaries);
  STRESS_RUN(test_long_keys_near_boundary_lengths);
}

#endif // SIMDJSON_SUPPORTS_CONCEPTS