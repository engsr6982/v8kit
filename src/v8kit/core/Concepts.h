#pragma once
#include "Fwd.h"

#include <string_view>

namespace v8kit ::concepts {

template <typename T>
concept NumberLike = std::is_arithmetic_v<T>;

template <typename T>
concept StringLike = std::convertible_to<T, std::string_view>;

template <typename T>
concept HasDefaultConstructor = requires { T{}; };

template <typename T>
concept HasEquality = requires(T const& lhs, T const& rhs) {
    { lhs == rhs } -> std::convertible_to<bool>;
};


template <typename T>
concept WrapType = std::derived_from<T, Value>;


} // namespace v8kit::concepts