#include "test_framework.hpp"
#include "json_builder.hpp"

#if SIMDJSON_SUPPORTS_CONCEPTS

#include <string>

using namespace simdjson;

namespace {

bool test_for_each_index_callback() {
  const char* test_name = "test_for_each_index_callback";
  (void)test_name;
  auto json = stress::make_json(R"({"city":"Montreal","name":"Daniel","age":42,"extra":1})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  using fields = ondemand::key_selector<"name", "city", "age">;
  std::string_view name, city;
  uint64_t age{};
  auto walk = obj.for_each<fields>([&](std::size_t i, ondemand::value v) {
    switch (i) {
      case 0: name = std::string_view(v); break;
      case 1: city = std::string_view(v); break;
      case 2: age = uint64_t(v); break;
    }
  });
  STRESS_ASSERT_SUCCESS(walk.error);
  STRESS_ASSERT_SV_EQUAL(name, "Daniel");
  STRESS_ASSERT_SV_EQUAL(city, "Montreal");
  STRESS_ASSERT_EQUAL(age, 42u);
  return true;
}

bool test_for_each_mixed_target_lambda() {
  const char* test_name = "test_for_each_mixed_target_lambda";
  (void)test_name;
  auto json = stress::make_json(R"({"name":"Daniel","age":42})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view name;
  uint64_t doubled{};
  auto mixed = obj.for_each<"name", "age">(
      name,
      [&](ondemand::value v) { doubled = 2 * uint64_t(v); });
  STRESS_ASSERT_SUCCESS(mixed.error);
  STRESS_ASSERT_SV_EQUAL(name, "Daniel");
  STRESS_ASSERT_EQUAL(doubled, 84u);
  return true;
}

bool test_for_each_error_propagation() {
  const char* test_name = "test_for_each_error_propagation";
  (void)test_name;
  auto json = stress::make_json(R"({"id":7,"name":"Daniel"})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  auto bad = obj.for_each<ondemand::key_selector<"id", "name">>(
      [&](std::size_t idx, ondemand::value v) -> error_code {
        std::string_view s;
        if (idx == 0) return v.get(s);
        return SUCCESS;
      });
  STRESS_ASSERT_ERROR(bad.error, INCORRECT_TYPE);
  return true;
}

bool test_for_each_stop_on_lambda_error() {
  const char* test_name = "test_for_each_stop_on_lambda_error";
  (void)test_name;
  auto json = stress::make_json(R"({"name":"Daniel","age":42,"city":"Montreal"})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  bool ran_name = false, ran_age = false, ran_city = false;
  auto result = obj.for_each<ondemand::key_selector<"name", "city", "age">>(
      [&](ondemand::value) -> error_code { ran_name = true; return SUCCESS; },
      [&](ondemand::value) -> error_code { ran_city = true; return SUCCESS; },
      [&](ondemand::value) -> error_code { ran_age = true; return INCORRECT_TYPE; });
  STRESS_ASSERT_ERROR(result.error, INCORRECT_TYPE);
  STRESS_ASSERT(ran_name);
  STRESS_ASSERT(ran_age);
  STRESS_ASSERT(!ran_city);
  return true;
}

bool test_for_each_nested() {
  const char* test_name = "test_for_each_nested";
  (void)test_name;
  auto json = stress::make_json(R"({"id":7,"author":{"name":"Lemire","handle":"x"}})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  uint64_t id{};
  std::string_view author_name, author_handle;
  auto nested = obj.for_each<ondemand::key_selector<"id", "author">>(
      id,
      [&](ondemand::value v) -> error_code {
        ondemand::object author;
        SIMDJSON_TRY(v.get_object().get(author));
        auto inner = author.for_each<"name", "handle">(author_name, author_handle);
        return inner.error;
      });
  STRESS_ASSERT_SUCCESS(nested.error);
  STRESS_ASSERT_EQUAL(id, 7u);
  STRESS_ASSERT_SV_EQUAL(author_name, "Lemire");
  STRESS_ASSERT_SV_EQUAL(author_handle, "x");
  return true;
}

bool test_for_each_result_forwarding() {
  const char* test_name = "test_for_each_result_forwarding";
  (void)test_name;
  auto json = stress::make_json(R"({"name":"Daniel","age":42})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view name;
  uint64_t age{};
  auto result = obj.for_each<"name", "age">(name, age);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 2u);
  STRESS_ASSERT_SV_EQUAL(name, "Daniel");
  STRESS_ASSERT_EQUAL(age, 42u);
  return true;
}

bool test_for_each_single_key() {
  const char* test_name = "test_for_each_single_key";
  (void)test_name;
  auto json = stress::make_json(R"({"only":99,"noise":1})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  uint64_t only{};
  auto result = obj.for_each<"only">(only);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 1u);
  STRESS_ASSERT_EQUAL(only, 99u);
  return true;
}

bool test_for_each_window_path_id_screen_name() {
  const char* test_name = "test_for_each_window_path_id_screen_name";
  (void)test_name;
  using Sel = ondemand::key_selector<"id", "screen_name">;
  static_assert(Sel::window.ok);

  auto json = stress::make_json(R"({"noise":1,"screen_name":"lemire","id":42,"more":2})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  uint64_t id{};
  std::string_view screen_name;
  auto result = obj.for_each<Sel>(id, screen_name);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 2u);
  STRESS_ASSERT_EQUAL(id, 42u);
  STRESS_ASSERT_SV_EQUAL(screen_name, "lemire");
  return true;
}

bool test_for_each_partial_tweets_window() {
  const char* test_name = "test_for_each_partial_tweets_window";
  (void)test_name;
  using tweet_fields = ondemand::key_selector<
      "created_at", "id", "text", "user", "retweet_count", "favorite_count", "in_reply_to_status_id">;
  static_assert(tweet_fields::window.ok);

  auto json = stress::make_json(R"({
    "favorite_count": 1,
    "id": 99,
    "text": "hi",
    "created_at": "now",
    "user": {"id": 1, "screen_name": "x"},
    "retweet_count": 0,
    "in_reply_to_status_id": 0,
    "ignored": true
  })");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view created_at, text;
  uint64_t id{}, retweet_count{}, favorite_count{};
  int64_t in_reply{};
  bool got_user = false;

  auto result = obj.for_each<tweet_fields>(
      created_at,
      id,
      text,
      [&](ondemand::value v) -> error_code {
        ondemand::object user;
        SIMDJSON_TRY(v.get_object().get(user));
        uint64_t uid{};
        std::string_view sn;
        auto inner = user.for_each<"id", "screen_name">(uid, sn);
        if (inner.error) { return inner.error; }
        got_user = (uid == 1 && sn == "x");
        return SUCCESS;
      },
      retweet_count,
      favorite_count,
      in_reply);
  STRESS_ASSERT_SUCCESS(result.error);
  STRESS_ASSERT_EQUAL(result.matched_count, 7u);
  STRESS_ASSERT(got_user);
  return true;
}

bool test_for_each_type_mismatch_direct_binding() {
  const char* test_name = "test_for_each_type_mismatch_direct_binding";
  (void)test_name;
  auto json = stress::make_json(R"({"name":"Daniel","age":42})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);

  std::string_view name, age_as_string;
  auto result = obj.for_each<"name", "age">(name, age_as_string);
  STRESS_ASSERT_ERROR(result.error, INCORRECT_TYPE);
  return true;
}

} // namespace

void register_for_each_tests() {
  STRESS_RUN(test_for_each_index_callback);
  STRESS_RUN(test_for_each_mixed_target_lambda);
  STRESS_RUN(test_for_each_error_propagation);
  STRESS_RUN(test_for_each_stop_on_lambda_error);
  STRESS_RUN(test_for_each_nested);
  STRESS_RUN(test_for_each_result_forwarding);
  STRESS_RUN(test_for_each_single_key);
  STRESS_RUN(test_for_each_window_path_id_screen_name);
  STRESS_RUN(test_for_each_partial_tweets_window);
  STRESS_RUN(test_for_each_type_mismatch_direct_binding);
}

#endif // SIMDJSON_SUPPORTS_CONCEPTS