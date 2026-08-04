#include "catch2/matchers/catch_matchers_string.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"


#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"

#include <iostream>


namespace ut {

using namespace jspp;
using namespace jspp::binding;

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

    static void check(std::string const& a, std::string const& b) { REQUIRE(a == b); }
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
        engine->globalThis().set(
            String::newString("check"),
            Function::newFunction(cpp_func(&ScriptEvalAssertContext::check))
        );
    }
};

// REQUIRE_EVAL("Foo.x", "${Foo.x}")
#define REQUIRE_EVAL(COND, MSG)                                                                                        \
    {                                                                                                                  \
        ScriptEvalAssertContext::CurrentRunningLine    = __LINE__;                                                     \
        ScriptEvalAssertContext::CurrentScriptEvalCode = MOUNT_FUNC_NAME "(" COND ", `" MSG "`)";                      \
        try {                                                                                                          \
            engine->evalScript(String::newString(ScriptEvalAssertContext::CurrentScriptEvalCode));                     \
        } catch (...) {                                                                                                \
            ScriptEvalAssertContext::handler.emplace(                                                                  \
                "REQUIRE"_catch_sr,                                                                                    \
                Catch::SourceLineInfo(__FILE__, ScriptEvalAssertContext::CurrentRunningLine),                          \
                Catch::StringRef(                                                                                      \
                    ScriptEvalAssertContext::CurrentScriptEvalCode.data(),                                             \
                    ScriptEvalAssertContext::CurrentScriptEvalCode.size()                                              \
                ),                                                                                                     \
                Catch::ResultDisposition::Normal                                                                       \
            );                                                                                                         \
            ScriptEvalAssertContext::handler->handleUnexpectedInflightException();                                     \
        }                                                                                                              \
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
    auto result = engine->evalScript(String::newString("StaticClass.add(1, 2)"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 3);

    result = engine->evalScript(String::newString("StaticClass.add2(1, 2)"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 3);

    result = engine->evalScript(String::newString("StaticClass.append('hello', 'world')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "helloworld");

    result = engine->evalScript(String::newString("StaticClass.append('hello', 123)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "hello123");

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("StaticClass.append(123, 'world')")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("no overload found"))
    );


    // .var
    // test for GetterCallback and SetterCallback
    engine->evalScript(String::newString("StaticClass.script_name = 'new name'"));
    REQUIRE(StaticClass::name == "new name");
    REQUIRE_EVAL("StaticClass.script_name == 'new name'", "${StaticClass.script_name}")

    // test for wrap native Getter & Setter
    StaticClass::name = "test";
    REQUIRE_EVAL("StaticClass.native_name == 'test'", "${StaticClass.native_name}")
    engine->evalScript(String::newString("StaticClass.native_name = 'foo'"));
    REQUIRE_EVAL("StaticClass.native_name == 'foo'", "${StaticClass.native_name}")
    REQUIRE(StaticClass::name == "foo");

    // test for auto gen getter & setter
    StaticClass::name = "test";
    REQUIRE_EVAL("StaticClass.auto_name == 'test'", "${StaticClass.auto_name}")
    engine->evalScript(String::newString("StaticClass.auto_name = 'foo'"));
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

    static Local<Value> getIdScript(InstancePayload& payload, Arguments const& /*arguments*/) {
        auto t = payload.unwrap<SimpleClass>();
        if (!t) throw Exception{"Accessing destroyed instance"};
        return Number::newNumber(t->id_);
    }

    int  getId() const { return id_; }
    void setId(int id) { id_ = id; }

    std::string const& getName() const { return name_; }
    void               setName(std::string name) { name_ = std::move(name); }
};


SimpleClass& getSimpleClass() {
    static SimpleClass instance{"hello"};
    return instance;
}
SimpleClass const& getSimpleClassConst() { return getSimpleClass(); }

auto DisableCtorTestMeta = defClass<SimpleClass>("DisableCtorSimpleClass")
                               .ctor(nullptr)
                               // 这里的 readonly 是相对于 JS 属性而非对象
                               .var_readonly(
                                   "inst",
                                   &getSimpleClass,
                                   // 默认对于左值是拷贝，但是对于全局单例，显式指定引用。
                                   ReturnValuePolicy::kReference
                               )
                               // 这里的 readonly 是真只读，保留了 const 修饰
                               .var_readonly("inst_const", &getSimpleClassConst, ReturnValuePolicy::kReference)
                               .method("getId", &SimpleClass::getId)
                               .method("setId", &SimpleClass::setId)
                               .method("getName", &SimpleClass::getName)
                               .method("setName", &SimpleClass::setName)
                               .build();
TEST_CASE_METHOD(BindingTestFixture, "Disallow script constructor and verify real reference") {
    EngineScope scope{engine.get()};
    engine->registerClass(DisableCtorTestMeta);

    // 不允许脚本构造
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("new DisableCtorSimpleClass()")),
        Exception,
        Catch::Matchers::MessageMatches(
            Catch::Matchers::ContainsSubstring("This native class cannot be constructed.") //
        )
    );

    // 访问 C++ 已有实例
    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getName() == 'hello'", "initial name check");

    // 确认引用语义
    engine->evalScript(String::newString(R"(
        let obj = DisableCtorSimpleClass.inst;
        obj.setName("world");
    )"));

    const auto& cpp_instance = getSimpleClassConst();
    REQUIRE(cpp_instance.getName() == "world");

    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getName() == 'world'", "modified name check");

    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        DisableCtorSimpleClass.inst.setId(123456);
    )")));
    REQUIRE(cpp_instance.getId() == 123456);
    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getId() == 123456", "modified id check");

    // 确认 const 语义被保留
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("DisableCtorSimpleClass.inst_const.setId(123456);")),
        Exception,
        Catch::Matchers::MessageMatches(
            Catch::Matchers::ContainsSubstring("Cannot unwrap const instance to mutable pointer")
        )
    );
}


auto BindCtorTestMeta = defClass<SimpleClass>("BindCtorSimpleClass")
                            .ctor<std::string>()
                            .ctor<int, std::string>()
                            .method("getId", &SimpleClass::getId)
                            .method("setId", &SimpleClass::setId)
                            .method("getName", &SimpleClass::getName)
                            .method("setName", &SimpleClass::setName)
                            .build();
TEST_CASE_METHOD(BindingTestFixture, "bind overload constructor") {
    EngineScope scope{engine.get()};
    engine->registerClass(BindCtorTestMeta);

    REQUIRE_EVAL(
        "new BindCtorSimpleClass('hello').getName() == 'hello'",
        "call SimpleClass constructor with 1 arguments"
    );
    REQUIRE_EVAL(
        "new BindCtorSimpleClass(123, 'hello').getId() == 123",
        "call SimpleClass constructor with 2 arguments"
    );
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("new BindCtorSimpleClass()")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("This native class cannot be constructed."))
    );
}


auto CustomCtorTestMeta = defClass<SimpleClass>("CustomCtorSimpleClass")
                              .ctor([](Arguments const& arguments) -> std::unique_ptr<NativeInstance> {
                                  if (arguments.length() == 1) {
                                      auto str = arguments[0].asString();
                                      return factory::newNativeInstance<SimpleClass>(str.getValue());
                                  }
                                  if (arguments.length() == 2) {
                                      auto id  = arguments[0].asNumber().getInt32();
                                      auto str = arguments[1].asString();
                                      return factory::newNativeInstance<SimpleClass>(id, str.getValue());
                                  }
                                  return nullptr;
                              })
                              .method("getId", &SimpleClass::getId)
                              .method("setId", &SimpleClass::setId)
                              .method("getName", &SimpleClass::getName)
                              .method("setName", &SimpleClass::setName)
                              .build();
TEST_CASE_METHOD(BindingTestFixture, "bind custom constructor") {
    EngineScope scope{engine.get()};
    engine->registerClass(CustomCtorTestMeta);
    REQUIRE_EVAL(
        "new CustomCtorSimpleClass('hello').getName() == 'hello'",
        "call SimpleClass constructor with 1 arguments"
    );
    REQUIRE_EVAL(
        "new CustomCtorSimpleClass(123, 'hello').getId() == 123",
        "call SimpleClass constructor with 2 arguments"
    );
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("new CustomCtorSimpleClass()")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("This native class cannot be constructed."))
    );
}


// 验证重载和 Builder 模式兼容性
class MessageStream {
    std::ostringstream oss;

public:
    MessageStream() = default;

    MessageStream& write(std::string_view str) {
        oss << str;
        return *this;
    }
    MessageStream& write(int num) {
        oss << num;
        return *this;
    }
    std::string str() const { return oss.str(); }
};
auto MessageStreamMeta =
    defClass<MessageStream>("MessageStream")
        .ctor()
        .method(
            "write",
            static_cast<MessageStream& (MessageStream::*)(std::string_view)>(&MessageStream::write),
            static_cast<MessageStream& (MessageStream::*)(int)>(&MessageStream::write)
        )
        .method("str", &MessageStream::str)
        .build();
TEST_CASE_METHOD(BindingTestFixture, "overload and builder mode compatibility") {
    EngineScope scope{engine.get()};
    engine->registerClass(MessageStreamMeta);

    REQUIRE_NOTHROW(
        engine->evalScript(String::newString("new MessageStream().write('hello').write(123).str() == 'hello123'"))
    );

    REQUIRE_EVAL("new MessageStream().write('test').str() == 'test'", "string overload check");
    REQUIRE_EVAL("new MessageStream().write(456).str() == '456'", "int overload check");
}


// 瞬态资源回调安全
class EventBase {
    int id{0};

public:
    EventBase(int id) { this->id = id; }

    int getId() const { return id; }

    static void listen(std::function<void(EventBase const&)> cb) {
        EventBase eventBase{1234};
        cb(eventBase);
    }
};
auto EventBaseMeta = defClass<EventBase>("EventBase")
                         .ctor(nullptr)
                         .method("getId", &EventBase::getId)
                         // bind callback func
                         .func("listen", &EventBase::listen)
                         .build();

TEST_CASE_METHOD(BindingTestFixture, "callback function with transient resource") {
    EngineScope scope{engine.get()};
    engine->registerClass(EventBaseMeta);

    // 同步逃逸测试
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let escaped;
            EventBase.listen((e) => {
                escaped = e; // 逃逸到外部作用域
            });

            // 此时 EventBase.listen 已经执行完毕
            // TransientObjectScope 已经析构，escaped 应该被标记为失效
            escaped.getId();
            throw new Error("Should not reach here");
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance of type"))
    );
}


class PropTest {
public:
    int         id_{0};
    std::string name_;

    PropTest(int id, std::string name) : id_{id}, name_{std::move(name)} {}

    PropTest(PropTest const&)            = delete;
    PropTest& operator=(PropTest const&) = delete;

    int getId() const { return id_; }

    void setId(int id) { id_ = id; }

    std::string getName() const { return name_; }

    void setName(std::string name) { name_ = std::move(name); }
};
auto PropTestMeta = defClass<PropTest>("PropTest")
                        .ctor<int, std::string>()
                        // getter / setter
                        .prop("id", &PropTest::getId, &PropTest::setId)
                        .prop("name", &PropTest::getName, &PropTest::setName)
                        // member pointer
                        .prop("id_", &PropTest::id_)
                        .prop("name_", &PropTest::name_)
                        // always readonly
                        .prop_readonly("readonly_id", &PropTest::getId)
                        .prop_readonly("readonly_name", &PropTest::name_)
                        // non member function pointer getter, e.g. lambda or function
                        .prop_readonly("non_member_get_id", [](PropTest const& self) { return self.id_; })
                        .prop(
                            "non_member_get_set",
                            [](PropTest const& self) {
                                return self.id_; //
                            },
                            [](PropTest& self, int id) {
                                self.id_ = id; //
                            }
                        )
                        .build();
TEST_CASE_METHOD(BindingTestFixture, "bind property") {
    EngineScope scope{engine.get()};

    engine->registerClass(PropTestMeta);

    // getter / setter
    REQUIRE_EVAL("new PropTest(123, 'hello').id == 123", "constructor check");

    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let obj = new PropTest(123, 'hello');
        obj.id = 456;
        if (obj.id != 456) throw new Error("id should be 456");
    )")));
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let obj2 = new PropTest(123, 'hello');
        obj2.name = 'world';
        if (obj2.name != 'world') throw new Error("name should be 'world'");
    )")));

    // member pointer
    REQUIRE_EVAL("new PropTest(123, 'hello').id_ == 123", "constructor check");
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let obj3 = new PropTest(123, 'hello');
        obj3.id_ = 456;
        if (obj3.id_ != 456) throw new Error("id_ should be 456");
    )")));

    // readonly
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            "use strict";
            let obj4 = new PropTest(123, 'hello');
            obj4.readonly_id = 456;
            if (obj4.readonly_id != 123) throw new Error("readonly_id should be 123");
        )")),
        Exception,
        Catch::Matchers::MessageMatches(
            Catch::Matchers::ContainsSubstring(
                "Cannot set property readonly_id of #<PropTest> which has only a getter" // v8
            )
            || Catch::Matchers::ContainsSubstring("no setter for property") // quickjs
        )
    );

    // non member function pointer getter, e.g. lambda or function
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let obj5 = new PropTest(123, 'hello');
        if (obj5.non_member_get_id != 123) {
            throw new Error("non_member_get_id should be 123");
        }
    )")));
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let obj6 = new PropTest(123, 'hello');
        const expected = obj6.non_member_get_set + 1;
        obj6.non_member_get_set = expected;
        if (obj6.non_member_get_set != expected) {
            throw new Error(`non_member_get_set should be ${expected}, current: ${obj6.non_member_get_set}`);
        }
    )")));
}


// 智能指针兼容测试
class SmartPointerTest {
public:
    int id_{0};

    SmartPointerTest(int id) : id_{id} {}

    SmartPointerTest(SmartPointerTest const&)            = delete;
    SmartPointerTest& operator=(SmartPointerTest const&) = delete;

    static std::unique_ptr<SmartPointerTest> withUnique(int id) { return std::make_unique<SmartPointerTest>(id); }
    static std::shared_ptr<SmartPointerTest> withShared() {
        static std::shared_ptr<SmartPointerTest> shared = std::make_shared<SmartPointerTest>(0);
        return shared;
    }
    static std::weak_ptr<SmartPointerTest> withWeak() { return withShared(); }

    static void checkUnique(std::unique_ptr<SmartPointerTest> ptr, int reqId) {
        REQUIRE(ptr != nullptr);
        REQUIRE(ptr->id_ == reqId);
    }
    static void checkShared(std::shared_ptr<SmartPointerTest> ptr, int reqId) {
        REQUIRE(ptr != nullptr);
        REQUIRE(ptr->id_ == reqId);
    }
    static void checkWeak(std::weak_ptr<SmartPointerTest> ptr, int reqId) {
        auto p = ptr.lock();
        REQUIRE(p != nullptr);
        REQUIRE(p->id_ == reqId);
    }
};

auto SmartPointerTestMeta = defClass<SmartPointerTest>("SmartPtr")
                                .ctor(nullptr)
                                .prop("id", &SmartPointerTest::id_)
                                .func("withUnique", &SmartPointerTest::withUnique)
                                .func("withShared", &SmartPointerTest::withShared)
                                .func("withWeak", &SmartPointerTest::withWeak)
                                .func("checkUnique", &SmartPointerTest::checkUnique)
                                .func("checkShared", &SmartPointerTest::checkShared)
                                .func("checkWeak", &SmartPointerTest::checkWeak)
                                .build();

TEST_CASE_METHOD(BindingTestFixture, "smart pointer test") {
    EngineScope scope{engine.get()};
    engine->registerClass(SmartPointerTestMeta);

    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let shared = SmartPtr.withShared();
        shared.id = 123;
        SmartPtr.checkShared(shared, 123);
    )")));

    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let weak = SmartPtr.withWeak();
        weak.id = 8856;
        SmartPtr.checkWeak(weak, 8856);
    )")));

    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let unique = SmartPtr.withUnique(1478520);
        SmartPtr.checkUnique(unique, 1478520);
    )")));

    // 确认 unique ptr 所有权正确
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let unique_ptr = SmartPtr.withUnique(1478520);
            SmartPtr.checkUnique(unique_ptr, 1478520); // release ownership
            unique_ptr.id = 123; // uaf
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance of type"))
    );
}

// ==============================================================================
// 普通类继承绑定 & 脚本层继承关系验证
// ==============================================================================
class BaseObj {
public:
    int baseId         = 10;
    virtual ~BaseObj() = default;
    int getBaseId() const { return baseId; }
};

class DerivedObj : public BaseObj {
public:
    int derivedId = 20;
    int getDerivedId() const { return derivedId; }
};

auto BaseObjMeta = defClass<BaseObj>("BaseObj")
                       .ctor<>()
                       .prop("baseId", &BaseObj::baseId)
                       .method("getBaseId", &BaseObj::getBaseId)
                       .build();

auto DerivedObjMeta = defClass<DerivedObj>("DerivedObj")
                          .inherit<BaseObj>(BaseObjMeta) // 继承 BaseObj
                          .ctor<>()
                          .prop("derivedId", &DerivedObj::derivedId)
                          .method("getDerivedId", &DerivedObj::getDerivedId)
                          .build();

TEST_CASE_METHOD(BindingTestFixture, "Normal class inheritance & instanceof") {
    EngineScope scope{engine.get()};
    engine->registerClass(BaseObjMeta);
    engine->registerClass(DerivedObjMeta);

    // 测试点：脚本层 instanceof 完全符合 C++ 继承关系
    REQUIRE_EVAL("new DerivedObj() instanceof DerivedObj", "instanceof derived");
    REQUIRE_EVAL("new DerivedObj() instanceof BaseObj", "instanceof base");

    // 测试点：子类可调用父类方法、访问父类属性
    REQUIRE_EVAL("new DerivedObj().getBaseId() === 10", "call base method");
    REQUIRE_EVAL("new DerivedObj().baseId === 10", "access base property");

    // 测试点：子类自有方法与属性正常
    REQUIRE_EVAL("new DerivedObj().getDerivedId() === 20", "call derived method");
    REQUIRE_EVAL("new DerivedObj().derivedId === 20", "access derived property");
}


// ==============================================================================
// C++接口/抽象类绑定
// ==============================================================================
class IAbstractObject {
public:
    virtual ~IAbstractObject()         = default;
    virtual std::string doWork() const = 0;
};

class ImplAbstractObject : public IAbstractObject {
public:
    std::string doWork() const override { return "working"; }
};

IAbstractObject* getAbstractObject() {
    static ImplAbstractObject inst;
    return &inst;
}

auto IAbstractObjectMeta = defClass<IAbstractObject>("IAbstractObject")
                               .ctor(nullptr) // 禁止 JS 直接 new
                               .method("doWork", &IAbstractObject::doWork)
                               .build();

TEST_CASE_METHOD(BindingTestFixture, "Abstract class / Interface binding") {
    EngineScope scope{engine.get()};
    engine->registerClass(IAbstractObjectMeta);
    engine->globalThis().set(
        String::newString("getAbstract"),
        Function::newFunction(cpp_func(&getAbstractObject, ReturnValuePolicy::kReference))
    );

    // 测试点：禁止 JS 直接 new 抽象类/接口
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("new IAbstractObject()")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("This native class cannot be constructed."))
    );

    // 测试点：只能通过 C++ 返回指针/引用，并且方法可以被正确调用
    REQUIRE_EVAL("getAbstract().doWork() === 'working'", "call abstract method from pointer");
}


// ==============================================================================
// pImpl 类绑定兼容
// ==============================================================================
class PImplObj {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    PImplObj();
    ~PImplObj();
    int getValue() const;
};

struct PImplObj::Impl {
    int val = 999;
};
PImplObj::PImplObj() : impl_(std::make_unique<Impl>()) {}
PImplObj::~PImplObj() = default;
int PImplObj::getValue() const { return impl_->val; }

auto PImplObjMeta = defClass<PImplObj>("PImplObj").ctor<>().method("getValue", &PImplObj::getValue).build();

TEST_CASE_METHOD(BindingTestFixture, "pImpl class binding compatibility") {
    EngineScope scope{engine.get()};
    engine->registerClass(PImplObjMeta);

    // 测试点：外部类不触发浅拷贝/深拷贝错误，编译不报 incomplete type，运行无异常
    REQUIRE_EVAL("new PImplObj().getValue() === 999", "pImpl method call works safely");
}


// ==============================================================================
// 多态类绑定 & 多继承绑定
// ==============================================================================
class MIBase1 {
public:
    int val1           = 100;
    virtual ~MIBase1() = default;
    virtual int get1() { return val1; }
};

class MIBase2 {
public:
    int val2           = 200;
    virtual ~MIBase2() = default;
    virtual int get2() { return val2; }
};

class MIDerived : public MIBase1, public MIBase2 {
public:
    int val3 = 300;
    int get1() override { return val1 + 1; }
    int get2() override { return val2 + 2; }
    int get3() { return val3; }
};

// 暴露用于测试各种基类指针转型的工厂函数
MIBase1* getMIBase1() {
    static MIDerived d;
    return &d;
}
MIBase2* getMIBase2() {
    static MIDerived d;
    return &d;
}

auto MIBase1Meta   = defClass<MIBase1>("MIBase1").ctor<>().method("get1", &MIBase1::get1).build();
auto MIBase2Meta   = defClass<MIBase2>("MIBase2").ctor<>().method("get2", &MIBase2::get2).build();
auto MIDerivedMeta = defClass<MIDerived>("MIDerived")
                         .inherit<MIBase1>(MIBase1Meta) // V8 原型链单继承限制，我们只挂载 MIBase1
                         .ctor<>()
                         .method("get3", &MIDerived::get3)
                         .build();

TEST_CASE_METHOD(BindingTestFixture, "Polymorphism & Multiple inheritance safety") {
    EngineScope scope{engine.get()};
    engine->registerClass(MIBase1Meta);
    engine->registerClass(MIBase2Meta);
    engine->registerClass(MIDerivedMeta);
    engine->globalThis().set(
        String::newString("getMIBase1"),
        Function::newFunction(cpp_func(&getMIBase1, ReturnValuePolicy::kReference))
    );
    engine->globalThis().set(
        String::newString("getMIBase2"),
        Function::newFunction(cpp_func(&getMIBase2, ReturnValuePolicy::kReference))
    );

    // 测试点：Base* 指向子类时能自动识别真实类型，多态挂钩生效
    REQUIRE_EVAL("getMIBase1() instanceof MIDerived", "RTTI correctly downcasts to MIDerived");
    REQUIRE_EVAL("getMIBase1().get1() === 101", "virtual function override called correctly");
    REQUIRE_EVAL("getMIBase1().get3() === 300", "can call derived method because RTTI exposed full type");

    // 测试点：多继承的交叉/次级父类指针转型
    // 即便返回的是次要基类指针(Base2*)，jspp 也能基于 RTTI 将其定位到最底层子类 (MIDerived)
    // 并且通过 castTo 在底层完成偏移矫正，避免野指针崩溃。
    REQUIRE_EVAL("getMIBase2() instanceof MIDerived", "RTTI from secondary base safely downcasts to MIDerived");
    // 因为对象在 JS 中已被视作 MIDerived，而 MIDerived 继承自 MIBase1，
    // 所以即便是从 Base2 指针抛出来的对象，依然能够安全调用 Base1 上的方法（内存地址偏移正常转换）
    REQUIRE_EVAL("getMIBase2().get1() === 101", "Memory offset and VTable cross-cast stable");
}


// ==============================================================================
// Return Value Policy 全覆盖
// ==============================================================================
class RvpObj {
public:
    int val;
    RvpObj(int v) : val(v) {}
};

class RvpManager {
public:
    RvpObj internalObj{42};
    RvpManager() = default;
    RvpObj& getInternal() { return internalObj; }
    RvpObj  getCopy() { return RvpObj{100}; }
    RvpObj* getTakeOwnership() { return new RvpObj{200}; }
};

RvpObj  globalRvpObj{300};
RvpObj* getReference() { return &globalRvpObj; }

auto RvpObjMeta = defClass<RvpObj>("RvpObj").ctor<int>().prop("val", &RvpObj::val).build();

auto RvpManagerMeta = defClass<RvpManager>("RvpManager")
                          .ctor<>()
                          .method("getInternal", &RvpManager::getInternal, ReturnValuePolicy::kReferenceInternal)
                          .method("getCopy", &RvpManager::getCopy, ReturnValuePolicy::kCopy)
                          .method("getTakeOwnership", &RvpManager::getTakeOwnership, ReturnValuePolicy::kTakeOwnership)
                          .build();

TEST_CASE_METHOD(BindingTestFixture, "Return Value Policy coverage") {
    EngineScope scope{engine.get()};
    engine->registerClass(RvpObjMeta);
    engine->registerClass(RvpManagerMeta);
    engine->globalThis().set(
        String::newString("getReference"),
        Function::newFunction(cpp_func(&getReference, ReturnValuePolicy::kReference))
    );

    // 1. kReference: 引用现有对象，不接管生命周期
    REQUIRE_EVAL("getReference().val === 300", "kReference policy returns global object reference");

    // 2. kCopy: 创建独立的拷贝供 JS 使用
    REQUIRE_EVAL("new RvpManager().getCopy().val === 100", "kCopy policy creates standalone copy");

    // 3. kTakeOwnership: JS 获得动态分配指针的所有权，会在 GC 时释放
    REQUIRE_EVAL("new RvpManager().getTakeOwnership().val === 200", "kTakeOwnership dynamically takes C++ allocation");

    // 4. kReferenceInternal: 子对象生命周期与父对象绑定，保活机制启动
    REQUIRE_EVAL("new RvpManager().getInternal().val === 42", "kReferenceInternal keeps parent alive");

    // 5. kAutomatic 及 kMove 的特性在上述普通按值返回的 TypeConverter 逻辑中已被隐式测试到。
}


// enable_trampoline

class Bootstrap {
public:
    std::string pass{"unique"};

    Bootstrap() {}

    Bootstrap(Bootstrap const&) { pass = "copy"; }
    Bootstrap& operator=(Bootstrap const&) {
        pass = "copy";
        return *this;
    }
};
class Plugin {
public:
    virtual ~Plugin() = default;

    virtual bool onLoad(Bootstrap const&) { return false; }

    virtual bool onUnload() { return false; }
};
class JSPlugin : public Plugin, public enable_trampoline {
public:
    bool onLoad(Bootstrap const& boot) override {
        REQUIRE(getEngine() != nullptr);
        REQUIRE_FALSE(getThis().isNullOrUndefined());
        JSPP_OVERRIDE(bool, Plugin, "onLoad", onLoad, std::ref(boot));
    }
};
auto BootstrapMeta = defClass<Bootstrap>("Bootstrap").ctor<>().prop_readonly("pass", &Bootstrap::pass).build();
auto JSPluginMeta  = defClass<JSPlugin>("Plugin")
                        .ctor()
                        .implements<Plugin>()
                        .method("onLoad", &JSPlugin::onLoad)
                        .method("onUnload", &JSPlugin::onUnload)
                        .build();
TEST_CASE_METHOD(BindingTestFixture, "enable_trampoline") {
    EngineScope scope{engine.get()};

    engine->registerClass(BootstrapMeta);
    engine->registerClass(JSPluginMeta);

    engine->globalThis().set(
        String::newString("test"), //
        Function::newFunction(cpp_func([](Plugin& plugin) {
            auto boot = Bootstrap{};
            plugin.onLoad(boot);
            plugin.onUnload();
        }))
    );

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            class MyPlugin extends Plugin {
                constructor() {
                    super();
                }
                onLoad(boot) {
                    let raw = super.onLoad(boot);
                    if (raw !== false) {
                        // call 了基类，但基类没有实现，这里应该返回 false
                        throw new Error("raw !== false, call super.onLoad failed");
                    }
                    if (boot.pass !== "unique") {
                        // 意外的拷贝了 bootstrap
                        throw new Error("boot.pass !== 'unique'");
                    }
                    throw new Error("onLoad called");
                }
            }
            let plugin = new MyPlugin();
            test(plugin);
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("onLoad called"))
    );
}


} // namespace ut
