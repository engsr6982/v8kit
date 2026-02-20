#pragma once
#include "traits/TypeTraits.h"

#include <string>

namespace v8kit {
enum class ReturnValuePolicy : uint8_t;
}

namespace v8kit::binding {

/**
 * @brief 类型转换器
 * @tparam T C++ RawType (class Foo const& -> Foo -> TypeConverter<Foo>::toJs/toCpp)
 *
 * @example TypeConverter<Foo>::toJs(Foo* / Foo& / Foo const& / Foo value) -> Local<T>
 * @example TypeConverter<Foo>::toCpp(Local<Value> const& value) -> T* / T& / T
 */
template <typename T>
struct TypeConverter;

template <typename T>
using RawTypeConverter = TypeConverter<traits::RawType_t<T>>;

template <typename T>
concept HasTypeConverter = requires { typename RawTypeConverter<T>; };
template <typename T>
inline constexpr bool HasTypeConverter_v = HasTypeConverter<T>;


template <typename T>
struct BindingClassTag : std::false_type {};

template <typename T>
inline constexpr bool IsBindingClass_v = BindingClassTag<T>::value;

namespace internal {

/**
 * @brief C++ 值类型转换器
 * @note 此转换器设计目的是对于某些特殊情况，例如 void foo(std::string_view)
 *       在绑定时，TypeConverter 对字符串的特化是接受 StringLike，但返回值统一为 std::string
 *       这种特殊情况下，会导致 toCpp<std::string_view> 内部类型断言失败:
 * @code using RawConvRet = std::remove_cv_t<std::remove_reference_t<TypedToCppRet<std::string_view>>> // std::string
 * @code std::same_v<RawConvRet, std::string_view> // false
 *
 * @note 为了解决此问题，引入了 CppValueTypeTransformer，用于放宽类型约束
 * @note 需要注意的是 CppValueTypeTransformer 仅放宽了类型约束，实际依然需要特化 TypeConverter<T>
 */
template <typename Form, typename To>
struct CppValueTypeTransformer : std::false_type {};

template <>
struct CppValueTypeTransformer<std::string, std::string_view> : std::true_type {};

template <typename From, typename To>
inline constexpr bool CppValueTypeTransformer_v = CppValueTypeTransformer<From, To>::value;

} // namespace internal

/**
 * Convert C++ type to js type
 * @tparam T C++ type
 * @param val C++ value
 * @return Local<Value>
 * @note forward to RawTypeConverter<T>::toJs(T)
 */
template <typename T>
[[nodiscard]] Local<Value> toJs(T&& val);

/**
 * Convert js type to C++ type
 * @tparam T C++ type
 * @param val C++ value
 * @param policy Return value policy
 * @param parent Parent object
 * @return Local<Value>
 * @note if not RawTypeConverter<T>::toJs(T, policy, parent) is defined, forward to RawTypeConverter<T>::toJs(T)
 */
template <typename T>
[[nodiscard]] Local<Value> toJs(T&& val, ReturnValuePolicy policy, Local<Value> parent);

template <typename T>
[[nodiscard]] decltype(auto) toCpp(Local<Value> const& value);

} // namespace v8kit::binding

#include "TypeConverter.inl" // NOLINT
