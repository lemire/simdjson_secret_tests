#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <unordered_set>

#include "simdjson.h"

using namespace simdjson;

// ---------------- RNG (deterministic, reproducible) ----------------
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
  uint64_t next() {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
  }
  uint32_t below(uint32_t n) { return static_cast<uint32_t>(next() % (n ? n : 1)); }
  uint8_t byte() { return static_cast<uint8_t>(next()); }
  double uniform01() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
  // Fuzzer helpers
  int32_t range(int32_t lo, int32_t hi) { return lo + static_cast<int32_t>(below(hi - lo + 1)); }
  size_t index(size_t n) { return below(static_cast<uint32_t>(n)); }
  bool coin(double p = 0.5) { return uniform01() < p; }
  void fill_random_bytes(std::string& out, size_t n) {
    out.reserve(out.size() + n);
    for (size_t i = 0; i < n; ++i) out.push_back(static_cast<char>(byte()));
  }
  uint8_t biased_byte() {
    // Bias toward bytes that appear in JSON and escapes
    uint32_t v = below(100);
    if (v < 40) return static_cast<uint8_t>(' ' + below(95)); // printable
    if (v < 55) return '"';
    if (v < 65) return '\\';
    if (v < 70) return 'u';
    if (v < 75) return '0' + below(10);
    if (v < 80) return 'x';
    if (v < 85) return 0x00;
    if (v < 90) return 0xFF;
    return byte();
  }
};

// ---------------- Serialization for comparison ----------------
static std::string to_text(dom::element e) {
  std::ostringstream out;
  out << e;
  return out.str();
}

// ---------------- Deep DOM structural equality (more reliable than text) ----------------
static bool dom_equal(dom::element a, dom::element b) {
  dom::element_type ta = a.type();
  dom::element_type tb = b.type();
  if (ta != tb) return false;

  switch (ta) {
    case dom::element_type::BIGINT: // treat like string for comparison purposes (when enabled)
    case dom::element_type::STRING: {
      std::string_view sa, sb;
      if (a.get(sa) || b.get(sb)) return false;
      return sa == sb;
    }
    case dom::element_type::INT64: {
      int64_t ia, ib;
      if (a.get(ia) || b.get(ib)) return false;
      return ia == ib;
    }
    case dom::element_type::UINT64: {
      uint64_t ua, ub;
      if (a.get(ua) || b.get(ub)) return false;
      return ua == ub;
    }
    case dom::element_type::DOUBLE: {
      double da, db;
      if (a.get(da) || b.get(db)) return false;
      // Bitwise or tolerant? For same input we want bitwise where possible.
      // Use memcmp on the doubles to catch NaN/Inf differences precisely.
      return std::memcmp(&da, &db, sizeof(double)) == 0;
    }
    case dom::element_type::BOOL: {
      bool ba, bb;
      if (a.get(ba) || b.get(bb)) return false;
      return ba == bb;
    }
    case dom::element_type::NULL_VALUE:
      return true;
    case dom::element_type::ARRAY: {
      dom::array aa, bb;
      if (a.get(aa) || b.get(bb)) return false;
      if (aa.size() != bb.size()) return false;
      auto ita = aa.begin();
      auto itb = bb.begin();
      for (; ita != aa.end(); ++ita, ++itb) {
        if (!dom_equal(*ita, *itb)) return false;
      }
      return true;
    }
    case dom::element_type::OBJECT: {
      dom::object oa, ob;
      if (a.get(oa) || b.get(ob)) return false;
      if (oa.size() != ob.size()) return false;
      auto ita = oa.begin();
      auto itb = ob.begin();
      for (; ita != oa.end(); ++ita, ++itb) {
        std::string_view ka = ita.key();
        std::string_view kb = itb.key();
        if (ka != kb) return false;
        if (!dom_equal(ita.value(), itb.value())) return false;
      }
      return true;
    }
  }
  return false;
}

// ---------------- Core consistency check ----------------
// Uses *exact-size* buffer for unpadded so ASAN redzone detects overruns.
// Exercises multiple overloads of the new unpadded APIs.
static bool check_consistent(const std::string& json, const char* label) {
  size_t len = json.size();
  size_t mod64 = len % 64;

  // --- Padded reference parse ---
  dom::parser p_padded;
  dom::element e_padded;
  auto err_padded = p_padded.parse(padded_string(json)).get(e_padded);

  // --- Unpadded: primary path (char*, len) using exact vector ---
  std::vector<char> exact(json.begin(), json.end());
  dom::parser p_unpadded1;
  dom::element e_unpadded1;
  auto err_unpadded1 = p_unpadded1.parse_unpadded(exact.data(), exact.size()).get(e_unpadded1);

  bool padded_ok = (err_padded == SUCCESS);
  bool unpadded_ok = (err_unpadded1 == SUCCESS);

  if (padded_ok != unpadded_ok) {
    // One path succeeded and the other failed: this is a real semantic bug for the feature.
    std::cerr << "\n[BUG] Success/failure mismatch between padded and unpadded (" << label << ")\n";
    std::cerr << "  len=" << len << "  len%64=" << mod64 << "\n";
    std::cerr << "  input (printable preview): ";
    for (size_t i = 0; i < std::min<size_t>(len, 256); ++i) {
      unsigned char c = static_cast<unsigned char>(json[i]);
      if (c >= 32 && c < 127) std::cerr << static_cast<char>(c);
      else std::cerr << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)c << std::dec;
    }
    if (len > 256) std::cerr << " ... (+" << (len-256) << ")";
    std::cerr << "\n";

    // Full exact hex dump for exact reproduction
    std::cerr << "  exact hex dump:";
    for (size_t i = 0; i < len; ++i) {
      if (i % 16 == 0) std::cerr << "\n    ";
      unsigned char c = static_cast<unsigned char>(json[i]);
      std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)c << " " << std::dec;
    }
    std::cerr << "\n";

    // Also emit a ready-to-paste C++ literal for the exact input
    std::cerr << "  C++ repro literal:\n";
    std::cerr << "    std::string input = \"";
    for (size_t i = 0; i < len; ++i) {
      unsigned char c = static_cast<unsigned char>(json[i]);
      if (c >= 32 && c < 127 && c != '"' && c != '\\') {
        std::cerr << static_cast<char>(c);
      } else {
        std::cerr << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)c << std::dec;
      }
    }
    std::cerr << "\";   // len=" << len << "\n";

    std::cerr << "  padded err:   " << err_padded << "\n";
    std::cerr << "  unpadded err: " << err_unpadded1 << "\n";
    return false;
  }

  // Both succeeded or both failed.
  // Per request, differing specific error codes on failure are now allowed
  // (the unpadded path has different internal logic for the tail and may classify
  //  certain malformed inputs differently while still correctly rejecting them).

  // Extra API coverage for unpadded (string_view + uint8_t* + into_document)
  {
    dom::parser p2;
    dom::element e2;
    std::string_view sv(exact.data(), exact.size());
    auto e2a = p2.parse_unpadded(sv).get(e2);
    if (e2a != err_unpadded1) {
      std::cerr << "\n[BUG] string_view overload disagreed with char* overload\n";
      return false;
    }

    dom::parser p3;
    dom::element e3;
    const uint8_t* up = reinterpret_cast<const uint8_t*>(exact.data());
    auto e3a = p3.parse_unpadded(up, exact.size()).get(e3);
    if (e3a != err_unpadded1) {
      std::cerr << "\n[BUG] uint8_t* overload disagreed\n";
      return false;
    }

    // parse_into_document_unpadded
    dom::parser p4;
    dom::document doc4;
    dom::element e4;
    auto e4a = p4.parse_into_document_unpadded(doc4, up, exact.size()).get(e4);
    if (e4a != err_unpadded1) {
      std::cerr << "\n[BUG] parse_into_document_unpadded disagreed\n";
      return false;
    }
    if (!err_unpadded1 && !dom_equal(e_unpadded1, e4)) {
      std::cerr << "\n[BUG] parse_into_document_unpadded produced different DOM\n";
      return false;
    }
  }

  if (!padded_ok) {
    // Both failed (specific error codes may legitimately differ for the unpadded tail paths).
    return true;
  }

  // Both succeeded: must be deeply equivalent (and text must match for debug)
  if (!dom_equal(e_padded, e_unpadded1)) {
    std::string a = to_text(e_padded);
    std::string b = to_text(e_unpadded1);
    std::cerr << "\n[BUG] Mismatched DOM (deep equal failed) (" << label << ")\n";
    std::cerr << "  len=" << len << " len%64=" << mod64 << "\n";
    std::cerr << "  padded:   " << a << "\n";
    std::cerr << "  unpadded: " << b << "\n";
    return false;
  }

  // Also require the text form to be identical (catches formatting surprises)
  std::string ta = to_text(e_padded);
  std::string tb = to_text(e_unpadded1);
  if (ta != tb) {
    std::cerr << "\n[BUG] Mismatched serialized text despite deep-equal (" << label << ")\n";
    std::cerr << "  len=" << len << "\n";
    std::cerr << "  padded:   " << ta << "\n";
    std::cerr << "  unpadded: " << tb << "\n";
    return false;
  }

  // Hygiene: reuse the unpadded parser for a subsequent padded parse
  {
    dom::element tmp;
    auto re = p_unpadded1.parse(std::string("{\"re\":\"use\"}")).get(tmp);
    if (re) {
      std::cerr << "\n[BUG] Parser reuse after unpadded parse failed on valid input\n";
      return false;
    }
  }

  return true;
}

// ---------------- Interesting hardcoded cases (from PR coverage + more) ----------------
static const std::vector<std::string> kInterestingCases = {
  // Degenerate / tiny
  "", " ", "\n", "\t", "\r\n", "{", "[", "]", "}", "\"", "1", "0", "true", "false", "null",
  "tru", "fals", "nul", "12.", "-.", "1e", "1e+", "1e-", "-", "+", ".", "e", "E",
  // Root string edge + incomplete escapes
  "\"\"", "\"a\"", "\"\\n\"", "\"\\uD800\"", "\"\\uD800\\uDC00\"", "\"\\u\"", "\"\\uD8\"",
  "\"\\uD800\\u\"", "\"\\uD800\\uD\"", " \"\\uD83D\\uDE0",   // truncated emoji surrogate
  // Long fractional at end (exercises number copy/validate near tail)
  "0." + std::string(80, '9'),
  "1." + std::string(200, '3') + "e-200",
  // String whose closing quote is very close to buffer end, with escapes
  "{\"k\":\"" + std::string(60, 'x') + "\\uD800\\u\"}",
  // Surrogate deep lookahead at chunk boundary (high surrogate + truncated \\u near 64B)
  // Root string case from PR test
  "\"" + std::string(63, 'a') + "\\uD800\\u\"",
  "\"" + std::string(64, 'b') + "\\uD800\\u\"",
  "\"" + std::string(127, 'c') + "\\uD800\\u\"",
  // Object value variant
  "{\"k\":\"" + std::string(63, 'b') + "\\uD800\\u\"}",
  // Mixed real-ish
  R"({"a":1,"b":[true,false,null,{"x":"y\nz"}],"c":3.141592653589793e-10})",
  // Leading BOM + content
  std::string("\xEF\xBB\xBF") + "[1,2,3]",
  // String containing raw BOM bytes
  "\"\xEF\xBB\xBF\"",
  // Large simple array to exercise structural counting
  "[\n" + std::string(300, '1') + std::string(299, ',') + "\n]",
  // Deeply nested to stress tape and depth near end
  std::string(40, '[') + "42" + std::string(40, ']'),
  // Many escapes in one string at the end
  "\"\\uD83D\\uDE00" + std::string(50, 'x') + "\\n\\t\\\\\\\"\"",
  // Control chars and weird whitespace near end
  "{\"\tkey\\u0000\":\"val\\r\\n\"}",
  // NaN / Inf related (when enabled in build)
  "inf", "NaN", "infinity", "[inf, -inf, nan]",
  // Partial atoms at various offsets
  "tr", "fals", "nulll", "truE", "False",
};

// ---------------- Structured JSON generator (good heuristic) ----------------
static void gen_string(Rng& r, std::string& out, int bias_end) {
  out += '"';
  uint32_t n = r.below( (bias_end > 0 ? 80 : 32) );
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t k = r.below(12);
    switch (k) {
      case 0: out += "\\n"; break;
      case 1: out += "\\t"; break;
      case 2: out += "\\\""; break;
      case 3: out += "\\\\"; break;
      case 4: out += "\\/"; break;
      case 5: out += "\\b"; break;
      case 6: out += "\\f"; break;
      case 7: out += "\\r"; break;
      case 8: out += "\\u00e9"; break;               // BMP escape
      case 9: out += "\\uD83D\\uDE00"; break;        // surrogate pair (emoji)
      case 10: out += "\xF0\x9F\x98\x80"; break;     // raw 4-byte UTF-8
      default: out += static_cast<char>('a' + r.below(26)); break;
    }
  }
  out += '"';
}

static void gen_number(Rng& r, std::string& out, bool force_at_end) {
  // Bias toward values whose significant bytes are near the buffer tail
  uint32_t kind = r.below(9);
  switch (kind) {
    case 0: out += std::to_string(r.below(1000000)); break;
    case 1: out += "-"; out += std::to_string(r.below(100000000)); break;
    case 2: {
      out += std::to_string(r.below(1000));
      out += '.';
      uint32_t frac = r.below( force_at_end ? 120 : 40 );
      out.append(frac, '0' + (1 + r.below(9)));
      break;
    }
    case 3: {
      out += "0.";
      uint32_t frac = r.below( force_at_end ? 120 : 30 );
      out.append(frac, '0' + (1 + r.below(9)));
      break;
    }
    case 4: {
      out += std::to_string(r.below(100));
      out += (r.below(2) ? 'e' : 'E');
      out += (r.below(2) ? '+' : '-');
      out += std::to_string(r.below(20));
      break;
    }
    case 5: out += "1.7976931348623157e308"; break; // near double limit
    case 6: out += "18446744073709551615"; break;
    case 7: out += "-9223372036854775808"; break;
    default: out += std::to_string(r.next() | (uint64_t(r.below(1u<<16))<<48)); break;
  }
}

static void gen_value(Rng& r, std::string& out, int depth, bool force_leaf_at_end);

static void gen_object(Rng& r, std::string& out, int depth, bool force_leaf_at_end) {
  out += '{';
  uint32_t n = r.below( (depth <= 0 ? 3 : 7) );
  for (uint32_t i = 0; i < n; ++i) {
    if (i) out += ',';
    gen_string(r, out, /*bias_end=*/0);
    out += ':';
    gen_value(r, out, depth - 1, (i + 1 == n) && force_leaf_at_end);
  }
  out += '}';
}

static void gen_array(Rng& r, std::string& out, int depth, bool force_leaf_at_end) {
  out += '[';
  uint32_t n = r.below( (depth <= 0 ? 4 : 8) );
  for (uint32_t i = 0; i < n; ++i) {
    if (i) out += ',';
    gen_value(r, out, depth - 1, (i + 1 == n) && force_leaf_at_end);
  }
  out += ']';
}

static void gen_value(Rng& r, std::string& out, int depth, bool force_leaf_at_end) {
  // At depth <=0 prefer scalars so we terminate with content near the end
  uint32_t choice = (depth <= 0) ? (3u + r.below(4)) : r.below(7);
  switch (choice) {
    case 0: gen_object(r, out, depth, force_leaf_at_end); break;
    case 1: gen_array(r, out, depth, force_leaf_at_end); break;
    case 2: gen_string(r, out, force_leaf_at_end ? 2 : 0); break;
    case 3: gen_number(r, out, force_leaf_at_end); break;
    case 4: out += (r.below(2) ? "true" : "false"); break;
    default: out += "null"; break;
  }
}

static std::string generate_structured(Rng& r) {
  std::string json;
  // Sometimes wrap to push interesting payload to the end
  bool wrap = r.below(3) != 0;
  if (wrap) {
    // 70% array or object wrapper so last element is "at the end"
    if (r.below(2)) {
      gen_array(r, json, 3, /*force_leaf_at_end=*/true);
    } else {
      gen_object(r, json, 3, /*force_leaf_at_end=*/true);
    }
  } else {
    gen_value(r, json, 3, /*force_leaf_at_end=*/true);
  }
  // Occasionally add trailing whitespace (legal, exercises atom-at-end after ws)
  if (r.below(5) == 0) {
    uint32_t ws = r.below(6);
    for (uint32_t i = 0; i < ws; ++i) {
      char wschars[] = " \t\n\r";
      json += wschars[r.below(4)];
    }
  }
  return json;
}

// ---------------- Sophisticated generators for hard-to-reach over-read paths ----------------

// Generate a "dangerous" suffix that stresses string/number/atom parsing near the very end.
static void append_dangerous_tail(Rng& r, std::string& out, size_t desired_tail_len) {
  // We want many different ways the last 1..256 bytes can look for the unpadded parser.
  // Focus: escapes, incomplete \u sequences, surrogate pairs (complete and broken),
  // very long fractional numbers, atoms that are truncated or almost valid,
  // strings that contain what look like structural closers, raw high bytes, etc.
  std::string tail;
  tail.reserve(desired_tail_len + 32);

  uint32_t strategy = r.below(18);
  switch (strategy) {
    case 0: { // long fractional digits right at the end
      tail += "0.";
      tail.append(desired_tail_len > 3 ? desired_tail_len - 3 : 10, '9' - (r.below(3)));
      break;
    }
    case 1: { // string with many mixed escapes ending at buffer end
      tail += '"';
      size_t n = desired_tail_len > 5 ? desired_tail_len - 4 : 20;
      for (size_t i = 0; i < n; ++i) {
        uint32_t k = r.below(10);
        if (k == 0) tail += "\\uD800";
        else if (k == 1) tail += "\\uDC00";
        else if (k == 2) tail += "\\uD83D\\uDE00";
        else if (k == 3) tail += "\\\\\\\"\"\\n\\t";
        else if (k == 4) tail += "\xF0\x9F\x98\x80\xF0\x9F"; // raw utf8 + partial
        else tail += static_cast<char>('a' + r.below(26));
      }
      tail += '"';
      break;
    }
    case 2: { // root-level almost-number or big int at end
      tail.append(desired_tail_len > 2 ? desired_tail_len - 1 : 30, '1' + r.below(8));
      if (r.coin(0.3)) tail += "e-" + std::to_string(r.below(300));
      break;
    }
    case 3: { // truncated high surrogate + partial \u at very end (the PR's special case)
      tail += '"';
      tail.append(r.below(80), r.coin() ? 'x' : 'y');
      tail += "\\uD800\\u";
      // pad or truncate to desired size roughly
      while (tail.size() < desired_tail_len) tail += 'z';
      if (tail.size() > desired_tail_len) tail.resize(desired_tail_len);
      tail += '"';
      break;
    }
    case 4: { // object with last value being a long escaped string
      tail += "{\"last\":\"";
      size_t n = desired_tail_len > 20 ? desired_tail_len - 12 : 40;
      for (size_t i=0; i<n; ++i) {
        if (r.below(7)==0) tail += "\\u00" + std::string(1, "0123456789abcdef"[r.below(16)]) + std::string(1, "0123456789abcdef"[r.below(16)]);
        else if (r.below(5)==0) tail += "\\\\";
        else tail.push_back('A' + r.below(26));
      }
      tail += "\"}";
      break;
    }
    case 5: { // array ending with a huge float
      tail += "[1,2,3.1415926535";
      tail.append(desired_tail_len > 20 ? desired_tail_len-20 : 60 , '1');
      tail += "]";
      break;
    }
    case 6: { // many backslashes (escape state machine stress)
      tail += '"';
      tail.append(std::min<size_t>(desired_tail_len-2, 180), '\\');
      tail += '"';
      break;
    }
    case 7: { // partial true/false/null right at end, preceded by junk
      tail.append(r.below(30), ' ');
      const char* atoms[] = {"tr", "tru", "truex", "f", "fa", "fal", "falsee", "n", "nu", "nul", "nullz", "inf", "nan", "infinity"};
      tail += atoms[r.below(14)];
      break;
    }
    default: {
      // Random-ish bytes biased to JSON + controls in the tail
      tail += '"';
      for (size_t i = 0; i < desired_tail_len/2 + 2; ++i) {
        tail.push_back(static_cast<char>(r.biased_byte()));
      }
      tail += '"';
      while (tail.size() < desired_tail_len) tail.push_back(r.biased_byte());
      if (tail.size() > desired_tail_len) tail.resize(desired_tail_len);
      break;
    }
  }

  // Trim or pad
  if (tail.size() > desired_tail_len) tail.resize(desired_tail_len);
  while (tail.size() < desired_tail_len) tail.push_back(r.byte() & 0x7f);

  out += tail;
}

// Generate documents deliberately crafted so interesting content is in the last N bytes.
// This directly attacks the safe-string / root-number / remaining_len paths.
static std::string generate_tail_stress(Rng& r, size_t max_total) {
  std::string doc;
  // Small "harmless" prefix to vary the starting alignment
  size_t prefix = r.below(64);
  for (size_t i = 0; i < prefix; ++i) {
    doc.push_back(" \t\n\r"[r.below(4)]);
  }

  // Main body (object or array) that ends with a dangerous tail
  bool as_object = r.coin(0.6);
  if (as_object) {
    doc += "{\"a\":1,\"b\":[true, false, null, 123],\"tail\":";
  } else {
    doc += "[1,2,3,true,false,null,\"x\",";
  }

  size_t room_for_tail = (max_total > doc.size() + 10) ? (max_total - doc.size() - 2) : 128;
  size_t tail_len = r.range(8, static_cast<int32_t>(std::min<size_t>(room_for_tail, 512)));

  if (r.coin(0.7)) {
    // Put a long string or number as the final value
    if (r.coin(0.6)) {
      doc += '"';
      // filler inside string
      size_t filler = tail_len > 20 ? tail_len - 15 : 10;
      doc.append(filler, 'Q' + r.below(10));
      // now the dangerous part inside the string
      append_dangerous_tail(r, doc, 12 + r.below(40));
      doc += '"';
    } else {
      append_dangerous_tail(r, doc, tail_len);
    }
  } else {
    append_dangerous_tail(r, doc, tail_len);
  }

  if (as_object) doc += "}";
  else doc += "]";

  if (doc.size() > max_total) doc.resize(max_total);
  return doc;
}

// Try to land the end of the document (especially string close or number digits) at many
// different positions relative to 64-byte block boundaries.
static std::string generate_block_boundary_variants(Rng& r, size_t base_len, int desired_remainder) {
  // desired_remainder is the intended len % 64 we want the final document to have.
  // We accept negative inputs for "a bit before a boundary".
  int mod = desired_remainder;
  while (mod < 0) mod += 64;
  mod %= 64;

  std::string doc;
  // Build a document whose length we will adjust to land on the desired remainder.
  size_t target = (base_len & ~size_t(63)) + mod;
  if (target < 32) target = 32 + mod;

  doc.append(r.below(25), ' ');

  // Container whose last value will contain the hot content
  doc += "{\"z\":";

  size_t current = doc.size();
  size_t desired_suffix = (target > current + 8) ? (target - current - 2) : 48;

  // Longish string with surrogate/escapes near its own end (thus near document end)
  doc += '"';
  doc.append(r.below(15), 's');
  if (r.coin(0.6)) {
    doc += "\\uD800\\uDC00\\uD83D\\uDE00";
  } else {
    doc += "\\n\\t\\\\\\\"";
  }
  size_t rem = (desired_suffix > doc.size()) ? (desired_suffix - doc.size()) : 8;
  doc.append(rem, 'E');
  doc += '"';
  doc += "}";

  // Adjust length to hit the target remainder (by adding harmless leading ws or trimming tail slightly)
  while (doc.size() < target) doc.insert(doc.begin(), ' ');
  if (doc.size() > target) doc.resize(target);

  return doc;
}

// Very small exhaustive + patterned tests (0..N bytes). Extremely good at finding
// off-by-one and early-exit over-read bugs.
static std::vector<std::string> generate_small_exhaustive_cases(Rng& r, size_t max_small) {
  std::vector<std::string> cases;
  for (size_t len = 0; len <= std::min<size_t>(max_small, 192); ++len) {
    // All zeros
    cases.push_back(std::string(len, '\0'));
    // All 0xFF
    cases.push_back(std::string(len, '\xFF'));
    // Alternating
    std::string alt(len, ' ');
    for (size_t i=0; i<len; ++i) alt[i] = (i&1) ? '"' : '{';
    cases.push_back(alt);
    // Random few per length
    for (int k=0; k < 3; ++k) {
      std::string s(len, ' ');
      for (size_t i=0; i<len; ++i) s[i] = static_cast<char>(r.byte());
      cases.push_back(s);
    }
    // Single structural char at different positions
    if (len > 0) {
      for (char c : {'{','[','"', '1', 't'}) {
        std::string s(len, ' ');
        s[r.below(static_cast<uint32_t>(len))] = c;
        cases.push_back(s);
      }
    }
  }
  return cases;
}

// ---------------- Garbage / random byte inputs (enhanced) ----------------
static std::string generate_garbage(Rng& r, size_t max_len) {
  size_t len = r.below(static_cast<uint32_t>(std::min<size_t>(max_len, 4096)));
  std::string s;
  s.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    if (r.below(4) == 0) {
      s += static_cast<char>(r.byte());
    } else {
      static const char ascii[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        " \t\n\r\"{}[],.:-+_*/\\";
      s += ascii[r.below(static_cast<uint32_t>(sizeof(ascii)-1))];
    }
  }
  return s;
}

// ---------------- Advanced mutations (much more aggressive) ----------------
static std::string mutate(const std::string& base, Rng& r, bool aggressive_tail = false) {
  if (base.empty()) return base;
  std::string m = base;
  size_t n = m.size();
  size_t tail_start = (n > 128) ? n - 128 : 0;

  uint32_t op = r.below(aggressive_tail ? 22 : 16);

  switch (op) {
    case 0: { // truncate in the tail half
      size_t cut = r.range(static_cast<int32_t>(n/2), static_cast<int32_t>(n));
      m.resize(cut);
      break;
    }
    case 1: { // flip bits, heavily in tail
      size_t pos = aggressive_tail ? (tail_start + r.below(static_cast<uint32_t>(n - tail_start))) : r.below(static_cast<uint32_t>(n));
      uint8_t flip = 1u << r.below(8);
      m[pos] ^= static_cast<char>(flip);
      break;
    }
    case 2: { // insert escape / quote / high bytes near (or in) tail
      size_t pos = aggressive_tail ? (tail_start + r.below(std::max<uint32_t>(1, static_cast<uint32_t>(n-tail_start)))) : r.below(static_cast<uint32_t>(n));
      std::string ins;
      uint32_t what = r.below(9);
      if (what==0) ins = "\\uD800";
      else if (what==1) ins = "\\\"";
      else if (what==2) ins = "\\\\";
      else if (what==3) ins = "\xF0\x9F";
      else if (what==4) ins = "\"";
      else ins = std::string(1, static_cast<char>(r.biased_byte()));
      m.insert(m.begin() + pos, ins.begin(), ins.end());
      break;
    }
    case 3: { // append lots of junk (or dangerous tail)
      size_t add = r.range(1, 200);
      if (r.coin(0.5)) append_dangerous_tail(r, m, add);
      else r.fill_random_bytes(m, add);
      break;
    }
    case 4: { // delete bytes in tail
      if (n > 3) {
        size_t pos = aggressive_tail ? tail_start + r.below(static_cast<uint32_t>(n - tail_start - 1)) : r.below(static_cast<uint32_t>(n-1));
        size_t cnt = 1 + r.below(4);
        if (pos + cnt <= n) m.erase(pos, cnt);
      }
      break;
    }
    case 5: { // replace the final 1..64 bytes with partial token + noise
      size_t keep = (n > 40) ? n - r.range(1, 40) : 0;
      m.resize(keep);
      const char* partials[] = {"\"", "tru", "fal", "nul", "1.", "0.", "\\uD8", "inf", "Na", "{\"k\":\""};
      m += partials[r.below(10)];
      r.fill_random_bytes(m, r.below(20));
      if (r.coin()) m += '"';
      break;
    }
    case 6: { // arithmetic on digits near end (good for number parsing)
      for (int i=0; i< r.range(1,5); ++i) {
        size_t pos = n > 10 ? (n - 1 - r.below(20)) : r.below(static_cast<uint32_t>(n));
        if (isdigit(static_cast<unsigned char>(m[pos]))) {
          m[pos] = '0' + ( (m[pos]-'0' + r.range(-2,3) + 10) % 10 );
        }
      }
      break;
    }
    case 7: { // splice two "documents" (cut one, glue tail of another conceptual string)
      // We mutate in place by overwriting a region in the second half with random "JSON-ish"
      if (n > 20) {
        size_t start = n/2 + r.below(static_cast<uint32_t>(n/2));
        size_t len = r.below(static_cast<uint32_t>(n - start));
        for (size_t i=0; i<len; ++i) m[start+i] = static_cast<char>(r.biased_byte());
      }
      break;
    }
    case 8: { // double a tail section (repeat last interesting part)
      if (n > 30) {
        size_t tlen = r.range(5, 60);
        size_t from = n > tlen ? n - tlen : 0;
        std::string dup = m.substr(from);
        m += dup;
      }
      break;
    }
    default: {
      // light noise or targeted tail change
      size_t pos = aggressive_tail && n>10 ? (n - 1 - r.below(30)) : r.below(static_cast<uint32_t>(n));
      if (pos < m.size()) m[pos] = static_cast<char>(r.biased_byte());
      break;
    }
  }
  return m;
}

// Splice two inputs (useful evolutionary step)
static std::string splice(const std::string& a, const std::string& b, Rng& r) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  size_t cut = r.below(static_cast<uint32_t>(a.size()));
  std::string out = a.substr(0, cut);
  size_t take_from_b = r.below(static_cast<uint32_t>(b.size()));
  out += b.substr(b.size() - take_from_b);  // prefer tails from b
  return out;
}

// ---------------- Driver ----------------
static void print_usage() {
  std::cout <<
    "unpadded_fuzzer [options]\n"
    "  --iterations N       Base number of generated cases (default 80000)\n"
    "  --seed S             64-bit seed for reproducibility\n"
    "  --max-size M         Max document size in bytes (default 4096)\n"
    "  --no-garbage         Skip pure random byte phase\n"
    "  --focus-tail         Spend more time on tail-stress + boundary generators\n"
    "  --small-exhaustive   Run exhaustive small-length patterned cases (slow if M large)\n"
    "  --campaigns C        Number of independent RNG campaigns (default 1)\n";
}

int main(int argc, char** argv) {
  size_t iterations = 80000;
  uint64_t seed = 0xC0FFEE123456789ull;
  size_t max_size = 4096;
  bool do_garbage = true;
  bool focus_tail = false;
  bool do_small_exhaustive = false;
  int campaigns = 1;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--iterations" && i+1 < argc) iterations = static_cast<size_t>(std::stoul(argv[++i]));
    else if (a == "--seed" && i+1 < argc) seed = static_cast<uint64_t>(std::stoull(argv[++i]));
    else if (a == "--max-size" && i+1 < argc) max_size = static_cast<size_t>(std::stoul(argv[++i]));
    else if (a == "--no-garbage") do_garbage = false;
    else if (a == "--focus-tail") focus_tail = true;
    else if (a == "--small-exhaustive") do_small_exhaustive = true;
    else if (a == "--campaigns" && i+1 < argc) campaigns = std::max(1, static_cast<int>(std::stoi(argv[++i])));
    else if (a == "--help" || a == "-h") { print_usage(); return 0; }
    else {
      std::cerr << "Unknown arg: " << a << "\n";
      print_usage();
      return 2;
    }
  }

  std::cout << "simdjson parse_unpadded fuzzer (enhanced)\n";
  std::cout << "  simdjson version: " << SIMDJSON_VERSION << "\n";
  std::cout << "  target branch: lemire/nopad (via CPM)\n";
  std::cout << "  base iterations: " << iterations << "\n";
  std::cout << "  max-size: " << max_size << "\n";
  std::cout << "  campaigns: " << campaigns << "\n";
  std::cout << "  focus-tail: " << (focus_tail ? "yes" : "no") << "\n";
  std::cout << "  sanitizers: " <<
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    "ASAN "
#endif
#if __has_feature(undefined_behavior_sanitizer)
    "UBSAN "
#endif
#endif
    "(enabled at build)\n";
  std::cout << "\n";

  uint64_t total_cases = 0;

  for (int camp = 0; camp < campaigns; ++camp) {
    uint64_t camp_seed = seed ^ (uint64_t(camp) * 0x9e3779b97f4a7c15ull);
    std::cout << "=== Campaign " << (camp+1) << "/" << campaigns << " (seed 0x" << std::hex << camp_seed << std::dec << ") ===\n";

    Rng r(camp_seed);
    uint64_t cases = 0;

    // 1. Curated hard cases (many from PR + new dangerous ones)
    for (const auto& c : kInterestingCases) {
      if (c.size() > max_size) continue;
      if (!check_consistent(c, "curated")) return 1;
      ++cases;
      if ((cases % 1000) == 0) { std::cout << "  ... " << (total_cases + cases) << " cases OK\r" << std::flush; }
    }

    // Small exhaustive patterned (very effective for off-by-few overreads)
    if (do_small_exhaustive) {
      auto smalls = generate_small_exhaustive_cases(r, std::min<size_t>(max_size, 256));
      for (const auto& s : smalls) {
        if (!check_consistent(s, "small-exhaustive")) return 1;
        ++cases;
      }
    }

    // 2. Main structured + heavy tail stress
    size_t struct_target = focus_tail ? (iterations * 3 / 4) : (iterations / 2);
    std::vector<std::string> corpus; // interesting successful cases for further mutation
    corpus.reserve(256);

    for (size_t i = 0; i < struct_target; ++i) {
      std::string doc;
      if (focus_tail || r.coin(0.35)) {
        doc = generate_tail_stress(r, max_size);
      } else {
        doc = generate_structured(r);
      }
      if (doc.size() > max_size) doc.resize(max_size);

      if (!check_consistent(doc, "structured/tail")) return 1;
      ++cases;

      // Keep "interesting" docs in corpus (long, have strings/escapes near end, or deep)
      if (corpus.size() < 300 && (doc.size() > 80 || doc.find('\\') != std::string::npos || doc.find("0.") != std::string::npos)) {
        corpus.push_back(doc);
      }

      // Aggressive mutation, often tail-focused
      int mut_times = focus_tail ? r.range(0,4) : (r.below(5) == 0 ? 1 : 0);
      for (int mt=0; mt < mut_times; ++mt) {
        std::string m = mutate(doc, r, /*aggressive_tail=*/true);
        if (!m.empty() && m.size() <= max_size) {
          if (!check_consistent(m, "aggressive-mutate")) return 1;
          ++cases;
        }
      }

      // Occasionally splice with something from corpus
      if (!corpus.empty() && r.below(9) == 0) {
        const auto& other = corpus[r.index(corpus.size())];
        std::string sp = splice(doc, other, r);
        if (sp.size() <= max_size && !sp.empty()) {
          if (!check_consistent(sp, "splice")) return 1;
          ++cases;
        }
      }

      if ((total_cases + cases) % 2500 == 0) {
        std::cout << "  ... " << (total_cases + cases) << " cases OK\n" << std::flush;
      }
    }

    // 3. Block boundary + modulo-64 targeted bombing
    {
      Rng rb(camp_seed ^ 0x123456789abcdefull);
      for (int m = -2; m <= 66; ++m) {
        for (int base_mult : {1, 2, 3, 4, 8}) {
          long long bl_raw = static_cast<long long>(base_mult) * 64 + m;
          if (bl_raw <= 0 || static_cast<size_t>(bl_raw) > max_size) continue;
          size_t bl = static_cast<size_t>(bl_raw);
          std::string v = generate_block_boundary_variants(rb, bl, m);
          if (!v.empty() && v.size() <= max_size) {
            if (!check_consistent(v, "block-boundary")) return 1;
            ++cases;
          }
        }
      }
    }

    // 4. Pure garbage + truncations (still very important)
    if (do_garbage) {
      size_t garb_target = focus_tail ? iterations / 6 : iterations / 5;
      for (size_t i = 0; i < garb_target; ++i) {
        std::string g = generate_garbage(r, max_size);
        if (!check_consistent(g, "garbage")) return 1;
        ++cases;

        // Multiple truncations + tail mutations of garbage
        for (int t=0; t < (focus_tail ? 2 : 1); ++t) {
          if (g.size() > 2 && r.below(3) == 0) {
            size_t cut = r.below(static_cast<uint32_t>(g.size()));
            std::string t1 = g.substr(0, cut);
            if (!check_consistent(t1, "garbage-trunc")) return 1;
            ++cases;
          }
        }
        if ((total_cases + cases) % 2500 == 0) {
          std::cout << "  ... " << (total_cases + cases) << " cases OK\n" << std::flush;
        }
      }
    }

    // 5. Corpus-driven tail re-mutation (evolutionary pressure on interesting cases)
    if (!corpus.empty()) {
      size_t extra = focus_tail ? 1200 : 600;
      for (size_t i = 0; i < extra; ++i) {
        const std::string& base = corpus[r.index(corpus.size())];
        std::string m = mutate(base, r, /*aggressive_tail=*/true);
        if (m.size() > max_size) m.resize(max_size);
        if (!m.empty()) {
          if (!check_consistent(m, "corpus-tail-mutate")) return 1;
          ++cases;
        }
        // Also splice corpus members
        if (r.below(4) == 0 && corpus.size() > 1) {
          const std::string& b2 = corpus[r.index(corpus.size())];
          std::string sp = splice(base, b2, r);
          if (!sp.empty() && sp.size() <= max_size && !check_consistent(sp, "corpus-splice")) return 1;
          ++cases;
        }
      }
    }

    // 6. Classic linear sweeps + extra alignment sweeps (cheap but high value)
    {
      Rng rs(camp_seed ^ 0xfeedface1234ull);
      for (size_t off = 0; off <= 192; ++off) {
        {
          std::string s = "{\"k\":\""; s.append(off, 'q'); s += "\\uD800\\uDC00\"}";
          if (s.size() <= max_size && !check_consistent(s, "string-sweep")) return 1;
          ++cases;
        }
        {
          std::string n = "1."; n.append(4 + off, '9'); n += "e-10";
          if (n.size() <= max_size && !check_consistent(n, "number-sweep")) return 1;
          ++cases;
        }
        if (off % 17 == 0) {
          // occasional root string with broken escape right at end
          std::string rs = "\""; rs.append(off % 90, 'z'); rs += "\\uD800\\u";
          if (rs.size() <= max_size && !check_consistent(rs, "root-escape-sweep")) return 1;
          ++cases;
        }
      }
    }

    // 7. "Suffix bomb": many different dangerous tails glued to a few minimal valid prefixes
    {
      Rng rt(camp_seed ^ 0xa5a5a5a5a5a5a5a5ull);
      const char* prefixes[] = {"[", "{\"k\":", "\"", "1.", "0.", "tru", "fa", "nul", "{\"last\":" };
      for (const char* pfx : prefixes) {
        for (int t=0; t < 80; ++t) {
          std::string doc = pfx;
          size_t tail = rt.range(4, 220);
          append_dangerous_tail(rt, doc, tail);
          if (doc.size() > max_size) doc.resize(max_size);
          if (!check_consistent(doc, "suffix-bomb")) return 1;
          ++cases;
        }
      }
    }

    total_cases += cases;
    std::cout << "Campaign " << (camp+1) << " finished: " << cases << " cases, cumulative " << total_cases << "\n";
  }

  std::cout << "\n=== ALL DONE ===\n";
  std::cout << "Completed " << total_cases << " cases across " << campaigns << " campaign(s) with no mismatches.\n";
  std::cout << "No bug found.\n";
  return 0;
}
