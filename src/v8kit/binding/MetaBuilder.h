#pragma once
#include "Adapter.h"
#include "ReturnValuePolicy.h"
#include "traits/FunctionTraits.h"
#include "v8kit/binding/traits/TypeTraits.h"
#include "v8kit/core/Concepts.h"
#include "v8kit/core/MetaInfo.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>


namespace v8kit::binding {


enum class ConstructorKind {
    kNone,    // 默认状态, 未设置状态
    kNormal,  // 默认绑定构造
    kCustom,  // 自定义/自行处理构造逻辑
    kDisabled // 禁止Js构造,自动生成空构造回调
};

template <typename T, ConstructorKind K = ConstructorKind::kNone>
class ClassMetaBuilder {
    std::string                               name_;
    std::vector<StaticMemberMeta::Property>   staticProperty_;
    std::vector<StaticMemberMeta::Function>   staticFunctions_;
    std::vector<InstanceMemberMeta::Property> instanceProperty_;
    std::vector<InstanceMemberMeta::Method>   instanceFunctions_;
    ClassMeta const*                          base_ = nullptr;

    ConstructorCallback              userDefinedConstructor_ = nullptr;
    std::vector<ConstructorCallback> constructors_           = {};

    ClassMeta::UpcasterCallback upcaster_ = nullptr;

    static constexpr bool isInstanceClass = !std::is_void_v<T>;

    template <ConstructorKind OtherState>
    explicit ClassMetaBuilder(ClassMetaBuilder<T, OtherState>&& other) noexcept
    : name_(std::move(other.className_)),
      staticProperty_(std::move(other.staticProperty_)),
      staticFunctions_(std::move(other.staticFunctions_)),
      instanceProperty_(std::move(other.instanceProperty_)),
      instanceFunctions_(std::move(other.instanceFunctions_)),
      base_(other.base_),
      userDefinedConstructor_(std::move(other.userDefinedConstructor_)),
      constructors_(std::move(other.constructors_)),
      upcaster_(other.upcaster_) {
        // note: other may be in moved-from state
    }

    template <typename, ConstructorKind>
    friend class ClassMetaBuilder;

public:
    /**
     * @param name class name
     * @note support namespace like `a.b.c.ClassName`
     * @note class name cannot start or end with '.'
     */
    explicit ClassMetaBuilder(std::string_view name) : name_{name} {
        if (name.empty()) {
            throw std::invalid_argument("class name cannot be empty");
        }
        if (name.front() == '.' || name.back() == '.') {
            throw std::invalid_argument("class name cannot start or end with '.'");
        }
        bool prev_dot = false;
        for (char c : name) {
            if (c == '.') {
                if (prev_dot) {
                    throw std::invalid_argument("class name cannot contain consecutive '.'");
                }
                prev_dot = true;
            } else {
                prev_dot = false;
            }
        }
    }

    // func -> any static function or free function
    // var -> any static property
    // var_readonly -> readonly static property
    // ctor -> constructor
    // method -> any instance method
    // prop -> any instance property
    // prop_readonly -> readonly instance property

    auto& func(std::string name, FunctionCallback fn) {
        staticFunctions_.emplace_back(std::move(name), std::move(fn));
        return *this;
    }

    template <typename Fn>
    auto& func(std::string name, Fn&& fn, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::isFunctionCallback_v<Fn>)
    {
        auto f = adapter::wrapFunction(std::forward<Fn>(fn), policy);
        staticFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    template <typename... Fn>
    auto& func(std::string name, Fn&&... fn)
        requires(sizeof...(Fn) > 1)
    {
        auto f = adapter::wrapOverloadFuncAndExtraPolicy(std::forward<Fn>(fn)...);
        staticFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    auto& var(std::string name, GetterCallback getter, SetterCallback setter) {
        staticProperty_.emplace_back(std::move(name), std::move(getter), std::move(setter));
        return *this;
    }

    template <typename G, typename S>
    auto& var(std::string name, G&& getter, S&& setter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::isGetterCallback_v<G> && (!traits::isSetterCallback_v<S> || std::is_null_pointer_v<S>))
    {
        auto g = adapter::wrapGetter(std::forward<G>(getter), policy);
        auto s = adapter::wrapSetter(std::forward<S>(setter));
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename Ty>
    auto& var(std::string name, Ty&& value, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!concepts::Callable<Ty>)
    {
        auto [g, s] = adapter::wrapStaticMember(std::forward<Ty>(value), policy);
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename Ty>
    auto& var_readonly(std::string name, Ty&& value, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!concepts::Callable<Ty>)
    {
        auto [g, s] = adapter::wrapStaticMember<Ty, true>(std::forward<Ty>(value), policy);
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename G>
    auto& var_readonly(std::string name, G&& getter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(concepts::Callable<G>)
    {
        if constexpr (traits::isGetterCallback_v<G>) {
            return var(std::move(name), std::forward<G>(getter), nullptr);
        } else {
            auto get = adapter::wrapGetter(std::forward<G>(getter), policy);
            staticProperty_.emplace_back(std::move(name), std::move(get), nullptr);
            return *this;
        }
    }


    // -----------
    // instance
    // -----------

    // auto ctor(std::nullptr_t)
    //     requires(isInstanceClass && K == ConstructorKind::kNone)
    // {
    //     userDefinedConstructor_ = [](Arguments const&) { return nullptr; };
    //     ClassMetaBuilder<T, ConstructorKind::kDisabled> builder{std::move(*this)};
    //     return builder; // NRVO/move
    // }
    //
    // auto ctor(ConstructorCallback fn)
    //     requires(isInstanceClass && K == ConstructorKind::kNone)
    // {
    //     userDefinedConstructor_ = std::move(fn);
    //     ClassMetaBuilder<T, ConstructorKind::kCustom> builder{std::move(*this)};
    //     return builder; // NRVO/move
    // }
    //
    // template <typename... Args>
    // decltype(auto) ctor()
    //     requires(isInstanceClass && (K == ConstructorKind::kNone || K == ConstructorKind::kNormal))
    // {
    //     static_assert(std::is_constructible_v<T, Args...>, "Class must be constructible from Args...");
    //     static_assert(!std::is_aggregate_v<T>, "Binding ctor requires explicit constructor, not aggregate class");
    //
    //     if constexpr (K == ConstructorKind::kNormal) {
    //         return *this;
    //     } else {
    //         ClassMetaBuilder<T, ConstructorKind::kNormal> builder{std::move(*this)};
    //         return builder; // NRVO/move
    //     }
    // }
    //
    // template <typename P>
    // auto& inherit(ClassMeta const& meta)
    //     requires isInstanceClass // 仅实例类允许继承
    // {
    //     if (base_) { // 重复继承
    //         throw std::invalid_argument("class can only inherit one base class");
    //     }
    //     std::type_index type = typeid(P);
    //     if (meta.typeId_ != type) { // 拿错类元信息?
    //         throw std::invalid_argument("base class meta mismatch");
    //     }
    //     if (!meta.hasConstructor()) { // 父类是静态类
    //         throw std::invalid_argument("base class has no constructor");
    //     }
    //     static_assert(std::derived_from<T, P>, "Illegal inheritance relationship");
    //
    //     base_     = &meta;
    //     upcaster_ = [](void* ptr) -> void* {
    //         T* derived = static_cast<T*>(ptr);
    //         P* base    = static_cast<P*>(derived);
    //         return base;
    //     };
    //     return *this;
    // }


    [[nodiscard]] ClassMeta build() {

        InstanceMemberMeta::InstanceEqualsCallback equalsCallback = nullptr;
        InstanceMemberMeta::CopyCloneCtor          copyCloneCtor  = nullptr;
        InstanceMemberMeta::MoveCloneCtor          moveCloneCtor  = nullptr;
        if constexpr (isInstanceClass) {
            equalsCallback = adapter::bindInstanceEquals<T>();
            if constexpr (std::is_copy_constructible_v<T>) {
                copyCloneCtor = [](const void* src) -> void* {
                    // 把 void* 强转回确切的子类 T*，调用 T 的拷贝构造
                    return new T(*static_cast<const T*>(src));
                };
            }
            if constexpr (std::is_move_constructible_v<T>) {
                moveCloneCtor = [](void* src) -> void* { return new T(std::move(*static_cast<T*>(src))); };
            }
        }

        return ClassMeta{
            std::move(name_),
            StaticMemberMeta{std::move(staticProperty_), std::move(staticFunctions_)},
            InstanceMemberMeta{
                             std::move(userDefinedConstructor_),
                             std::move(instanceProperty_),
                             std::move(instanceFunctions_),
                             traits::size_of_v<T>,
                             equalsCallback, copyCloneCtor,
                             moveCloneCtor
            },
            base_,
            std::type_index{typeid(T)},
            upcaster_
        };
    }
};


template <typename T>
    requires std::is_enum_v<T>
class EnumMetaBuilder {
    std::string                  name_;
    std::vector<EnumMeta::Entry> entries_;

public:
    explicit EnumMetaBuilder(std::string_view name) : name_{name} {}

    EnumMetaBuilder& value(std::string name, T e) {
        entries_.emplace_back(std::move(name), static_cast<int64_t>(e));
        return *this;
    }

    [[nodiscard]] EnumMeta build() { return EnumMeta{std::move(name_), std::move(entries_)}; }
};


template <typename T>
auto defClass(std::string_view name) {
    return ClassMetaBuilder<T>{name};
}
template <typename T>
auto defEnum(std::string_view name) {
    return EnumMetaBuilder<T>{name};
}


} // namespace v8kit::binding