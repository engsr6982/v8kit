#include "v8kit/core/Engine.h"
#include "v8kit/core/EngineScope.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"

struct CoreTestFixture {
    std::unique_ptr<v8kit::Engine> engine;
    CoreTestFixture() { engine = std::make_unique<v8kit::Engine>(); }
};

TEST_CASE_METHOD(CoreTestFixture, "Engine::eval") {
    REQUIRE(engine != nullptr);

    v8kit::EngineScope scope(engine.get());

    auto result = engine->eval(v8kit::String::newString("1 + 1"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 2);

    result = engine->eval(v8kit::String::newString("1 + '1'"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "11");
}


class ScriptClass {
public:
    static v8kit::Local<v8kit::Value> foo(v8kit::Arguments const& arguments) { return v8kit::String::newString("foo"); }
    static v8kit::Local<v8kit::Value> forward(v8kit::Arguments const& arguments) { return arguments[0]; }

    inline static std::string         name{"123"};
    static v8kit::Local<v8kit::Value> getter() { return v8kit::String::newString(name); }
    static void                       setter(v8kit::Local<v8kit::Value> const& value) {
        if (value.isString()) {
            name = value.asString().getValue();
        }
    }
};
TEST_CASE_METHOD(CoreTestFixture, "registerClass") {
    v8kit::EngineScope scope{engine.get()};

    // clang-format off
    static auto meta = v8kit::ClassMeta{
        "ScriptClass",
        v8kit::StaticMemberMeta{
            {
                v8kit::StaticMemberMeta::Property{"name", &ScriptClass::getter, &ScriptClass::setter},
            },
            {
                v8kit::StaticMemberMeta::Function{"foo", &ScriptClass::foo},
                v8kit::StaticMemberMeta::Function{"forward", &ScriptClass::forward}
            },
        },
        v8kit::InstanceMemberMeta{
            nullptr,
            {},
            {},
            sizeof(ScriptClass),
            nullptr
        },
        nullptr,
        typeid(ScriptClass)
    };
    // clang-format on

    engine->registerClass(meta);

    auto result = engine->eval(v8kit::String::newString("ScriptClass.foo()"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "foo");

    result = engine->eval(v8kit::String::newString("ScriptClass.forward('bar')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "bar");

    engine->eval(v8kit::String::newString("ScriptClass.name = 'bar'"));
    result = engine->eval(v8kit::String::newString("ScriptClass.name"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "bar");

    // ensure toStringTag
    result = engine->eval(v8kit::String::newString("Object.prototype.toString.call(ScriptClass)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "[object ScriptClass]");
}


enum class Color { Red, Green, Blue };
TEST_CASE_METHOD(CoreTestFixture, "registerEnum") {
    v8kit::EngineScope scope{engine.get()};

    static auto meta = v8kit::EnumMeta{
        "Color",
        {
          v8kit::EnumMeta::Entry{"Red", static_cast<int64_t>(Color::Red)},
          v8kit::EnumMeta::Entry{"Green", static_cast<int64_t>(Color::Green)},
          v8kit::EnumMeta::Entry{"Blue", static_cast<int64_t>(Color::Blue)},
          }
    };

    engine->registerEnum(meta);

    auto result = engine->eval(v8kit::String::newString("Color.$name"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "Color");

    result = engine->eval(v8kit::String::newString("Color.Red"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Red));

    result = engine->eval(v8kit::String::newString("Color.Green"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Green));

    result = engine->eval(v8kit::String::newString("Color.Blue"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Blue));

    // ensure toStringTag
    result = engine->eval(v8kit::String::newString("Object.prototype.toString.call(Color)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "[object Color]");

    // ensure $name don't enumerate
    auto ensure = v8kit::Function::newFunction([](v8kit::Arguments const& arguments) -> v8kit::Local<v8kit::Value> {
        REQUIRE(arguments.length() == 1);
        REQUIRE(arguments[0].isString());
        REQUIRE(arguments[0].asString().getValue() != "$name");
        return {};
    });
    engine->globalThis().set(v8kit::String::newString("ensure"), ensure);
    engine->eval(v8kit::String::newString("for (let key in Color) { ensure(key) }"));
}


TEST_CASE_METHOD(CoreTestFixture, "Exception pass-through") {
    v8kit::EngineScope scope{engine.get()};

    REQUIRE_THROWS_MATCHES(
        engine->eval(v8kit::String::newString("throw new Error('abc')")),
        v8kit::Exception,
        Catch::Matchers::Message("Uncaught Error: abc")
    );

    static constexpr auto msg = "Cpp layer throw exception";
    auto thowr  = v8kit::Function::newFunction([](v8kit::Arguments const& arguments) -> v8kit::Local<v8kit::Value> {
        throw v8kit::Exception{msg};
    });
    auto ensure = v8kit::Function::newFunction([](v8kit::Arguments const& arguments) -> v8kit::Local<v8kit::Value> {
        REQUIRE(arguments.length() == 1);
        REQUIRE(arguments[0].isString());
        REQUIRE(arguments[0].asString().getValue() == msg);
        return {};
    });
    engine->globalThis().set(v8kit::String::newString("throwr"), thowr);
    engine->globalThis().set(v8kit::String::newString("ensure"), ensure);

    engine->eval(v8kit::String::newString("try { throwr() } catch (e) { ensure(e.message) }"));
}