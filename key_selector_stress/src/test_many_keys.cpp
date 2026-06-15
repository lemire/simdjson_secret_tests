#include "test_framework.hpp"

#if SIMDJSON_SUPPORTS_CONCEPTS

#include "many_keys_10.hpp"
#include "many_keys_50.hpp"
#include "many_keys_100.hpp"
#include "many_keys_200.hpp"
#include "many_keys_250.hpp"

#include <array>
#include <cstdint>

using namespace simdjson;

namespace {

#define DEFINE_MANY_KEYS_TESTS(N) \
  bool test_many_keys_##N##_reversed() { \
    const char* test_name = "test_many_keys_" #N "_reversed"; \
    (void)test_name; \
    auto json = stress::make_json(generated_many_keys_##N::build_json_ordered(true)); \
    ondemand::parser parser; \
    ondemand::object obj; \
    STRESS_PARSE_OBJECT(parser, json, obj); \
    std::size_t callbacks = 0; \
    using Sel = generated_many_keys_##N::selector; \
    auto result = obj.for_each<Sel>([&](std::size_t, ondemand::value) { ++callbacks; }); \
    STRESS_ASSERT_SUCCESS(result.error); \
    STRESS_ASSERT_EQUAL(result.matched_count, static_cast<std::size_t>(N)); \
    STRESS_ASSERT_EQUAL(callbacks, static_cast<std::size_t>(N)); \
    return true; \
  } \
  bool test_many_keys_##N##_shuffled() { \
    const char* test_name = "test_many_keys_" #N "_shuffled"; \
    (void)test_name; \
    auto json = stress::make_json(generated_many_keys_##N::build_json_shuffled(0xDEADBEEF)); \
    ondemand::parser parser; \
    ondemand::object obj; \
    STRESS_PARSE_OBJECT(parser, json, obj); \
    std::array<uint64_t, N> got{}; \
    using Sel = generated_many_keys_##N::selector; \
    auto result = obj.for_each<Sel>([&](std::size_t idx, ondemand::value v) -> error_code { \
      return v.get(got[idx]); \
    }); \
    STRESS_ASSERT_SUCCESS(result.error); \
    STRESS_ASSERT_EQUAL(result.matched_count, static_cast<std::size_t>(N)); \
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) { \
      STRESS_ASSERT_EQUAL(got[i], static_cast<uint64_t>(i + 1000)); \
    } \
    return true; \
  } \
  bool test_many_keys_##N##_noise() { \
    const char* test_name = "test_many_keys_" #N "_noise"; \
    (void)test_name; \
    auto json = stress::make_json(generated_many_keys_##N::build_json_with_noise(static_cast<std::size_t>(N) * 2)); \
    ondemand::parser parser; \
    ondemand::object obj; \
    STRESS_PARSE_OBJECT(parser, json, obj); \
    using Sel = generated_many_keys_##N::selector; \
    auto result = obj.for_each<Sel>([&](std::size_t idx, ondemand::value v) -> error_code { \
      uint64_t n{}; \
      auto err = v.get(n); \
      if (err) { return err; } \
      if (n != static_cast<uint64_t>(idx * 3)) { return INCORRECT_TYPE; } \
      return SUCCESS; \
    }); \
    STRESS_ASSERT_SUCCESS(result.error); \
    STRESS_ASSERT_EQUAL(result.matched_count, static_cast<std::size_t>(N)); \
    return true; \
  } \
  bool test_many_keys_##N##_duplicates() { \
    const char* test_name = "test_many_keys_" #N "_duplicates"; \
    (void)test_name; \
    auto json = stress::make_json(generated_many_keys_##N::build_json_with_duplicates()); \
    ondemand::parser parser; \
    ondemand::object obj; \
    STRESS_PARSE_OBJECT(parser, json, obj); \
    using Sel = generated_many_keys_##N::selector; \
    std::array<uint64_t, N> got{}; \
    auto result = obj.for_each<Sel>([&](std::size_t idx, ondemand::value v) -> error_code { \
      return v.get(got[idx]); \
    }); \
    STRESS_ASSERT_SUCCESS(result.error); \
    STRESS_ASSERT_EQUAL(result.matched_count, static_cast<std::size_t>(N)); \
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) { \
      STRESS_ASSERT_EQUAL(got[i], static_cast<uint64_t>(i)); \
    } \
    return true; \
  } \
  bool test_many_keys_##N##_partial() { \
    const char* test_name = "test_many_keys_" #N "_partial"; \
    (void)test_name; \
    constexpr std::size_t present = (N > 5) ? (N / 2) : (N - 1); \
    auto json = stress::make_json(generated_many_keys_##N::build_json_partial(present)); \
    ondemand::parser parser; \
    ondemand::object obj; \
    STRESS_PARSE_OBJECT(parser, json, obj); \
    using Sel = generated_many_keys_##N::selector; \
    auto result = obj.for_each<Sel>([&](std::size_t, ondemand::value) {}); \
    STRESS_ASSERT_SUCCESS(result.error); \
    STRESS_ASSERT_EQUAL(result.matched_count, present); \
    return true; \
  }

DEFINE_MANY_KEYS_TESTS(10)
DEFINE_MANY_KEYS_TESTS(50)
DEFINE_MANY_KEYS_TESTS(100)
DEFINE_MANY_KEYS_TESTS(200)
DEFINE_MANY_KEYS_TESTS(250)

bool test_many_keys_10_direct_binding() {
  const char* test_name = "test_many_keys_10_direct_binding";
  (void)test_name;
  auto json = stress::make_json(generated_many_keys_10::build_json_ordered(false));
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  uint64_t k000{}, k001{}, k002{}, k003{}, k004{}, k005{}, k006{}, k007{}, k008{}, k009{};
  auto result = obj.for_each<generated_many_keys_10::selector>(
      k000, k001, k002, k003, k004, k005, k006, k007, k008, k009);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 10u);
  STRESS_ASSERT_EQUAL(k000, 0u);
  STRESS_ASSERT_EQUAL(k009, 9u);
  return true;
}

bool test_many_keys_250_selector_properties() {
  const char* test_name = "test_many_keys_250_selector_properties";
  (void)test_name;
  using Sel = generated_many_keys_250::selector;
  STRESS_ASSERT_EQUAL(Sel::size(), 250u);
  STRESS_ASSERT_SV_EQUAL(Sel::key_at(0), "f00_000");
  STRESS_ASSERT_SV_EQUAL(Sel::key_at(249), "ff9_249");
  STRESS_ASSERT_EQUAL(stress::probe_match_len<Sel>("f00_000"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match_len<Sel>("ff9_249"), 249u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("k999"), Sel::size());
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("noise"), Sel::size());
  return true;
}

bool test_many_keys_250_early_stop() {
  const char* test_name = "test_many_keys_250_early_stop";
  (void)test_name;
  std::string json = generated_many_keys_250::build_json_ordered(false);
  json.pop_back();
  for (std::size_t i = 0; i < 500; ++i) {
    json += ",\"tail_" + std::to_string(i) + "\":" + std::to_string(i);
  }
  json += "}";

  auto padded = stress::make_json(json);
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, padded, obj);

  std::size_t fields_visited = 0;
  using Sel = generated_many_keys_250::selector;
  auto result = obj.for_each<Sel>([&](std::size_t, ondemand::value) { ++fields_visited; });
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 250u);
  STRESS_ASSERT(fields_visited == 250u);
  return true;
}

} // namespace

void register_many_keys_tests() {
  STRESS_RUN(test_many_keys_10_reversed);
  STRESS_RUN(test_many_keys_10_shuffled);
  STRESS_RUN(test_many_keys_10_noise);
  STRESS_RUN(test_many_keys_10_duplicates);
  STRESS_RUN(test_many_keys_10_partial);
  STRESS_RUN(test_many_keys_10_direct_binding);

  STRESS_RUN(test_many_keys_50_reversed);
  STRESS_RUN(test_many_keys_50_shuffled);
  STRESS_RUN(test_many_keys_50_noise);
  STRESS_RUN(test_many_keys_50_duplicates);
  STRESS_RUN(test_many_keys_50_partial);

  STRESS_RUN(test_many_keys_100_reversed);
  STRESS_RUN(test_many_keys_100_shuffled);
  STRESS_RUN(test_many_keys_100_noise);
  STRESS_RUN(test_many_keys_100_duplicates);
  STRESS_RUN(test_many_keys_100_partial);

  STRESS_RUN(test_many_keys_200_reversed);
  STRESS_RUN(test_many_keys_200_shuffled);
  STRESS_RUN(test_many_keys_200_noise);
  STRESS_RUN(test_many_keys_200_duplicates);
  STRESS_RUN(test_many_keys_200_partial);

  STRESS_RUN(test_many_keys_250_reversed);
  STRESS_RUN(test_many_keys_250_shuffled);
  STRESS_RUN(test_many_keys_250_noise);
  STRESS_RUN(test_many_keys_250_duplicates);
  STRESS_RUN(test_many_keys_250_partial);
  STRESS_RUN(test_many_keys_250_selector_properties);
  STRESS_RUN(test_many_keys_250_early_stop);
}

#endif // SIMDJSON_SUPPORTS_CONCEPTS