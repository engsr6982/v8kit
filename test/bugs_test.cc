#include "catch2/matchers/catch_matchers_string.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/ReturnValuePolicy.h"
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
#include <memory>
#include <optional>

namespace {

using namespace jspp;

struct BugTestFixture {
    std::unique_ptr<jspp::Engine> engine;

    BugTestFixture() : engine(std::make_unique<jspp::Engine>()) {}
};

struct Color {
    int r, g, b, a;

    static Color const& RED() {
        static constexpr auto red = Color{255, 0, 0, 255};
        return red;
    }
};
class Shape {
public:
    Shape()             = default;
    using OptionalColor = std::optional<Color>;
    OptionalColor color;

    OptionalColor getColor() const { return color; }
    void          setColor(OptionalColor c) { color = c; }
};

TEST_CASE_METHOD(
    BugTestFixture,
    "Bug: TypeConverter value types require mutable pointer, rejecting const instances (e.g. Color.RED)",
    "[bugs]"
) {
    static auto colorMeta = jspp::binding::defClass<Color>("Color")
                                .ctor(nullptr)
                                .prop("r", &Color::r)
                                .prop("g", &Color::g)
                                .prop("b", &Color::b)
                                .prop("a", &Color::a)
                                .var_readonly("RED", &Color::RED, binding::ReturnValuePolicy::kReferencePersistent)
                                .build();
    static auto shapeMeta =
        jspp::binding::defClass<Shape>("Shape").ctor<>().prop("color", &Shape::getColor, &Shape::setColor).build();

    EngineScope lock{*engine};

    engine->registerClass(colorMeta);
    engine->registerClass(shapeMeta);

    REQUIRE_NOTHROW(engine->evalScript(String::newString("new Shape().color = Color.RED")));
}


struct Base {
    int       foo;
    int const bar = 42;
    Base(int foo) : foo(foo) {}
};
struct Derived : public Base {
    using Base::Base;
};
TEST_CASE_METHOD(
    BugTestFixture,
    "Bug: member pointer from derived class fails unwrap due to base class type mismatch",
    "[bugs]"
) {
    static auto Test =
        binding::defClass<Derived>("Derived").ctor<int>().prop("foo", &Derived::foo).prop("bar", &Base::bar).build();

    EngineScope lock{*engine};
    engine->registerClass(Test);

    REQUIRE_NOTHROW(engine->evalScript(String::newString("new Derived(1).foo = 42")));
    REQUIRE_THROWS(engine->evalScript(String::newString("\"use strict\"; new Derived(1).bar = 42")));
}


// ============================================================
// 瞬态作用域回归（TransientObjectScope：保活 + 溯源式跟踪）
// ============================================================

class Child {
    int id_{0};

public:
    explicit Child(int id) : id_{id} {}
    int getId() const { return id_; }
};
static auto ChildMeta = binding::defClass<Child>("Child").ctor<int>().method("getId", &Child::getId).build();

class EventWithChild {
    Child child_{7};

public:
    Child& getChild() { return child_; }

    static void listen(std::function<void(EventWithChild&)> cb) {
        EventWithChild ev;
        cb(ev);
    }
};
static auto EventWithChildMeta = binding::defClass<EventWithChild>("EventWithChild")
                                     .ctor(nullptr)
                                     // kReference 策略：回调内访问子属性会进入瞬态作用域托管
                                     .prop("child", &EventWithChild::getChild, nullptr, binding::ReturnValuePolicy::kReference)
                                     .func("listen", &EventWithChild::listen)
                                     .build();

// 场景 A：回调不持有事件参数。事件参数 wrapper 由 argv 持有引用计数，
// 作用域退出时 argv 先于 TransientObjectScope 析构 -> wrapper 被释放 (QuickJS 引用计数归零立即回收)
// -> InstancePayload/NativeInstance 被 finalizer 删除 -> TransientObjectScope 析构时
// 对已释放的 NativeInstance 调用 invalidate() -> use-after-free
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope UAF when callback does not retain event arg", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    // 回调体内完全不持有 e，也不访问任何子属性
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            EventWithChild.listen(() => { /* not retaining e */ });
        )"))
    );
}

// 场景 B：回调内访问子属性 (kReference) 并立即丢弃。
// wrapper 引用计数归零 -> 立即释放 -> NativeInstance 销毁，但仍在 trackedInstances_ 中
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope UAF when sub property dropped inside callback", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    // x 创建后立即被丢弃，未逃逸；作用域退出时 invalidate 命中的是已释放内存
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            EventWithChild.listen((e) => {
                let x = e.child; // kReference wrapper, 被瞬态作用域跟踪
                x = null;        // 立即丢弃 -> 引用计数归零 -> NativeInstance 被销毁
            });
        )"))
    );
}

// 场景 C：长期对象的成员在回调内被访问，不应被瞬态作用域误伤（溯源式跟踪）
class Holder {
    Child child_;

public:
    explicit Holder(int id) : child_(id) {}
    Child& getChild() { return child_; }
};
static auto HolderMeta = binding::defClass<Holder>("Holder")
                             .ctor<int>()
                             .prop("child", &Holder::getChild, nullptr, binding::ReturnValuePolicy::kReference)
                             .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope must not poison long-lived member accessed in callback", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);
    engine->registerClass(HolderMeta);

    // h 在瞬态作用域外创建（未被跟踪）；回调内访问 h.child（kReference，
    // parent=h 不在跟踪集合中）按溯源规则不进入托管，回调结束后引用必须仍然有效
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            let h = new Holder(9);
            let captured;
            EventWithChild.listen(() => { captured = h.child; });
            captured.getId() === 9;
        )"))
    );
}

// 阳性对照：事件对象的子属性从瞬态根派生，逃逸后仍必须失效（原有语义保持）
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope event child must stay poisoned after escape", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let escaped;
            EventWithChild.listen((e) => { escaped = e.child; });
            // 此时 TransientObjectScope 已析构，escaped 应被标记为失效
            escaped.getId();
            throw new Error("Should not reach here");
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance of type"))
    );
}




} // namespace
