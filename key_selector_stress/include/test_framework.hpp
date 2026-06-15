#pragma once

#include "simdjson.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace stress {

inline simdjson::padded_string make_json(std::string_view sv) {
  return simdjson::padded_string(sv);
}

inline simdjson::error_code parse_top_object(
    simdjson::ondemand::parser& parser,
    simdjson::padded_string& json,
    simdjson::ondemand::document& doc,
    simdjson::ondemand::object& obj) {
  auto doc_result = parser.iterate(json);
  SIMDJSON_TRY(std::move(doc_result).get(doc));
  return doc.get_object().get(obj);
}

template <typename T>
inline simdjson::error_code parse_top(
    simdjson::ondemand::parser& parser,
    simdjson::padded_string& json,
    simdjson::ondemand::document& doc,
    T& out) {
  auto doc_result = parser.iterate(json);
  SIMDJSON_TRY(std::move(doc_result).get(doc));
  return doc.get(out);
}

namespace detail {
inline std::string make_quoted_padded_key(std::string_view key) {
  std::string storage(key);
  storage.push_back('"');
  storage.append(simdjson::SIMDJSON_PADDING, ' ');
  return storage;
}
} // namespace detail

// Probe match_raw the way simdjson does at runtime: key bytes followed by closing quote.
template <typename Selector>
inline std::size_t probe_match(std::string_view key) {
  auto storage = detail::make_quoted_padded_key(key);
  simdjson::ondemand::raw_json_string rjs(
      reinterpret_cast<const uint8_t*>(storage.data()));
  return Selector::match_raw(rjs);
}

// Length-known overload: p[len] must be the closing quote in a padded buffer.
template <typename Selector>
inline std::size_t probe_match_len(std::string_view key) {
  auto storage = detail::make_quoted_padded_key(key);
  return Selector::match_raw(storage.data(), key.size());
}

struct runner {
  int passed = 0;
  int failed = 0;

  void pass(std::string_view name) {
    ++passed;
    std::cout << "  PASS " << name << "\n";
  }

  void fail(std::string_view name, const char* file, int line, std::string_view msg) {
    ++failed;
    std::cerr << "  FAIL " << name << " at " << file << ":" << line << " — " << msg << "\n";
  }
};

inline runner& global_runner() {
  static runner r;
  return r;
}

#define STRESS_ASSERT_MSG(cond, msg) \
  do { \
    if (!(cond)) { \
      stress::global_runner().fail(test_name, __FILE__, __LINE__, msg); \
      return false; \
    } \
  } while (0)

#define STRESS_ASSERT(cond) STRESS_ASSERT_MSG(cond, #cond)

#define STRESS_ASSERT_EQUAL(a, b) \
  do { \
    auto _a = (a); \
    auto _b = (b); \
    if (!(_a == _b)) { \
      stress::global_runner().fail(test_name, __FILE__, __LINE__, \
        std::string("expected ") + std::to_string(_b) + ", got " + std::to_string(_a)); \
      return false; \
    } \
  } while (0)

#define STRESS_ASSERT_SV_EQUAL(a, b) \
  do { \
    auto _a = (a); \
    auto _b = (b); \
    if (_a != _b) { \
      stress::global_runner().fail(test_name, __FILE__, __LINE__, "string_view mismatch"); \
      return false; \
    } \
  } while (0)

#define STRESS_ASSERT_SUCCESS(ec) \
  do { \
    auto _ec = (ec); \
    if (_ec != simdjson::SUCCESS) { \
      stress::global_runner().fail(test_name, __FILE__, __LINE__, \
        std::string("expected SUCCESS, got ") + simdjson::error_message(_ec)); \
      return false; \
    } \
  } while (0)

#define STRESS_ASSERT_ERROR(ec, expected) \
  do { \
    auto _ec = (ec); \
    if (_ec != (expected)) { \
      stress::global_runner().fail(test_name, __FILE__, __LINE__, \
        std::string("expected ") + simdjson::error_message(expected) + ", got " + simdjson::error_message(_ec)); \
      return false; \
    } \
  } while (0)

#define STRESS_RUN(test_fn) \
  do { \
    const char* test_name = #test_fn; \
    std::cout << ">> " << test_name << "\n"; \
    if (test_fn()) { \
      stress::global_runner().pass(test_name); \
    } \
  } while (0)

// Keep document alive for the lifetime of any object/value views extracted from it.
#define STRESS_PARSE_OBJECT(parser, json, obj) \
  simdjson::ondemand::document STRESS_DOC{}; \
  STRESS_ASSERT_SUCCESS(stress::parse_top_object(parser, json, STRESS_DOC, obj))

#define STRESS_PARSE_TOP(parser, json, out) \
  simdjson::ondemand::document STRESS_DOC{}; \
  STRESS_ASSERT_SUCCESS(stress::parse_top(parser, json, STRESS_DOC, out))

} // namespace stress