#pragma once
#include "traits/FunctionTraits.h"
#include "v8kit/binding/TypeConverter.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"


namespace v8kit::binding::adapter {

template <typename TargetT>
using ConverterRetType = decltype(toCpp<TargetT>(std::declval<Local<Value>>()));

template <typename TargetT>
struct StorageTypeDetector {
    using RetT = ConverterRetType<TargetT>;

    // 核心逻辑：
    // 1. 如果 Converter 返回左值引用 (Foo&)，说明对象已存在 -> Tuple 存 Foo&
    // 2. 如果 Converter 返回右值/值 (std::string, int)，说明是临时对象 -> Tuple 存 std::string (按值存储以保活)
    using type = std::conditional_t<
        std::is_lvalue_reference_v<RetT>,
        RetT,                     // Keep Ref
        std::remove_cvref_t<RetT> // Decay to Value (remove const/volatile/ref)
        >;
};

template <typename TargetT>
using StorageType_t = StorageTypeDetector<TargetT>::type;

template <typename Tuple, std::size_t... Is>
inline decltype(auto) ConvertArgsToTuple(Arguments const& args, std::index_sequence<Is...>) {
    using SafeTuple = std::tuple<StorageType_t<std::tuple_element_t<Is, Tuple>>...>;
    return SafeTuple{toCpp<std::tuple_element_t<Is, Tuple>>(args[Is])...};
}


// ---------------------
// Adapter
// ---------------------

// JavaScript lambda -> std::function
template <typename R, typename... Args>
decltype(auto) wrapScriptCallback(Local<Value> const& value) {
    if (!value.isFunction()) [[unlikely]] {
        throw Exception("expected function", Exception::Type::TypeError);
    }
    auto& engine = EngineScope::currentEngineChecked();

    Global<Function> global{value.asFunction()}; // keep alive
    return [keep = std::move(global), engine = &engine](Args&&... args) -> R {
        EngineScope lock{engine};

        auto function = keep.get();

        std::array<Value, sizeof...(Args)> argv{toJs(std::forward<Args>(args))...};
        if constexpr (std::is_void_v<R>) {
            function.call({}, argv);
            return;
        }
        return toCpp<R>(function.call({}, argv)); // TODO: 处理智能指针
    };
}

// C++ function -> JavaScript function
template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isFunctionCallback_v<Fn>) {
        return std::forward<Fn>(fn);
    }
    return [f = std::forward<Fn>(fn), policy](Arguments const& args) -> Local<Value> {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = typename Trait::ReturnType;
        using Tuple = typename Trait::ArgsTuple;

        constexpr auto Count = Trait::ArgsCount;
        if (args.length() != Count) [[unlikely]] {
            throw Exception("argument count mismatch", Exception ::Type::TypeError);
        }

        if constexpr (std::is_void_v<R>) {
            std::apply(f, ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<Count>()));
            return {}; // undefined
        } else {
            decltype(auto) ret = std::apply(f, ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<Count>()));
            return toJs(ret, policy, args.hasThiz() ? args.thiz() : Local<Value>{});
        }
    };
}


template <typename R, typename C, size_t Len, typename... Args>
R dispatchOverloadImpl(std::array<C, Len> const& overloads, Args&&... args) {
    // TODO: consider optimizing overload dispatch (e.g. arg-count lookup)
    // if we ever hit cases with >3 overloads. Current linear dispatch is ideal
    // for small sets and keeps the common path fast.
    for (size_t i = 0; i < Len; ++i) {
        try {
            return std::invoke(overloads[i], std::forward<Args>(args)...);
        } catch (Exception const&) {
            if (i == Len - 1) [[unlikely]] {
                throw Exception{"no overload found", Exception::Type::TypeError};
            }
        }
    }
    return R{};
}

template <size_t Len>
inline FunctionCallback _mergeFunctionCallbacks(std::array<FunctionCallback, Len> overloads) {
    return [fs = std::move(overloads)](Arguments const& args) -> Local<Value> {
        return dispatchOverloadImpl<Local<Value>>(fs, args);
    };
}

template <typename... Overload>
FunctionCallback wrapOverloadFunction(ReturnValuePolicy policy, Overload&&... fn) {
    std::array<FunctionCallback, sizeof...(Overload)> overloads = {wrapFunction(std::forward<Overload>(fn), policy)...};
    return _mergeFunctionCallbacks(std::move(overloads));
}

template <typename... Overload>
FunctionCallback wrapOverloadFuncAndExtraPolicy(Overload&&... fn) {
    constexpr size_t policy_count = (static_cast<size_t>(traits::is_policy<Overload>::value) + ...);
    static_assert(policy_count <= 1, "ReturnValuePolicy can only appear once in argument list");

    ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic;
    if constexpr (policy_count > 0) {
        (
            [&](auto&& arg) {
                if constexpr (traits::is_policy<decltype(arg)>::value) {
                    policy = arg;
                }
            }(fn),
            ...
        );
    }

    constexpr size_t func_count = sizeof...(Overload) - policy_count;
    static_assert(func_count > 0, "No functions provided to overload");

    auto overloads = [&]() {
        std::array<FunctionCallback, func_count> arr;
        size_t                                   idx = 0;
        (
            [&](auto&& arg) {
                if constexpr (!traits::is_policy<decltype(arg)>::value) {
                    arr[idx++] = wrapFunction(std::forward<decltype(arg)>(arg), policy);
                }
            }(std::forward<Overload>(fn)),
            ...
        );
        return arr;
    }();

    return _mergeFunctionCallbacks(std::move(overloads));
}


// C++ Getter / Setter -> JavaScript Getter / Setter
template <typename Fn>
GetterCallback wrapGetter(Fn&& getter, ReturnValuePolicy policy) {
    if constexpr (traits::isGetterCallback_v<Fn>) {
        return std::forward<Fn>(getter);
    }
    return [get = std::forward<Fn>(getter), policy]() -> Local<Value> {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = Trait::ReturnType;
        static_assert(!std::is_void_v<R>, "Getter must return a value");
        static_assert(Trait::ArgsCount == 0, "Getter must not take arguments");

        decltype(auto) value = std::invoke(get);
        return toJs(value, policy, {});
    };
}
template <typename Fn>
SetterCallback wrapSetter(Fn&& setter) {
    if constexpr (traits::isSetterCallback_v<Fn>) {
        return std::forward<Fn>(setter);
    }
    return [set = std::forward<Fn>(setter)](Local<Value> const& value) -> void {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = Trait::ReturnType;
        static_assert(std::is_void_v<R>, "Setter must not return a value");
        static_assert(Trait::ArgsCount == 1, "Setter must take one argument");

        using Args = Trait::ArgsTuple;
        using Type = std::tuple_element_t<0, Args>;
        std::invoke(set, toCpp<Type>(value));
    };
}

template <typename Ty, bool forceReadonly>
std::pair<GetterCallback, SetterCallback> wrapStaticMember(Ty&& member, ReturnValuePolicy policy) {
    static_assert(!std::is_member_pointer_v<std::remove_cvref_t<Ty>>);

    using RawType = std::remove_reference_t<Ty>;
    if constexpr (std::is_pointer_v<RawType>) {
        // Ty* / Ty const*
        using ValueType = std::remove_pointer_t<RawType>;

        GetterCallback getter = [member, policy]() -> Local<Value> {
            if (!member) throw Exception("Accessing null static member pointer");
            return toJs(*member, policy, {});
        };
        SetterCallback setter = nullptr;
        if constexpr (!std::is_const_v<ValueType> && !forceReadonly) {
            setter = [member](Local<Value> const& val) {
                if (!member) throw Exception("Accessing null static member pointer");
                *member = toCpp<ValueType>(val);
            };
        }
        return {std::move(getter), std::move(setter)};
    } else {
        // Ty
        GetterCallback getter = [val = std::forward<Ty>(member), policy]() -> Local<Value> {
            // 对常量的 toJs，policy 通常是 Copy (对于基础类型)
            // 如果是大对象，policy 可能是 Reference，但引用的将是 lambda 内部的 val
            return toJs(val, policy, {});
        };
        return {std::move(getter), nullptr};
    }
}


template <typename C, typename... Args>
ConstructorCallback bindConstructor() {
    return [](Arguments const& args) -> std::unique_ptr<NativeInstance> {
        constexpr size_t N = sizeof...(Args);
        if constexpr (N == 0) {
            static_assert(
                concepts::HasDefaultConstructor<C>,
                "Class C must have a no-argument constructor; otherwise, a constructor must be specified."
            );
            if (args.length() != 0) return nullptr; // Parameter mismatch
            // return v8wrap::internal::factory::makeOwnedRaw(new C());

        } else {
            if (args.length() != N) return nullptr; // Parameter mismatch

            using Tuple = std::tuple<Args...>;

            auto parameters = ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<N>());
            return std::apply(
                [](auto&&... unpackedArgs) {
                    auto rawPointer = new C(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                    // return v8wrap::internal::factory::makeOwnedRaw(rawPointer);
                },
                std::move(parameters)
            );
        }
    };
}

template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEqualsImpl(std::false_type) {
    return [](void* lhs, void* rhs) -> bool { return lhs == rhs; };
}
template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEqualsImpl(std::true_type) {
    return [](void* lhs, void* rhs) -> bool {
        if (!lhs || !rhs) return false;
        return *static_cast<C*>(lhs) == *static_cast<C*>(rhs);
    };
}
template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEquals() {
    // use tag dispatch to fix MSVC pre name lookup or overload resolution
    return bindInstanceEqualsImpl<C>(std::bool_constant<concepts::HasEquality<C>>{});
}


} // namespace v8kit::binding::adapter