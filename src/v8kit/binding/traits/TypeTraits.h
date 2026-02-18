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
inline constexpr bool isFunctionCallback =
    std::is_invocable_r_v<Local<Value>, T, Arguments const&> || std::convertible_to<T, v8kit::FunctionCallback>;


} // namespace v8kit::binding::traits