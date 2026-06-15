#pragma once

#include <random>
#include <string>
#include <vector>

namespace stress::json_builder {

inline std::string object(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out = "{";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i) out += ",";
    out += "\"" + fields[i].first + "\":" + fields[i].second;
  }
  out += "}";
  return out;
}

inline std::string quoted(std::string_view s) {
  return "\"" + std::string(s) + "\"";
}

inline std::string repeat_char(char c, std::size_t n) {
  return std::string(n, c);
}

inline std::string shuffled_object(
    const std::vector<std::pair<std::string, std::string>>& fields,
    uint64_t seed) {
  std::vector<std::size_t> order(fields.size());
  for (std::size_t i = 0; i < fields.size(); ++i) order[i] = i;
  std::mt19937_64 rng(seed);
  std::shuffle(order.begin(), order.end(), rng);
  std::vector<std::pair<std::string, std::string>> shuffled;
  shuffled.reserve(fields.size());
  for (auto idx : order) shuffled.push_back(fields[idx]);
  return object(shuffled);
}

} // namespace stress::json_builder