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


// ============================================================
// 成员指针 prop 回归（默认引用语义 / const 解包修复）
// ============================================================

class PermsPod {
public:
    int water{0};
    int land{0};

    PermsPod() = default;
    explicit PermsPod(int w) : water(w) {}
};
static auto PermsPodMeta = binding::defClass<PermsPod>("PermsPod")
                               .ctor<int>()
                               .prop("water", &PermsPod::water)
                               .build();

class LandTablePod {
public:
    PermsPod environment{3};
    PermsPod role{5};
    PermsPod readonlyEnv{7};

    LandTablePod() = default;
};
static auto LandTablePodMeta = binding::defClass<LandTablePod>("LandTablePod")
                                   .ctor<>()
                                   // 成员指针 prop 默认自动升级为 kReferenceInternal：
                                   // 类类型成员按引用返回，写回生效、宿主保活
                                   .prop("environment", &LandTablePod::environment)
                                   .prop("role", &LandTablePod::role)
                                   // 显式 kCopy：仍为拷贝语义，不写回原成员
                                   .prop("env_copy", &LandTablePod::environment, binding::ReturnValuePolicy::kCopy)
                                   // prop_readonly：始终只读
                                   .prop_readonly("readonlyEnv", &LandTablePod::readonlyEnv)
                                   .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: member pointer prop should reference nested POD member (write-back)", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // 修复前：kAutomatic 解析为 kCopy 且 const 解包 -> 抛 "Object is not copy constructible"
    auto result = engine->evalScript(String::newString(R"(
        let t = new LandTablePod();
        t.environment.water = 42; // 写回宿主成员
        t.role.water = 99;
        t.environment.water === 42 && t.role.water === 99;
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

TEST_CASE_METHOD(BugTestFixture, "Bug: member pointer prop explicit kCopy must not affect original", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // 显式 kCopy：修改作用于拷贝，不影响原成员
    auto result = engine->evalScript(String::newString(R"(
        let t = new LandTablePod();
        t.env_copy.water = 1;
        t.environment.water === 3;
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

// const 宿主（通过 const 引用暴露）-> 子成员包装必须只读
class ConstHolderPod {
public:
    LandTablePod pod_;

    ConstHolderPod() = default;
    LandTablePod const& getPod() const { return pod_; }
};
static auto ConstHolderPodMeta = binding::defClass<ConstHolderPod>("ConstHolderPod")
                                     .ctor<>()
                                     .prop("pod", &ConstHolderPod::getPod, nullptr, binding::ReturnValuePolicy::kReference)
                                     .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: const host must yield read-only member wrapper", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);
    engine->registerClass(ConstHolderPodMeta);

    // 宿主 const -> 成员引用只读，写入必须被拒绝
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let h = new ConstHolderPod();
            h.pod.environment.water = 1;
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Cannot unwrap const instance"))
    );
}

TEST_CASE_METHOD(BugTestFixture, "Bug: prop_readonly member pointer must stay read-only", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // prop_readonly 强制只读：即使宿主可变，成员引用也必须只读
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let t = new LandTablePod();
            t.readonlyEnv.water = 1;
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Cannot unwrap const instance"))
    );
}


// ============================================================
// const& 返回 + kCopy / lambda method 绑定 回归
// ============================================================

// 业务场景：POD 对象以 const& 返回，绑定层应拷贝出可变副本
class LandPerm {
public:
    int water{0};
    int land{0};

    LandPerm() = default;
    explicit LandPerm(int w) : water(w) {}
};
static auto LandPermMeta = binding::defClass<LandPerm>("LandPerm")
                               .ctor<int>()
                               .prop("water", &LandPerm::water)
                               .build();

class Land {
public:
    LandPerm table_{11};

    Land() = default;
    explicit Land(int w) : table_(w) {}
    LandPerm const& getPermTable() const { return table_; }
};
static auto LandMeta = binding::defClass<Land>("Land")
                           .ctor<int>()
                           // 成员函数指针 + 显式 kCopy：const& 返回 -> 可变副本（修复：kCopy 剥离 const）
                           .method("getTableCopy", &Land::getPermTable, binding::ReturnValuePolicy::kCopy)
                           // lambda 绑定 + kCopy：const& 返回 -> 可变副本（修复：method 支持 lambda）
                           .method(
                               "getTableLambda",
                               [](Land& self) -> LandPerm const& { return self.getPermTable(); },
                               binding::ReturnValuePolicy::kCopy
                           )
                           // lambda 绑定 + 默认策略：按值返回 -> kAutomatic 解析为 kCopy（修复：按值默认拷贝）
                           .method("getTableValue", [](Land& self) -> LandPerm {
                               return LandPerm{self.getPermTable().water + 1};
                           })
                           // lambda 绑定 + 参数（void 分支）
                           .method("setWater", [](Land& self, int w) { self.table_.water = w; })
                           // lambda builder 特判：返回 C& 时返回 thiz
                           .method("self", [](Land& self) -> Land& { return self; })
                           .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: const-ref + kCopy must produce mutable copy", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(LandPermMeta);
    engine->registerClass(LandMeta);

    // 修复前：const LandPerm& + kCopy -> ElementType 带 const -> 抛 "Object is not copy constructible"
    auto result = engine->evalScript(String::newString(R"(
        let l = new Land(5);
        let t = l.getTableCopy();
        t.water = 42;                 // 副本必须可写
        l.getTableCopy().water === 5; // 原对象不变（副本独立）
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

TEST_CASE_METHOD(BugTestFixture, "Bug: lambda method binding (const-ref kCopy / by-value automatic / args)", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(LandPermMeta);
    engine->registerClass(LandMeta);

    // 修复前：.method 模板按成员函数指针展开（(inst->*f)），lambda 无法编译
    // 各段脚本用 IIFE 包裹，避免共享全局作用域导致 let 重复声明
    // lambda + const& 返回 + kCopy -> 可变副本
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            let t = l.getTableLambda();
            t.water = 42;
            return t.water === 42;
        })()
    )")));
    // lambda + 按值返回 + 默认策略 -> kAutomatic 解析为 kCopy
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            let v = l.getTableValue();
            return v.water === 8;
        })()
    )")));
    // lambda + 参数（void 分支，写回宿主）
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            l.setWater(9);
            return l.getTableCopy().water === 9;
        })()
    )")));
    // builder 特判：lambda 返回 C& -> thiz
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            return l.self() === l;
        })()
    )")));
}


} // namespace
