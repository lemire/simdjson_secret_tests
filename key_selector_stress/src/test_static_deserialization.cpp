#include "test_framework.hpp"
#include "reflection_struct_10.hpp"
#include "reflection_struct_50.hpp"
#include "reflection_struct_100.hpp"


#if SIMDJSON_SUPPORTS_CONCEPTS

#include <optional>
#include <string>

using namespace simdjson;

namespace {

// Manual struct using tag_invoke — exercises custom deserialization without reflection.
struct manual_point {
  double x{};
  double y{};
  std::string_view label{};
};

error_code tag_invoke(deserialize_tag, ondemand::object& obj, manual_point& out) noexcept {
  return obj.for_each<"x", "y", "label">(out.x, out.y, out.label);
}

bool test_manual_tag_invoke_for_each() {
  const char* test_name = "test_manual_tag_invoke_for_each";
  (void)test_name;
  auto json = stress::make_json(R"({"label":"origin","y":2.5,"x":1.5,"noise":0})");
  ondemand::parser parser;
  manual_point pt{};
  STRESS_PARSE_TOP(parser, json, pt);
  STRESS_ASSERT(pt.x == 1.5);
  STRESS_ASSERT(pt.y == 2.5);
  STRESS_ASSERT_SV_EQUAL(pt.label, "origin");
  return true;
}

struct manual_nested {
  uint64_t id{};
  manual_point point{};
};

error_code tag_invoke(deserialize_tag, ondemand::object& obj, manual_nested& out) noexcept {
  return obj.for_each<"id", "point">(
      out.id,
      [&](ondemand::value v) -> error_code {
        ondemand::object inner;
        SIMDJSON_TRY(v.get_object().get(inner));
        return simdjson::deserialize(inner, out.point);
      });
}

bool test_manual_nested_tag_invoke() {
  const char* test_name = "test_manual_nested_tag_invoke";
  (void)test_name;
  auto json = stress::make_json(R"({"point":{"y":3,"x":4,"label":"p"},"id":9})");
  ondemand::parser parser;
  manual_nested n{};
  STRESS_PARSE_TOP(parser, json, n);
  STRESS_ASSERT_EQUAL(n.id, 9u);
  STRESS_ASSERT(n.point.x == 4.0);
  STRESS_ASSERT_SV_EQUAL(n.point.label, "p");
  return true;
}

bool test_manual_optional_fields() {
  const char* test_name = "test_manual_optional_fields";
  (void)test_name;
  auto json = stress::make_json(R"({"name":"Daniel","age":42})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view name{};
  std::optional<uint64_t> age{};
  std::optional<std::string_view> city{};
  auto result = obj.for_each<"name", "age", "city">(
      name,
      [&](ondemand::value v) -> error_code {
        uint64_t n{};
        auto err = v.get(n);
        if (!err) { age = n; }
        return err;
      },
      [&](ondemand::value v) -> error_code {
        std::string_view s;
        auto err = v.get(s);
        if (!err) { city = s; }
        return err;
      });
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 2u);
  STRESS_ASSERT_SV_EQUAL(name, "Daniel");
  STRESS_ASSERT(age.has_value());
  STRESS_ASSERT_EQUAL(*age, 42u);
  STRESS_ASSERT(!city.has_value());
  return true;
}

#if SIMDJSON_STATIC_REFLECTION

#define DEFINE_REFLECTION_TEST(N, LAST_MEMBER) \
  bool test_reflection_deser_##N() { \
    const char* test_name = "test_reflection_deser_" #N; \
    (void)test_name; \
    auto json = stress::make_json(build_reflection_json_##N(false)); \
    ondemand::parser parser; \
    reflected_record_##N rec{}; \
    STRESS_PARSE_TOP(parser, json, rec); \
    STRESS_ASSERT_EQUAL(rec.k000, 7u); \
    STRESS_ASSERT_EQUAL(rec.k001, 8u); \
    STRESS_ASSERT_EQUAL(rec.LAST_MEMBER, static_cast<uint64_t>(N - 1 + 7)); \
    return true; \
  } \
  bool test_reflection_deser_##N##_reversed() { \
    const char* test_name = "test_reflection_deser_" #N "_reversed"; \
    (void)test_name; \
    auto json = stress::make_json(build_reflection_json_##N(true)); \
    ondemand::parser parser; \
    reflected_record_##N rec{}; \
    STRESS_PARSE_TOP(parser, json, rec); \
    STRESS_ASSERT_EQUAL(rec.k000, 7u); \
    STRESS_ASSERT_EQUAL(rec.LAST_MEMBER, static_cast<uint64_t>(N - 1 + 7)); \
    return true; \
  }

DEFINE_REFLECTION_TEST(10, k009)
DEFINE_REFLECTION_TEST(50, k049)
DEFINE_REFLECTION_TEST(100, k099)

bool test_reflection_long_member_fallback() {
  const char* test_name = "test_reflection_long_member_fallback";
  (void)test_name;
  // Member name > 63 chars forces ordered fallback — must still deserialize.
  struct long_name_record {
    uint64_t ok{};
    // This member name is intentionally too long for key_selector (>63 chars).
    uint64_t this_member_name_is_way_too_long_for_the_key_selector_and_should_force_the_ordered_fallback_path{};
  };
  std::string json = R"({"ok":1,"this_member_name_is_way_too_long_for_the_key_selector_and_should_force_the_ordered_fallback_path":99})";
  ondemand::parser parser;
  long_name_record rec{};
  auto padded = stress::make_json(json);
  STRESS_PARSE_TOP(parser, padded, rec);
  STRESS_ASSERT_EQUAL(rec.ok, 1u);
  STRESS_ASSERT_EQUAL(rec.this_member_name_is_way_too_long_for_the_key_selector_and_should_force_the_ordered_fallback_path, 99u);
  return true;
}

#endif // SIMDJSON_STATIC_REFLECTION

} // namespace

void register_static_deserialization_tests() {
  STRESS_RUN(test_manual_tag_invoke_for_each);
  STRESS_RUN(test_manual_nested_tag_invoke);
  STRESS_RUN(test_manual_optional_fields);

#if SIMDJSON_STATIC_REFLECTION
  STRESS_RUN(test_reflection_deser_10);
  STRESS_RUN(test_reflection_deser_10_reversed);
  STRESS_RUN(test_reflection_deser_50);
  STRESS_RUN(test_reflection_deser_50_reversed);
  STRESS_RUN(test_reflection_deser_100);
  STRESS_RUN(test_reflection_deser_100_reversed);
  STRESS_RUN(test_reflection_long_member_fallback);
#endif
}

#endif // SIMDJSON_SUPPORTS_CONCEPTS