#include "test_framework.hpp"

#if SIMDJSON_SUPPORTS_CONCEPTS

using namespace simdjson;

namespace {

bool test_matcher_agreement_with_literal() {
  const char* test_name = "test_matcher_agreement_with_literal";
  (void)test_name;
  using Sel = ondemand::key_selector<"alpha", "beta", "gamma">;
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("alpha"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("beta"), 1u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("gamma"), 2u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("alph"), Sel::size());
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("alphaa"), Sel::size());
  return true;
}

bool test_matcher_prefix_collision_jo_joe() {
  const char* test_name = "test_matcher_prefix_collision_jo_joe";
  (void)test_name;
  using Sel = ondemand::key_selector<"jo", "joe">;
  static_assert(Sel::window.ok);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("jo"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("joe"), 1u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("joeb"), Sel::size());
  return true;
}

bool test_matcher_describe_nonempty() {
  const char* test_name = "test_matcher_describe_nonempty";
  (void)test_name;
  using Sel = ondemand::key_selector<"a", "bb", "ccc">;
  auto desc = Sel::describe();
  STRESS_ASSERT(!desc.empty());
  return true;
}

bool test_matcher_slash_in_key() {
  const char* test_name = "test_matcher_slash_in_key";
  (void)test_name;
  using Sel = ondemand::key_selector<"a/b", "c/d">;
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("a/b"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("c/d"), 1u);
  return true;
}

} // namespace

void register_matcher_tests() {
  STRESS_RUN(test_matcher_agreement_with_literal);
  STRESS_RUN(test_matcher_prefix_collision_jo_joe);
  STRESS_RUN(test_matcher_describe_nonempty);
  STRESS_RUN(test_matcher_slash_in_key);
}

#endif // SIMDJSON_SUPPORTS_CONCEPTS