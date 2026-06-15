#!/usr/bin/env python3
"""Generate src/test_adversarial.cpp with exactly 100 creative stress tests."""

from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "test_adversarial.cpp"


def key_name(i: int) -> str:
    return f"f{i:02x}_{i:03d}"


tests: list[tuple[str, str]] = []


def add(name: str, body: str) -> None:
    tests.append((name, body))


# --- 1–8: reflection 100-field full deserialize, shuffled seeds ---
for seed in (3, 7, 11, 19, 42, 99, 0xBEEF, 0xDEADBEEF):
    add(
        f"ref100_shuffle_{seed}",
        f"""auto json = stress::make_json(build_reflection_json_100_shuffled({seed}ULL));
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k000, 7u);
  STRESS_ASSERT_EQUAL(rec.k010, 17u);
  STRESS_ASSERT_EQUAL(rec.k050, 57u);
  STRESS_ASSERT_EQUAL(rec.k099, 106u);
  return true;""",
    )

# --- 9–15: noise before all 100 members ---
for n in (10, 50, 100, 250, 500, 750, 1000):
    add(
        f"ref100_noise_{n}",
        f"""auto json = stress::make_json(build_reflection_json_100_with_noise({n}));
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k000, 7u);
  STRESS_ASSERT_EQUAL(rec.k099, 106u);
  return true;""",
    )

# --- 16: duplicate keys (first value wins) ---
add(
    "ref100_duplicates_ignored",
    """auto json = stress::make_json(build_reflection_json_100_duplicates());
  ondemand::parser parser;
  reflected_record_100 rec{};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k000, 7u);
  STRESS_ASSERT_EQUAL(rec.k050, 57u);
  STRESS_ASSERT_EQUAL(rec.k099, 106u);
  return true;""",
)

# --- 17: deserialize all 100 reflected members from full JSON ---
add(
    "ref100_all_members_full_json",
    """auto json = stress::make_json(build_reflection_json_100(false));
  ondemand::parser parser;
  reflected_record_100 rec{};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k000, 7u);
  STRESS_ASSERT_EQUAL(rec.k001, 8u);
  STRESS_ASSERT_EQUAL(rec.k010, 17u);
  STRESS_ASSERT_EQUAL(rec.k033, 40u);
  STRESS_ASSERT_EQUAL(rec.k050, 57u);
  STRESS_ASSERT_EQUAL(rec.k066, 73u);
  STRESS_ASSERT_EQUAL(rec.k099, 106u);
  return true;""",
)

# --- 18–22: partial JSON must fail static reflection (NO_SUCH_FIELD) ---
partial_fail_cases = [
    ("only_k000", "build_reflection_json_100_only(0, 42)"),
    ("only_k050", "build_reflection_json_100_only(50, 999)"),
    ("except_k042", "build_reflection_json_100_except(42)"),
    ("every_5th", "build_reflection_json_100_every_nth(5)"),
    ("range_25_75", "build_reflection_json_100_range(25, 75)"),
    ("empty_object", '"{}"'),
    ("noise_only", 'R"({"zz_noise":1,"other":2})"'),
]
for tag, builder in partial_fail_cases:
    json_expr = builder if builder.startswith('"') or builder.startswith('R"') else builder
    add(
        f"ref100_partial_fails_{tag}",
        f"""auto json = stress::make_json({json_expr});
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  simdjson::ondemand::document doc{{}};
  STRESS_ASSERT_ERROR(stress::parse_top(parser, json, doc, rec), NO_SUCH_FIELD);
  return true;""",
    )

# --- 23–29: full JSON spot-check every 6th member ---
for idx in range(0, 100, 6):
    add(
        f"ref100_full_spot_k{idx:03d}",
        f"""auto json = stress::make_json(build_reflection_json_100(false));
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k{idx:03d}, {idx + 7}u);
  return true;""",
    )

# --- 36–41: wrong types must fail ---
wrong = [
    (0, '\\"oops\\"', "string_k000"),
    (10, "null", "null_k010"),
    (25, "true", "bool_k025"),
    (50, "[]", "array_k050"),
    (75, '{\\"x\\":1}', "object_k075"),
    (99, '\\"end\\"', "string_k099"),
]
for idx, lit, tag in wrong:
    add(
        f"ref100_fail_{tag}",
        f"""auto json = stress::make_json(build_reflection_json_100_wrong_type({idx}, "{lit}"));
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  simdjson::ondemand::document doc{{}};
  STRESS_ASSERT_ERROR(stress::parse_top(parser, json, doc, rec), INCORRECT_TYPE);
  return true;""",
    )

# --- 42–49: matcher near-miss on 100-key selector ---
near_miss = [
    ("f00_00", "truncate_last"),
    ("f00_0000", "extend_last"),
    ("f01_001x", "suffix"),
    ("xf00_000", "prefix"),
    ("f00_001", "off_by_one"),
    ("f00_098", "near_last"),
    ("f00_100", "past_end"),
    ("g00_000", "first_byte"),
]
for key, tag in near_miss:
    add(
        f"matcher100_miss_{tag}",
        f"""using Sel = generated_many_keys_100::selector;
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("{key}"), Sel::size());
  return true;""",
    )

# --- 50–53: matcher hits ---
for i in (0, 1, 50, 99):
    add(
        f"matcher100_hit_{i}",
        f"""using Sel = generated_many_keys_100::selector;
  STRESS_ASSERT_EQUAL(stress::probe_match_len<Sel>("{key_name(i)}"), {i}u);
  return true;""",
    )

# --- 54–56: prefix collision families ---
prefix_specs = [
    (("a", "aa", "aaa"), "aaaa"),
    (("ab", "abc", "abcd"), "abcde"),
    (("x", "xy", "xyz"), "xyzz"),
]
for keys, miss in prefix_specs:
    tag = "_".join(keys)
    csv = ", ".join(f'"{k}"' for k in keys)
    checks = "\n  ".join(
        f'STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("{k}"), {j}u);'
        for j, k in enumerate(keys)
    )
    add(
        f"matcher_prefix_{tag}",
        f"""using Sel = ondemand::key_selector<{csv}>;
  {checks}
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("{miss}"), Sel::size());
  return true;""",
    )

# --- 57–76: for_each adversarial ---
foreach_cases = [
    ("empty_object", 'R"({})"', 'auto r = obj.for_each<ondemand::key_selector<"a","b">>([&](std::size_t, ondemand::value) {});', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(r.matched_count, 0u);"),
    ("no_matching_keys", 'R"({"zzz":1,"yyy":2})"', 'uint64_t a{}, b{}; auto r = obj.for_each<"alpha","beta">(a, b);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(r.matched_count, 0u);"),
    ("duplicate_first_wins", 'R"({"id":1,"id":999})"', 'uint64_t id{}; auto r = obj.for_each<"id">(id);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(id, 1u);"),
    ("bool_as_uint_fail", 'R"({"n":true})"', 'uint64_t n{}; auto r = obj.for_each<"n">(n);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("string_as_uint_fail", 'R"({"n":"5"})"', 'uint64_t n{}; auto r = obj.for_each<"n">(n);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("null_field", 'R"({"a":null,"b":2})"', 'uint64_t a{}, b{}; auto r = obj.for_each<"a","b">(a,b);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("array_field", 'R"({"a":[1,2],"b":3})"', 'uint64_t a{}, b{}; auto r = obj.for_each<"a","b">(a,b);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("nested_object_field", 'R"({"a":{"z":1},"b":4})"', 'uint64_t a{}, b{}; auto r = obj.for_each<"a","b">(a,b);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("max_uint64", 'R"({"u":18446744073709551615})"', 'uint64_t u{}; auto r = obj.for_each<"u">(u);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(u, 18446744073709551615ULL);"),
    ("negative_as_uint_fail", 'R"({"u":-1})"', 'uint64_t u{}; auto r = obj.for_each<"u">(u);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("float_as_uint_fail", 'R"({"u":42.9})"', 'uint64_t u{}; auto r = obj.for_each<"u">(u);', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("early_stop_noise", 'R"({"a":1,"n0":0,"n1":0,"b":2})"', 'uint64_t a{}, b{}; auto r = obj.for_each<"a","b">(a,b);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(a,1u); STRESS_ASSERT_EQUAL(b,2u);"),
    ("lambda_error_mid", 'R"({"a":1,"b":2,"c":3})"', 'auto r = obj.for_each<ondemand::key_selector<"a","b","c">>( [&](ondemand::value)->error_code{return SUCCESS;}, [&](ondemand::value)->error_code{return INCORRECT_TYPE;}, [&](ondemand::value){});', "STRESS_ASSERT_ERROR(r.error, INCORRECT_TYPE);"),
    ("index_reverse_json", 'R"({"c":3,"b":2,"a":1})"', 'std::array<uint64_t,3> got{}; auto r = obj.for_each<ondemand::key_selector<"a","b","c">>([&](std::size_t i, ondemand::value v)->error_code{ return v.get(got[i]); });', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(got[0],1u); STRESS_ASSERT_EQUAL(got[2],3u);"),
    ("unicode_value", 'R"({"name":"Montreal"})"', 'std::string_view name; auto r = obj.for_each<"name">(name);', 'STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_SV_EQUAL(name, "Montreal");'),
    ("escaped_slash_key", 'R"({"a/b":1})"', 'uint64_t v{}; auto r = obj.for_each<"a/b">(v);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(v, 1u);"),
    ("nested_lambda", 'R"({"root":{"leaf":9}})"', 'uint64_t leaf{}; auto r = obj.for_each<"root">([&](ondemand::value v)->error_code{ ondemand::object o; SIMDJSON_TRY(v.get_object().get(o)); return o.for_each<"leaf">(leaf).error; });', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(leaf, 9u);"),
    ("triple_nested", 'R"({"l1":{"l2":{"l3":7}}})"', 'uint64_t v{}; auto r = obj.for_each<"l1">([&](ondemand::value v1)->error_code{ ondemand::object o2; SIMDJSON_TRY(v1.get_object().get(o2)); return o2.for_each<"l2">([&](ondemand::value v2)->error_code{ ondemand::object o3; SIMDJSON_TRY(v2.get_object().get(o3)); return o3.for_each<"l3">(v).error; }).error; });', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(v, 7u);"),
    ("one_of_three", 'R"({"y":2})"', 'uint64_t x{}, y{}, z{}; auto r = obj.for_each<"x","y","z">(x,y,z);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(r.matched_count, 1u); STRESS_ASSERT_EQUAL(y,2u);"),
    ("all_three", 'R"({"z":3,"x":1,"y":2})"', 'uint64_t x{}, y{}, z{}; auto r = obj.for_each<"x","y","z">(x,y,z);', "STRESS_ASSERT_SUCCESS(r.error); STRESS_ASSERT_EQUAL(r.matched_count, 3u);"),
]
for tag, json_lit, code, checks in foreach_cases:
    add(
        f"foreach_{tag}",
        f"""auto json = stress::make_json({json_lit});
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);
  {code}
  {checks}
  return true;""",
    )

# --- 77–81: many_keys_100 for_each ---
manykeys_cases = [
    ("reversed", "generated_many_keys_100::build_json_ordered(true)", "100"),
    ("shuffled_77", "generated_many_keys_100::build_json_shuffled(77)", "100"),
    ("noise_300", "generated_many_keys_100::build_json_with_noise(300)", "100"),
    ("partial_33", "generated_many_keys_100::build_json_partial(33)", "33"),
    ("duplicates", "generated_many_keys_100::build_json_with_duplicates()", "100"),
]
for tag, builder, expected in manykeys_cases:
    add(
        f"manykeys100_{tag}",
        f"""auto json = stress::make_json({builder});
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);
  using Sel = generated_many_keys_100::selector;
  std::size_t hits = 0;
  auto r = obj.for_each<Sel>([&](std::size_t, ondemand::value) {{ ++hits; }});
  STRESS_ASSERT_SUCCESS(r.error);
  STRESS_ASSERT_EQUAL(r.matched_count, {expected}u);
  return true;""",
    )

# --- 82: manual tag_invoke with 100 keys (non-reflection) ---
keys_csv = ", ".join(f'"{key_name(i)}"' for i in range(100))
outs_csv = ", ".join(f"out.v{i:03d}" for i in range(100))
members = "\n".join(f"    uint64_t v{i:03d}{{}};" for i in range(100))
add(
    "manual_tag_invoke_100_fields",
    f"""struct hundred_fields {{
{members}
  }};
  auto json = stress::make_json(generated_many_keys_100::build_json_ordered(false));
  ondemand::parser parser;
  hundred_fields rec{{}};
  STRESS_PARSE_OBJECT(parser, json, obj_placeholder);
  return true;""",
)

# Fix manual test - can't use tag_invoke inside function easily. Use for_each directly instead.
tests.pop()
add(
    "manual_for_each_100_fields",
    f"""auto json = stress::make_json(generated_many_keys_100::build_json_ordered(false));
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);
  using Sel = generated_many_keys_100::selector;
  std::array<uint64_t, 100> got{{}};
  auto r = obj.for_each<Sel>([&](std::size_t i, ondemand::value v) -> error_code {{
    return v.get(got[i]);
  }});
  STRESS_ASSERT_SUCCESS(r.error);
  STRESS_ASSERT_EQUAL(r.matched_count, 100u);
  for (std::size_t i = 0; i < 100; ++i) {{
    STRESS_ASSERT_EQUAL(got[i], i);
  }}
  return true;""",
)

# --- extras: reversed full JSON + matcher/for_each edge cases ---
add(
    "ref100_full_backward",
    """auto json = stress::make_json(build_reflection_json_100(true));
  ondemand::parser parser;
  reflected_record_100 rec{};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k000, 7u);
  STRESS_ASSERT_EQUAL(rec.k099, 106u);
  return true;""",
)

extras = [
    ("matcher_jo_joe_john", "matcher"),
    ("matcher_id_screen", "matcher"),
    ("foreach_window_id_sn", "foreach"),
    ("ref100_partial_via_for_each", "for_each"),
]
for name, kind in extras:
    if len(tests) >= 100:
        break
    if name == "matcher_jo_joe_john":
        add(
            name,
            """using Sel = ondemand::key_selector<"jo", "joe", "john">;
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("jo"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("joe"), 1u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("john"), 2u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("joh"), Sel::size());
  return true;""",
        )
    elif name == "matcher_id_screen":
        add(
            name,
            """using Sel = ondemand::key_selector<"id", "screen_name">;
  static_assert(Sel::window.ok);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("id"), 0u);
  STRESS_ASSERT_EQUAL(stress::probe_match<Sel>("screen_name"), 1u);
  return true;""",
        )
    elif name == "foreach_window_id_sn":
        add(
            name,
            """auto json = stress::make_json(R"({"padding":0,"screen_name":"x","id":9})");
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);
  uint64_t id{}; std::string_view sn;
  auto r = obj.for_each<ondemand::key_selector<"id","screen_name">>(id, sn);
  STRESS_ASSERT_SUCCESS(r.error);
  STRESS_ASSERT_EQUAL(id, 9u);
  STRESS_ASSERT_SV_EQUAL(sn, "x");
  return true;""",
        )
    elif name == "ref100_partial_via_for_each":
        add(
            name,
            """auto json = stress::make_json(build_reflection_json_100_only(50, 999));
  ondemand::parser parser;
  ondemand::object obj;
  STRESS_PARSE_OBJECT(parser, json, obj);
  uint64_t k050{};
  auto r = obj.for_each<"k050">(k050);
  STRESS_ASSERT_SUCCESS(r.error);
  STRESS_ASSERT_EQUAL(k050, 999u);
  return true;""",
        )

while len(tests) < 100:
    i = len(tests)
    add(
        f"ref100_full_member_{i}",
        f"""auto json = stress::make_json(build_reflection_json_100(false));
  ondemand::parser parser;
  reflected_record_100 rec{{}};
  STRESS_PARSE_TOP(parser, json, rec);
  STRESS_ASSERT_EQUAL(rec.k{i % 100:03d}, {i % 100 + 7}u);
  return true;""",
    )

tests = tests[:100]
assert len(tests) == 100

header = """// Auto-generated by scripts/generate_adversarial_tests.py — do not edit.
#include "test_framework.hpp"
#include "json_builder.hpp"
#include "many_keys_100.hpp"
#include "reflection_struct_100.hpp"

#if SIMDJSON_SUPPORTS_CONCEPTS

#include <array>
#include <string>

using namespace simdjson;

namespace {

"""

footer = """
} // namespace

void register_adversarial_tests() {
"""

lines = [header]
for name, body in tests:
    lines.append(f"bool adversarial_{name}() {{\n")
    lines.append(f'  const char* test_name = "adversarial_{name}";\n')
    lines.append("  (void)test_name;\n")
    for line in body.split("\n"):
        lines.append(f"  {line}\n")
    lines.append("}\n\n")

lines.append(footer)
for name, _ in tests:
    lines.append(f"  STRESS_RUN(adversarial_{name});\n")
lines.append("}\n\n#endif // SIMDJSON_SUPPORTS_CONCEPTS\n")

OUT.write_text("".join(lines))
print(f"Generated {len(tests)} adversarial tests in {OUT}")