#include "v8kit/binding/TypeConverter.h"

#include "catch2/catch_test_macros.hpp"
#include <catch2/catch_approx.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

TEST_CASE("TypeConverter full test") {
    auto               engine = std::make_unique<v8kit::Engine>();
    v8kit::EngineScope enter{engine.get()};

    using namespace v8kit::binding;

    // ----------------------------
    // bool
    // ----------------------------
    auto js_bool = toJs(true);
    REQUIRE(js_bool.isBoolean());
    REQUIRE(js_bool.asBoolean().getValue() == true);

    bool cpp_bool = toCpp<bool>(js_bool);
    REQUIRE(cpp_bool == true);

    // ----------------------------
    // numbers
    // ----------------------------
    int32_t n_i32  = 123;
    auto    js_i32 = toJs(n_i32);
    REQUIRE(js_i32.isNumber());
    REQUIRE(js_i32.asNumber().getInt32() == n_i32);

    double n_double  = 3.14;
    auto   js_double = toJs(n_double);
    REQUIRE(js_double.isNumber());
    REQUIRE(js_double.asNumber().getValueAs<double>() == Catch::Approx(3.14));

    int64_t n_i64  = 9876543210;
    auto    js_i64 = toJs(n_i64);
    REQUIRE(js_i64.isBigInt());
    REQUIRE(js_i64.asBigInt().getInt64() == n_i64);

    uint64_t n_u64  = 1234567890;
    auto     js_u64 = toJs(n_u64);
    REQUIRE(js_u64.isBigInt());
    REQUIRE(js_u64.asBigInt().getUint64() == n_u64);

    // ----------------------------
    // strings
    // ----------------------------
    std::string str    = "hello";
    auto        js_str = toJs(str);
    REQUIRE(js_str.isString());
    REQUIRE(js_str.asString().getValue() == str);

    // char array
    const char* cstr    = "world";
    auto        js_cstr = toJs(cstr);
    REQUIRE(js_cstr.isString());
    REQUIRE(js_cstr.asString().getValue() == cstr);

    // ----------------------------
    // enum
    // ----------------------------
    enum class Color { Red, Green, Blue };
    auto js_enum = toJs(Color::Green);
    REQUIRE(js_enum.isNumber());
    REQUIRE(js_enum.asNumber().getInt32() == static_cast<int>(Color::Green));

    Color cpp_enum = toCpp<Color>(js_enum);
    REQUIRE(cpp_enum == Color::Green);

    // ----------------------------
    // optional
    // ----------------------------
    std::optional<int> opt    = std::nullopt;
    auto               js_opt = toJs(opt);
    REQUIRE(js_opt.isNull());

    opt    = 42;
    js_opt = toJs(opt);
    REQUIRE(js_opt.isNumber());
    REQUIRE(js_opt.asNumber().getInt32() == 42);

    std::optional<int> cpp_opt = toCpp<std::optional<int>>(js_opt);
    REQUIRE(cpp_opt.has_value());
    REQUIRE(cpp_opt.value() == 42);

    // ----------------------------
    // vector
    // ----------------------------
    std::vector<int> vec    = {1, 2, 3};
    auto             js_vec = toJs(vec);
    REQUIRE(js_vec.isArray());
    REQUIRE(js_vec.asArray().length() == 3);
    REQUIRE(js_vec.asArray().get(0).asNumber().getInt32() == 1);
    REQUIRE(js_vec.asArray().get(1).asNumber().getInt32() == 2);
    REQUIRE(js_vec.asArray().get(2).asNumber().getInt32() == 3);

    std::vector<int> cpp_vec = toCpp<std::vector<int>>(js_vec);
    REQUIRE(cpp_vec == vec);

    // ----------------------------
    // unordered_map
    // ----------------------------
    std::unordered_map<std::string, int> map = {
        {"a", 1},
        {"b", 2}
    };
    auto js_map = toJs(map);
    REQUIRE(js_map.isObject());

    auto cpp_map = toCpp<std::unordered_map<std::string, int>>(js_map);
    REQUIRE(cpp_map == map);

    // ----------------------------
    // pair
    // ----------------------------
    std::pair<int, std::string> p       = {42, "pair"};
    auto                        js_pair = toJs(p);
    REQUIRE(js_pair.isArray());
    REQUIRE(js_pair.asArray().length() == 2);
    REQUIRE(js_pair.asArray().get(0).asNumber().getInt32() == 42);
    REQUIRE(js_pair.asArray().get(1).asString().getValue() == "pair");

    auto cpp_pair = toCpp<std::pair<int, std::string>>(js_pair);
    REQUIRE(cpp_pair == p);

    // ----------------------------
    // variant
    // ----------------------------
    std::variant<int, std::string> var    = 123;
    auto                           js_var = toJs(var);
    REQUIRE(js_var.isNumber());

    auto cpp_var = toCpp<std::variant<int, std::string>>(js_var);
    REQUIRE(std::get<int>(cpp_var) == 123);

    var    = std::string("variant");
    js_var = toJs(var);
    REQUIRE(js_var.isString());
    cpp_var = toCpp<std::variant<int, std::string>>(js_var);
    REQUIRE(std::get<std::string>(cpp_var) == "variant");

    // ----------------------------
    // monostate
    // ----------------------------
    std::monostate ms;
    auto           js_ms = toJs(ms);
    REQUIRE(js_ms.isNull());

    auto cpp_ms = toCpp<std::monostate>(js_ms);
    (void)cpp_ms; // just type check

    // ----------------------------
    // nested containers
    // ----------------------------
    std::vector<std::optional<int>> nested    = {1, std::nullopt, 3};
    auto                            js_nested = toJs(nested);
    REQUIRE(js_nested.isArray());
    auto cpp_nested = toCpp<std::vector<std::optional<int>>>(js_nested);
    REQUIRE(cpp_nested.size() == 3);
    REQUIRE(cpp_nested[0] == 1);
    REQUIRE(!cpp_nested[1].has_value());
    REQUIRE(cpp_nested[2] == 3);
}
