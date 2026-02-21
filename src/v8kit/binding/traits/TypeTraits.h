#pragma once
#include <type_traits>

#include <string_view>

#include "v8kit/core/Concepts.h"

namespace v8kit::binding::traits {

template <typename T>
struct RawTypeHelper {
    using Decayed = std::decay_t<T>;

    // static_assert(
    //     !std::is_pointer_v<Decayed> || concepts::StringLike<Decayed>,
    //     "RawType does not allow raw pointer types except string-like"
    // );

    using type = std::conditional_t<
        concepts::StringLike<Decayed>,
        std::string_view,              // 命中 StringLike -> 强制映射为 string_view (fix const char[N])
        std::remove_pointer_t<Decayed> // 其他类型 -> 移除指针
        >;
};

template <typename T>
using RawType_t = typename RawTypeHelper<T>::type;

template <typename T>
inline constexpr size_t size_of_v = sizeof(T);

template <>
inline constexpr size_t size_of_v<void> = 0;


template <typename T>
inline constexpr bool isFunctionCallback_v = std::convertible_to<std::remove_cvref_t<T>, FunctionCallback>
                                          || std::is_invocable_r_v<Local<Value>, T, Arguments const&>;

template <typename T>
inline constexpr bool isGetterCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, GetterCallback> || std::is_invocable_r_v<Local<Value>, T>;

template <typename T>
inline constexpr bool isSetterCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, SetterCallback> || std::is_invocable_r_v<void, T, Local<Value> const&>;

template <typename T>
inline constexpr bool isInstanceMethodCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, InstanceMethodCallback>
    || std::is_invocable_r_v<Local<Value>, T, InstancePayload&, Arguments const&>;


// unique_ptr
template <typename T>
struct is_unique_ptr : std::false_type {};
template <typename T, typename D>
struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};

template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<std::remove_cvref_t<T>>::value;

// shared_ptr
template <typename T>
struct is_shared_ptr : std::false_type {};
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<std::remove_cvref_t<T>>::value;

// weak_ptr
template <typename T>
struct is_weak_ptr : std::false_type {};
template <typename T>
struct is_weak_ptr<std::weak_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_weak_ptr_v = is_weak_ptr<std::remove_cvref_t<T>>::value;


template <typename T>
struct pointee;

template <typename T, typename D>
struct pointee<std::unique_ptr<T, D>> {
    using type = T;
};

template <typename T>
struct pointee<std::shared_ptr<T>> {
    using type = T;
};

template <typename T>
using pointee_t = typename pointee<std::remove_cvref_t<T>>::type;


} // namespace v8kit::binding::traits