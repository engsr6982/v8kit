#pragma once
#include "NativeInstanceImpl.h"
#include "ReturnValuePolicy.h"
#include "traits/Polymorphic.h"
#include "traits/TypeTraits.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"

#include <stdexcept>
#include <string>
#include <variant>

namespace v8kit::binding {

namespace detail {

template <typename T>
struct GenericTypeConverter;

} // namespace detail

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


// -----------------------------
// impl start
// -----------------------------

namespace detail {

struct ResolvedCastSource {
    const void*      ptr;           // 最终决定使用的 C++ 指针（可能经过了偏移）
    ClassMeta const* meta;          // 最终决定使用的 JS 类定义
    bool             is_downcasted; // 是否发生了成功的多态向下转型
};

template <typename T>
ResolvedCastSource resolveCastSource(T* value) {
    auto& engine = EngineScope::currentRuntimeChecked();

    // 1. 获取动态类型和基址
    const std::type_info* dynamicType = nullptr;
    const void*           dynamicPtr  = traits::PolymorphicTypeHook<T>::get(value, dynamicType);

    // 2. 尝试决议动态类型 (Downcast)
    if (dynamicType && dynamicPtr) {
        if (auto* meta = engine.getClassDefine(std::type_index(*dynamicType))) {
            return {dynamicPtr, meta, true}; // 完美命中子类
        }
    }

    // 3. Fallback 回退到静态类型 (Original)
    std::type_index staticIdx(typeid(T));
    auto*           staticMeta = engine.getClassDefine(staticIdx);
    if (!staticMeta) {
        throw Exception("Class not registered: " + std::string(staticIdx.name()));
    }

    return {static_cast<const void*>(value), staticMeta, false};
}

template <typename T>
struct GenericTypeConverter {
    template <typename U>
    static ReturnValuePolicy handleAutomaticPolicy(ReturnValuePolicy policy) {
        // @see ReturnValuePolicy::kAutomatic comment
        if (policy == ReturnValuePolicy::kAutomatic) {
            if constexpr (std::is_pointer_v<U>) {
                return ReturnValuePolicy::kTakeOwnership;
            } else if constexpr (std::is_lvalue_reference_v<U>) {
                return ReturnValuePolicy::kCopy;
            } else if constexpr (std::is_rvalue_reference_v<U>) {
                return ReturnValuePolicy::kMove;
            }
        }
        return policy;
    }

    // C++ -> JS
    static Local<Value> toJs(T&& value, ReturnValuePolicy policy, Local<Value> parent) {
        using RawType     = std::decay_t<T>;
        using ElementType = std::remove_pointer_t<RawType>;
        policy            = handleAutomaticPolicy<T>(policy);

        // 剥离引用/指针，获取底层裸指针进行多态决议
        ElementType* rawPtr = nullptr;
        if constexpr (std::is_pointer_v<RawType>) {
            rawPtr = value;
            if (!rawPtr) return Null::newNull();
        } else {
            rawPtr = &value;
        }

        auto resolved = detail::resolveCastSource<ElementType>(rawPtr);

        std::unique_ptr<NativeInstance> instance;

        auto createImpl = [&](auto&& holder) {
            using HolderT = std::decay_t<decltype(holder)>;
            // 这里我们统一把 holder 存为声明类型 ElementType，但赋予它多态的 Meta
            // C++ 层用基类指针操作，JS 层用子类 Prototype 操作，完美契合！
            return std::make_unique<NativeInstanceImpl<ElementType, HolderT>>(
                resolved.meta,
                std::forward<decltype(holder)>(holder)
            );
        };

        switch (policy) {
        case ReturnValuePolicy::kCopy:
            // 【注意切片问题】这里暂时只做基类拷贝，如果你需要完整多态拷贝，
            // 必须像 pybind11 那样在 Meta 中注册拷贝构造钩子，或者要求用户提供 clone()。
            if (resolved.is_downcasted) {
                // 如果发生了多态决议且要求 Copy，为了防止切片，你可以先抛错拦截，或者调用虚拟 clone()
                throw Exception("Copying polymorphic objects requires a clone mechanism to prevent slicing.");
            }
            if constexpr (std::is_copy_constructible_v<ElementType>) {
                instance = createImpl(std::make_unique<ElementType>(*rawPtr));
            } else {
                throw Exception("Object is not copy constructible");
            }
            break;

        case ReturnValuePolicy::kMove:
            // 同理，移动多态对象也会切片
            if constexpr (std::is_move_constructible_v<ElementType>) {
                instance = createImpl(std::make_unique<ElementType>(std::move(*rawPtr)));
            }
            break;

        case ReturnValuePolicy::kTakeOwnership:
            if constexpr (std::is_pointer_v<RawType>) {
                instance = createImpl(std::unique_ptr<ElementType>(value));
            } else {
                throw Exception("Cannot take ownership of non-pointer");
            }
            break;

        case ReturnValuePolicy::kReference:
        case ReturnValuePolicy::kReferenceInternal:
            // 引用策略最简单，直接持有裸指针
            instance = createImpl(rawPtr);
            break;

        default:
            break;
        }

        // 4. 交给引擎组装 JS 对象
        auto&         engine = EngineScope::currentRuntimeChecked();
        Local<Object> jsObj  = engine.newInstance(*resolved.meta, std::move(instance));

        // 5. Keep Alive 逻辑
        if (policy == ReturnValuePolicy::kReferenceInternal) {
            // ... 处理保活 ...
        }

        return jsObj;
    }

    // JS -> C++
    static T* toCpp(Local<Value> const& value) {
        // 获取 Payload
        // auto* payload = internal::getPayload(value);
        // if (!payload) throw Exception("Not a native instance");
        //
        // // 使用 unwrap，它会调用 NativeInstance::cast -> Meta::castTo
        // // 自动处理继承偏移
        // T* ptr = payload->getHolder()->unwrap<T>();
        // if (!ptr) throw Exception("Type mismatch or cast failed");
        // return ptr;
    }
};

} // namespace detail

template <typename T>
struct TypeConverter : detail::GenericTypeConverter<T> {};

// v8kit::Local<T>
template <typename T>
    requires concepts::WrapType<T>
struct TypeConverter<Local<T>> {
    static Local<Value> toJs(Local<T> const& value) { return value.asValue(); }
    static Local<T>     toCpp(Local<Value> const& value) { return value.as<T>(); }
};

// bool <-> Boolean
template <>
struct TypeConverter<bool> {
    static Local<Boolean> toJs(bool value) { return Boolean::newBoolean(value); }
    static bool           toCpp(Local<Value> const& value) { return value.asBoolean().getValue(); }
};

// int/uint/float/double/int64/uint64 <-> Number/BigInt
template <typename T>
    requires concepts::NumberLike<T>
struct TypeConverter<T> {
    static Local<Value> toJs(T value) {
        if constexpr (std::same_as<T, int64_t> || std::same_as<T, uint64_t>) {
            return BigInt::newBigInt(value); // C++ -> Js: 严格类型转换
        } else {
            return Number::newNumber(value);
        }
    }
    static T toCpp(Local<Value> const& value) {
        if (value.isNumber()) {
            return value.asNumber().getValueAs<T>(); // Js -> C++: 宽松转换
        }
        if (value.isBigInt()) {
            if constexpr (std::same_as<T, int64_t>) {
                return value.asBigInt().getInt64();
            } else {
                return value.asBigInt().getUint64();
            }
        }
        [[unlikely]] throw Exception{"Cannot convert value to NumberLike<T>", Exception::Type::TypeError};
    }
};

// std::string <-> String
template <typename T>
    requires concepts::StringLike<T>
struct TypeConverter<T> {
    static Local<String> toJs(T const& value) { return String::newString(std::string_view{value}); }
    static std::string   toCpp(Local<Value> const& value) { return value.asString().getValue(); } // always UTF-8
};

// enum -> Number (enum value)
template <typename T>
    requires std::is_enum_v<T>
struct TypeConverter<T> {
    static Local<Number> toJs(T value) { return Number::newNumber(static_cast<int>(value)); }
    static T             toCpp(Local<Value> const& value) { return static_cast<T>(value.asNumber().getInt32()); }
};

// std::optional <-> null/undefined
template <typename T>
struct TypeConverter<std::optional<T>> {
    static Local<Value> toJs(std::optional<T> const& value) {
        if (value) {
            return binding::toJs(value.value());
        }
        return Null::newNull(); // default to null
    }
    static std::optional<T> toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined()) {
            return std::nullopt;
        }
        return std::optional<T>{binding::toCpp<T>(value)};
    }
};

// std::vector <-> Array
template <typename T>
struct TypeConverter<std::vector<T>> {
    static Local<Value> toJs(std::vector<T> const& value) {
        auto array = Array::newArray(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            array.set(i, binding::toJs(value[i]));
        }
        return array;
    }
    static std::vector<T> toCpp(Local<Value> const& value) {
        auto array = value.asArray();

        std::vector<T> result;
        result.reserve(array.length());
        for (std::size_t i = 0; i < array.length(); ++i) {
            result.push_back(binding::toCpp<T>(array[i]));
        }
        return result;
    }
};

template <typename K, typename V>
    requires concepts::StringLike<K> // JavaScript only supports string keys
struct TypeConverter<std::unordered_map<K, V>> {
    static_assert(HasTypeConverter_v<V>, "Cannot convert std::unordered_map to Object; type V has no TypeConverter");

    static Local<Value> toJs(std::unordered_map<K, V> const& value) {
        auto object = Object::newObject();
        for (auto const& [key, val] : value) {
            object.set(String::newString(key), binding::toJs(val));
        }
        return object;
    }

    static std::unordered_map<K, V> toCpp(Local<Value> const& value) {
        auto object = value.asObject();
        auto keys   = object.getOwnPropertyNames();

        std::unordered_map<K, V> result;
        for (auto const& key : keys) {
            result[key.getValue()] = binding::toCpp<V>(object.get(key));
        }
        return result;
    }
};

// std::variant <-> Type
template <typename... Is>
struct TypeConverter<std::variant<Is...>> {
    static_assert(
        (HasTypeConverter_v<Is> && ...),
        "Cannot convert std::variant to Object; all types must have a TypeConverter"
    );
    using TypedVariant = std::variant<Is...>;

    static Local<Value> toJs(TypedVariant const& value) {
        if (value.valueless_by_exception()) {
            return Null::newNull();
        }
        return std::visit([&](auto const& v) -> Local<Value> { return binding::toJs(v); }, value);
    }

    static TypedVariant toCpp(Local<Value> const& value) { return tryToCpp(value); }

    template <size_t I = 0>
    static TypedVariant tryToCpp(Local<Value> const& value) {
        if constexpr (I >= sizeof...(Is)) {
            throw Exception{
                "Cannot convert Value to std::variant; no matching type found.",
                Exception::Type::TypeError
            };
        } else {
            using Type = std::variant_alternative_t<I, TypedVariant>;
            try {
                return binding::toCpp<Type>(value);
            } catch (Exception const&) {
                return tryToCpp<I + 1>(value);
            }
        }
    }
};

// std::monostate <-> null/undefined
template <>
struct TypeConverter<std::monostate> {
    static Local<Value> toJs(std::monostate) { return Null::newNull(); }

    static std::monostate toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined()) {
            return std::monostate{};
        }
        [[unlikely]] throw Exception{"Expected null/undefined for std::monostate", Exception::Type::TypeError};
    }
};

// std::pair <-> [T1, T2]
template <typename Ty1, typename Ty2>
struct TypeConverter<std::pair<Ty1, Ty2>> {
    static_assert(HasTypeConverter_v<Ty1>);
    static_assert(HasTypeConverter_v<Ty2>);

    static Local<Value> toJs(std::pair<Ty1, Ty2> const& pair) {
        auto array = Array::newArray(2);
        array.set(0, binding::toJs(pair.first));
        array.set(1, binding::toJs(pair.second));
        return array;
    }
    static std::pair<Ty1, Ty2> toCpp(Local<Value> const& value) {
        if (!value.isArray() || value.asArray().length() != 2) {
            throw Exception{"Invalid argument type, expected array with 2 elements"};
        }
        auto array = value.asArray();
        return std::make_pair(binding::toCpp<Ty1>(array.get(0)), binding::toCpp<Ty2>(array.get(1)));
    }
};

// TODO: impl
// std::function -> Function
// template <typename R, typename... Args>
// struct TypeConverter<std::function<R(Args...)>> {
//     static_assert(
//         (HasTypeConverter_v<Args> && ...),
//         "Cannot convert std::function to Function; all parameter types must have a TypeConverter"
//     );
//     static Local<Value>              toJs(std::function<R(Args...)> const& value) {}
//     static std::function<R(Args...)> toCpp(Local<Value> const& value) {}
// };


// free functions
template <typename T>
Local<Value> toJs(T&& val) {
    return RawTypeConverter<T>::toJs(std::forward<T>(val));
}

template <typename T>
Local<Value> toJs(T&& val, ReturnValuePolicy policy, Local<Value> parent) {
    if constexpr (requires { RawTypeConverter<T>::toJs(std::forward<T>(val), policy, parent); }) {
        return RawTypeConverter<T>::toJs(std::forward<T>(val), policy, parent);
    } else {
        return toJs(std::forward<T>(val)); // drop policy and parent
    }
}

template <typename T>
decltype(auto) toCpp(Local<Value> const& value) {
    using BareT = std::remove_cv_t<std::remove_reference_t<T>>; // T

    using Conv    = RawTypeConverter<T>;
    using ConvRet = decltype(Conv::toCpp(std::declval<Local<Value>>()));

    if constexpr (std::is_lvalue_reference_v<T>) {
        // 左值引用 T&
        if constexpr (std::is_pointer_v<std::remove_reference_t<ConvRet>>) {
            auto p = Conv::toCpp(value); // 返回 T*
            if (p == nullptr) [[unlikely]] {
                throw std::runtime_error("TypeConverter::toCpp returned a null pointer.");
            }
            return static_cast<T&>(*p); // 返回 T&
        } else if constexpr (std::is_lvalue_reference_v<ConvRet> || std::is_const_v<std::remove_reference_t<T>>) {
            return Conv::toCpp(value); // 已返回 T&，直接转发 或者 const T& 可以绑定临时
        } else {
            static_assert(
                std::is_pointer_v<std::remove_reference_t<ConvRet>> || std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return either T* or T& when toCpp<T&> is required. Returning T (by "
                "value) cannot bind to a non-const lvalue reference; change TypeConverter or request a value type."
            );
        }
    } else if constexpr (std::is_pointer_v<T>) {
        // 指针类型 T*
        if constexpr (std::is_pointer_v<std::remove_reference_t<ConvRet>>) {
            return Conv::toCpp(value); // 直接返回
        } else if constexpr (std::is_lvalue_reference_v<ConvRet>) {
            return std::addressof(Conv::toCpp(value)); // 返回 T& -> 可以取地址
        } else {
            static_assert(
                std::is_pointer_v<std::remove_reference_t<ConvRet>> || std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return T* or T& when toCpp<T*> is required. "
                "Returning T (by value) would produce pointer to temporary (unsafe)."
            );
        }
    } else {
        // 值类型 T
        using RawConvRet = std::remove_cv_t<std::remove_reference_t<ConvRet>>;
        if constexpr ((std::is_same_v<RawConvRet, BareT> || internal::CppValueTypeTransformer_v<RawConvRet, BareT>)
                      && !std::is_pointer_v<std::remove_reference_t<ConvRet>> && !std::is_lvalue_reference_v<ConvRet>) {
            return Conv::toCpp(value); // 按值返回 / 直接返回 (可能 NRVO)
        } else {
            static_assert(
                std::is_same_v<RawConvRet, BareT> && !std::is_pointer_v<std::remove_reference_t<ConvRet>>
                    && !std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return T (by value) for toCpp<T>. "
                "Other return forms (T* or T&) are not supported for value request."
            );
        }
    }
}


} // namespace v8kit::binding
