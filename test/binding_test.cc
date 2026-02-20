#include "v8kit/core/Engine.h"
#include "v8kit/core/EngineScope.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"

#include "v8kit/binding/BindingUtils.h"
#include "v8kit/binding/MetaBuilder.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"

#include <iostream>


namespace {

using namespace v8kit;
using namespace v8kit::binding;

struct ScriptEvalAssertContext {
    inline static size_t                                 CurrentRunningLine    = 0;
    inline static std::string                            CurrentScriptEvalCode = "";
    inline static bool                                   CurrentCondition      = true;
    inline static std::optional<Catch::AssertionHandler> handler;

    static void required(bool condition, std::string ctx) {
        CurrentCondition = condition;
        handler.emplace(
            "REQUIRE"_catch_sr,
            Catch::SourceLineInfo(__FILE__, CurrentRunningLine),
            Catch::StringRef(CurrentScriptEvalCode.data(), CurrentScriptEvalCode.size()),
            Catch::ResultDisposition::Normal
        );
        handler->handleExpr(Catch::Decomposer() <= condition);
        if (!condition) handler->handleMessage(Catch::ResultWas::ExplicitFailure, std::move(ctx));
    }
};

#define MOUNT_FUNC_NAME "assert"

struct BindingTestFixture {
    std::unique_ptr<Engine> engine;
    explicit BindingTestFixture() : engine(std::make_unique<Engine>()) {
        // mount assert
        EngineScope lock{engine.get()};
        engine->globalThis().set(
            String::newString(MOUNT_FUNC_NAME),
            Function::newFunction(cpp_func(&ScriptEvalAssertContext::required))
        );
    }
};

// REQUIRE_EVAL("Foo.x", "${Foo.x}")
#define REQUIRE_EVAL(COND, MSG)                                                                                        \
    {                                                                                                                  \
        ScriptEvalAssertContext::CurrentRunningLine    = __LINE__;                                                     \
        ScriptEvalAssertContext::CurrentScriptEvalCode = MOUNT_FUNC_NAME "(" COND ", `" MSG "`)";                      \
        engine->eval(String::newString(ScriptEvalAssertContext::CurrentScriptEvalCode));                               \
        ScriptEvalAssertContext::handler->complete();                                                                  \
        ScriptEvalAssertContext::handler.reset();                                                                      \
    }


TEST_CASE_METHOD(BindingTestFixture, "Local<Function> suger api") {
    EngineScope scope{engine.get()};

    auto fn     = Function::newFunction([](Arguments const& arguments) -> Local<Value> {
        REQUIRE(arguments.length() == 2);
        REQUIRE(arguments[0].isNumber());
        REQUIRE(arguments[0].asNumber().getInt32() == 123);
        REQUIRE(arguments[1].isString());
        REQUIRE(arguments[1].asString().getValue() == "abc");
        return String::newString("hello world");
    });
    auto result = call(fn, {}, 123, "abc");
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "hello world");
}


class StaticClass {
public:
    static Local<Value> add(Arguments const& arguments) {
        REQUIRE(arguments.length() == 2);
        REQUIRE(arguments[0].isNumber());
        REQUIRE(arguments[1].isNumber());
        return Number::newNumber(arguments[0].asNumber().getInt32() + arguments[1].asNumber().getInt32());
    }
    static int         add2(int a, int b) { return a + b; }
    static std::string append(std::string str, std::string str2) { return str + str2; }
    static std::string append(std::string str, int num) { return str + std::to_string(num); }

    inline static std::string name = "StaticClass";

    static std::string const& getName() { return name; }
    static void               setName(std::string name1) { name = std::move(name1); }

    static Local<Value> getNameScript() { return String::newString(name); }
    static void         setNameScript(Local<Value> const& value) {
        if (value.isString()) {
            name = value.asString().getValue();
        }
    }
};
auto StaticClassMeta = defClass<void>("StaticClass")
                           .func("add", &StaticClass::add)
                           .func("add2", &StaticClass::add2)
                           .func(
                               "append",
                               static_cast<std::string (*)(std::string, std::string)>(&StaticClass::append),
                               static_cast<std::string (*)(std::string, int)>(&StaticClass::append)
                           )
                           // with script callback
                           .var("script_name", &StaticClass::getNameScript, &StaticClass::setNameScript)
                           // with wrap native Getter & Setter
                           .var("native_name", &StaticClass::getName, &StaticClass::setName)
                           // with auto gen getter & setter
                           .var("auto_name", &StaticClass::name)
                           // with auto gen constant getter
                           .var("auto_const", "constant")
                           // with readonly for native getter
                           .var_readonly("readonly_s_name", &StaticClass::getNameScript)
                           // with readonly for wrap native Getter
                           .var_readonly("readonly_n_name", &StaticClass::getName)
                           // with readonly for auto gen getter
                           .var_readonly("readonly_a_name", &StaticClass::name)
                           .build();

TEST_CASE_METHOD(BindingTestFixture, "Static class") {
    EngineScope scope{engine.get()};

    engine->registerClass(StaticClassMeta);

    // .func
    auto result = engine->eval(String::newString("StaticClass.add(1, 2)"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 3);

    result = engine->eval(String::newString("StaticClass.add2(1, 2)"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 3);

    result = engine->eval(String::newString("StaticClass.append('hello', 'world')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "helloworld");

    result = engine->eval(String::newString("StaticClass.append('hello', 123)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "hello123");

    REQUIRE_THROWS_MATCHES(
        engine->eval(String::newString("StaticClass.append(123, 'world')")),
        Exception,
        Catch::Matchers::Message("Uncaught TypeError: no overload found")
    );


    // .var
    // test for GetterCallback and SetterCallback
    engine->eval(String::newString("StaticClass.script_name = 'new name'"));
    REQUIRE(StaticClass::name == "new name");
    REQUIRE_EVAL("StaticClass.script_name == 'new name'", "${StaticClass.script_name}")

    // test for wrap native Getter & Setter
    StaticClass::name = "test";
    REQUIRE_EVAL("StaticClass.native_name == 'test'", "${StaticClass.native_name}")
    engine->eval(String::newString("StaticClass.native_name = 'foo'"));
    REQUIRE_EVAL("StaticClass.native_name == 'foo'", "${StaticClass.native_name}")
    REQUIRE(StaticClass::name == "foo");

    // test for auto gen getter & setter
    StaticClass::name = "test";
    REQUIRE_EVAL("StaticClass.auto_name == 'test'", "${StaticClass.auto_name}")
    engine->eval(String::newString("StaticClass.auto_name = 'foo'"));
    REQUIRE_EVAL("StaticClass.auto_name == 'foo'", "${StaticClass.auto_name}")
    REQUIRE(StaticClass::name == "foo");

    // test for auto gen constant getter
    REQUIRE_EVAL("StaticClass.auto_const == 'constant'", "${StaticClass.auto_const}")

    // test for readonly
    StaticClass::name = "readonly";
    REQUIRE_EVAL("StaticClass.readonly_s_name == 'readonly'", "${StaticClass.readonly_s_name}") // script callback
    REQUIRE_EVAL("StaticClass.readonly_n_name == 'readonly'", "${StaticClass.readonly_n_name}") // native callback
    REQUIRE_EVAL("StaticClass.readonly_a_name == 'readonly'", "${StaticClass.readonly_a_name}") // auto gen callback
}


class SimpleClass {
public:
    int         id_;
    std::string name_;

    SimpleClass(std::string name) : id_(0), name_(std::move(name)) {}
    SimpleClass(int id, std::string name) : id_(id), name_(std::move(name)) {}
};
auto SimpleClassMeta = defClass<SimpleClass>("SimpleClass")
                           // .ctor(nullptr)
                           // .ctor([](Arguments const& arguments) -> std::unique_ptr<NativeInstance> {
                           //
                           // })
                           // .ctor<std::string>()
                           .build();
TEST_CASE_METHOD(BindingTestFixture, "Simple Class") {}


} // namespace
