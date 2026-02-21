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


namespace ut {

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
        engine->eval(String::newString("new DisableCtorSimpleClass()")),
        Exception,
        Catch::Matchers::Message("Uncaught Error: This native class cannot be constructed.")
    );

    // 访问 C++ 已有实例
    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getName() == 'hello'", "initial name check");

    // 确认引用语义
    engine->eval(String::newString(R"(
        let obj = DisableCtorSimpleClass.inst;
        obj.setName("world");
    )"));

    const auto& cpp_instance = getSimpleClassConst();
    REQUIRE(cpp_instance.getName() == "world");

    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getName() == 'world'", "modified name check");

    REQUIRE_NOTHROW(engine->eval(String::newString(R"(
        DisableCtorSimpleClass.inst.setId(123456);
    )")));
    REQUIRE(cpp_instance.getId() == 123456);
    REQUIRE_EVAL("DisableCtorSimpleClass.inst.getId() == 123456", "modified id check");

    // 确认 const 语义被保留
    REQUIRE_THROWS_MATCHES(
        engine->eval(String::newString("DisableCtorSimpleClass.inst_const.setId(123456);")),
        Exception,
        Catch::Matchers::Message("Uncaught Error: Cannot unwrap const instance to mutable pointer")
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
        engine->eval(String::newString("new BindCtorSimpleClass()")),
        Exception,
        Catch::Matchers::Message("Uncaught Error: This native class cannot be constructed.")
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
        engine->eval(String::newString("new CustomCtorSimpleClass()")),
        Exception,
        Catch::Matchers::Message("Uncaught Error: This native class cannot be constructed.")
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

    REQUIRE_NOTHROW(engine->eval(String::newString("new MessageStream().write('hello').write(123).str() == 'hello123'"))
    );

    REQUIRE_EVAL("new MessageStream().write('test').str() == 'test'", "string overload check");
    REQUIRE_EVAL("new MessageStream().write(456).str() == '456'", "int overload check");
}


// TODO:
// ### 4.1.2 普通类继承绑定
// - 测试点：
//     - 子类可自动向上转为父类
//     - 子类可调用父类方法
//     - 类型信息不丢失
//     - 方法、属性继承正确
// - 判定标准：脚本层 `instanceof` 完全符合 C++ 继承关系

// ### 4.1.3 接口/抽象类绑定
// - 测试点：
//     - 禁止 JS 直接 `new`
//     - 只能通过 C++ 返回指针/引用
//     - 不生成拷贝、赋值逻辑
// - 判定标准：JS 构造抛异常，只能以指针形式存在

// ### 4.1.4 pImpl 类绑定兼容
// - 测试点：
//     - 外部类不触发浅拷贝/深拷贝
//     - 编译不报 `incomplete type`
//     - 不提前析构内部实现
// - 判定标准：编译通过、运行稳定、无内存错误

// ## 4.2 多态与继承安全
// ### 4.2.1 多态类绑定
// - 测试点：
//     - `Base*` 指向子类时能自动识别真实类型
//     - 多态钩子生效
//     - `dynamic_cast<void*>` 正确
// - 判定标准：脚本层能看到完整子类类型

// ### 4.2.2 多继承绑定
// - 测试点：
//     - 从任意父类 unwrap 都能得到正确真实对象
//     - 指针偏移计算正确
//     - 不出现野指针、交叉强转崩溃
// - 判定标准：所有父类指针转换稳定、结果一致

// ## 4.3 脚本行为与语义
// ### 4.3.1 脚本层继承关系验证
// - 测试点：
//     - `obj instanceof Base`
//     - `obj instanceof Derived`
//     - 原型链正确
// - 判定标准：与 C++ 继承树完全一致

// ### 4.3.2 实例等同比较 `$equals`
// - 测试点：
//     - 同一 C++ 实例 → `true`
//     - 不同实例 → `false`
//     - 不比较 JS 对象地址
// - 判定标准：按底层原生指针比较

// ### 4.3.4 Return Value Policy 全覆盖
// - 测试点：
//     - `reference`：不接管、不释放
//     - `copy`：脚本侧独立对象
//     - `move`：原指针置空/失效
//     - `take_ownership`：脚本 GC 后自动析构
//     - `reference_internal`：宿主存活则有效
// - 判定标准：所有权、生命周期、泄漏、有效性全部符合预期



} // namespace ut
