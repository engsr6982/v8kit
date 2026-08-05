#pragma once
#include "NativeInstanceImpl.h"
#include "ReturnValuePolicy.h"
#include "jspp/core/Exception.h"
#include "jspp/core/InstancePayload.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/TrackedHandle.h"
#include "jspp/core/Value.h"
#include "traits/FunctionTraits.h"
#include "traits/Polymorphic.h"
#include "traits/TypeTraits.h"


#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace jspp::binding {

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


template <typename T>
struct GenericTypeConverter {
    template <typename U>
    static ReturnValuePolicy handleAutomaticPolicy(ReturnValuePolicy policy) {
        return detail::resolveAutomaticPolicy<U>(policy);
    }

    // C++ -> JS
    // U is introduced to enable perfect forwarding.
    // T cannot be used because it is already determined by the enclosing template,
    // so T&& would be a pure rvalue reference rather than a forwarding reference.
    template <typename U>
    static Local<Value> toJs(U&& value, ReturnValuePolicy policy, Local<Value> const& parent) {
        policy = handleAutomaticPolicy<U>(policy);

        using ElementType   = typename traits::detail::ElementTypeExtractor<U>::type;
        ElementType* rawPtr = nullptr;

        // 裸指针/智能指针判定时，需要剥离引用的原始类型(保留 const 语义)
        using BaseU = std::remove_reference_t<U>;

        if constexpr (std::is_pointer_v<BaseU>) {
            rawPtr = value;
            if (!rawPtr) return Null::newNull();
        } else if constexpr (traits::is_unique_ptr_v<BaseU> || traits::is_shared_ptr_v<BaseU>) {
            rawPtr = value.get();
            if (!rawPtr) return Null::newNull();
        } else {
            rawPtr = &value;
        }

        // 查表：解析对象的最终多态 Meta 和首地址偏移
        auto resolved = traits::detail::resolveCastSource<ElementType>(rawPtr);

        // 创建包装着 C++ 实例的底座 (NativeInstance)
        auto instance = factory::createNativeInstance(std::forward<U>(value), policy, resolved);
        if (!instance) return Null::newNull();

        auto&         engine = EngineScope::currentEngineChecked();
        Local<Object> jsObj  = engine.newInstance(*resolved.meta, std::move(instance));

        if (policy == ReturnValuePolicy::kReferenceInternal
            || policy == ReturnValuePolicy::kReferenceInternalPersistent) {
            if (!parent.isObject()) {
                throw Exception("kReferenceInternal/kReferenceInternalPersistent requires a valid parent object");
            }
            if (!engine.trySetReferenceInternal(parent.asObject(), jsObj)) {
                throw Exception("Failed to set reference internal");
            }
        }

        // 瞬态作用域（溯源式跟踪）：
        // 仅跟踪"瞬态根"（parent 为空，如回调参数）或从瞬态根派生的包装
        // （parent 的 NativeInstance 已在当前作用域的跟踪集合中）。
        // 在回调内访问长期对象的成员不会被误伤。被跟踪的 wrapper 会被保活
        // 至作用域结束，避免 QuickJS 引用计数归零提前回收导致悬垂指针。
        if (policy == ReturnValuePolicy::kReference || policy == ReturnValuePolicy::kReferenceInternal) {
            if (TransientObjectScope::isActive()) {
                auto& scope                = TransientObjectScope::currentChecked();
                bool  derivedFromTransient = false;
                if (parent.isObject()) {
                    if (auto* parentPayload = engine.getInstancePayload(parent.asObject())) {
                        derivedFromTransient = scope.contains(&parentPayload->getHolder());
                    }
                }
                if (!parent.isObject() || derivedFromTransient) {
                    scope.track(jsObj.asValue());
                }
            }
        }
        return jsObj;
    }

    // JS -> C++
    template <typename U = T>
    static U* toCpp(Local<Value> const& value) {
        auto& engine  = EngineScope::currentEngineChecked();
        auto  payload = engine.getInstancePayload(value.asObject());
        if (!payload) {
            throw Exception("Argument is not a native instance", ExceptionType::TypeError);
        }
        return payload->unwrap<U>();
    }
};

} // namespace detail

template <typename T>
struct TypeConverter : detail::GenericTypeConverter<T> {};

// jspp::Local<T>
template <typename T>
    requires concepts::WrapType<T>
struct TypeConverter<Local<T>> {
    static Local<Value> toJs(Local<T> const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return value.asValue();
    }
    static Local<T> toCpp(Local<Value> const& value) { return value.as<T>(); }
};

// bool <-> Boolean
template <>
struct TypeConverter<bool> {
    static Local<Boolean> toJs(bool value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return Boolean::newBoolean(value);
    }
    static bool toCpp(Local<Value> const& value) { return value.asBoolean().getValue(); }
};

// int/uint/float/double/int64/uint64 <-> Number/BigInt
template <typename T>
    requires concepts::NumberLike<T>
struct TypeConverter<T> {
    static Local<Value> toJs(T value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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
    static Local<String> toJs(T const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return String::newString(std::string_view{value});
    }
    static std::string toCpp(Local<Value> const& value) { return value.asString().getValue(); } // always UTF-8
};

// enum -> Number (enum value)
template <typename T>
    requires std::is_enum_v<T>
struct TypeConverter<T> {
    static Local<Number> toJs(T value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return Number::newNumber(static_cast<int>(value));
    }
    static T toCpp(Local<Value> const& value) { return static_cast<T>(value.asNumber().getInt32()); }
};

// std::optional <-> null/undefined
template <typename T>
struct TypeConverter<std::optional<T>> {
    static Local<Value>
    toJs(std::optional<T> const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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
    static Local<Value>
    toJs(std::vector<T> const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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

    static Local<Value>
    toJs(std::unordered_map<K, V> const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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

    static Local<Value>
    toJs(TypedVariant const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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
    static Local<Value> toJs(std::monostate, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return Null::newNull();
    }

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

    static Local<Value>
    toJs(std::pair<Ty1, Ty2> const& pair, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
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


// fwd decl
namespace adapter {
template <typename R, typename... Args>
std::function<R(Args...)> wrapScriptCallback(Local<Value> const& value);
template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy);
} // namespace adapter

// std::function -> Function
template <typename R, typename... Args>
struct TypeConverter<std::function<R(Args...)>> {
    static_assert(
        (HasTypeConverter_v<Args> && ...),
        "Cannot convert std::function to Function; all parameter types must have a TypeConverter"
    );
    using Fn = std::function<R(Args...)>;

    static Local<Value> toJs(Fn const& value, ReturnValuePolicy policy, Local<Value> const& /*parent*/) {
        return adapter::wrapFunction(std::forward<Fn>(value), policy);
    }
    static Fn toCpp(Local<Value> const& value) { return adapter::wrapScriptCallback<R, Args...>(value); }
};


template <typename T>
struct TypeConverter<std::shared_ptr<T>> {
    using Ptr = std::shared_ptr<T>;

    template <typename U>
    static Local<Value> toJs(U&& value, ReturnValuePolicy policy, Local<Value> parent) {
        return TypeConverter<T>::template toJs<U>(std::forward<U>(value), policy, parent);
    }

    static Ptr toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined() || !value.isObject()) {
            return nullptr;
        }

        auto& engine  = EngineScope::currentEngineChecked();
        auto  payload = engine.getInstancePayload(value.asObject());
        if (payload == nullptr) [[unlikely]] {
            throw Exception("Argument is not a native instance");
        }

        auto& holder = payload->getHolder();
        if (holder.is_const() && !std::is_const_v<T>) [[unlikely]] {
            throw Exception(
                "Cannot transfer ownership: The JS object is const, but C++ requires a mutable std::shared_ptr.",
                Exception::Type::TypeError
            );
        }

        auto shared_void = holder.get_shared_ptr();
        if (!shared_void) {
            throw Exception("Underlying C++ object is not managed by std::shared_ptr");
        }

        // 计算裸指针地址(多继承指针偏移)
        void* casted_ptr = holder.cast(typeid(T));
        if (!casted_ptr) {
            throw Exception("Type mismatch or polymorphic cast failed");
        }

        return Ptr(shared_void, static_cast<T*>(casted_ptr));
    }
};

template <typename T>
struct TypeConverter<std::weak_ptr<T>> {

    template <typename U>
    static Local<Value> toJs(U&& value, ReturnValuePolicy policy, Local<Value> parent) {
        auto shared = value.lock();
        if (!shared) {
            return Null::newNull(); // 对象已死，返回 null
        }
        // 委托给 shared_ptr 转换器，让 NativeInstance 持有一个强引用
        return TypeConverter<std::shared_ptr<T>>::toJs(shared, policy, parent);
    }

    using Ptr = std::weak_ptr<T>;
    static Ptr toCpp(Local<Value> const& value) {
        // 提取出来的临时 shared_ptr 会隐式转换为 weak_ptr 并返回。
        // 因为 Class 里的 NativeInstance 还在，所以底层的引用计数至少为 1，绝不会立刻失效。
        return TypeConverter<std::shared_ptr<T>>::toCpp(value);
    }
};

template <typename T, typename Deleter>
struct TypeConverter<std::unique_ptr<T, Deleter>> {
    using Ptr = std::unique_ptr<T>;

    static_assert(
        std::is_same_v<Deleter, std::default_delete<T>>,
        "[jspp] FATAL: Only std::unique_ptr with std::default_delete is supported!"
    );

    template <typename U>
    static Local<Value> toJs(U&& value, ReturnValuePolicy policy, Local<Value> parent) {
        return TypeConverter<T>::template toJs<U>(std::forward<U>(value), policy, parent);
    }

    static Ptr toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined() || !value.isObject()) {
            return nullptr;
        }

        auto& engine  = EngineScope::currentEngineChecked();
        auto  payload = engine.getInstancePayload(value.asObject());
        if (payload == nullptr) {
            throw Exception("Argument is not a native instance");
        }

        auto& holder = payload->getHolder();
        if (holder.is_const() && !std::is_const_v<T>) [[unlikely]] {
            throw Exception(
                "Cannot transfer ownership: The JS object is const, but C++ requires a mutable std::unique_ptr.",
                Exception::Type::TypeError
            );
        }

        // 解析多态指针地址
        void* casted_ptr = holder.cast(typeid(T));
        if (!casted_ptr) {
            throw Exception("Type mismatch or polymorphic cast failed");
        }

        void* released_raw = holder.release_ownership();
        if (!released_raw) {
            throw Exception(
                "Cannot transfer ownership to std::unique_ptr: "
                "The JS object is not uniquely owned (it might be managed by shared_ptr or raw pointer)."
            );
        }

        T* raw_ptr = static_cast<T*>(casted_ptr);
        return Ptr(raw_ptr);
    }
};

template <typename T>
struct TypeConverter<std::reference_wrapper<T>> {
    static Local<Value> toJs(std::reference_wrapper<T> value, ReturnValuePolicy policy, Local<Value> parent) {
        if (policy == ReturnValuePolicy::kAutomatic || policy == ReturnValuePolicy::kTakeOwnership) {
            policy = ReturnValuePolicy::kReference;
        }
        return TypeConverter<T>::toJs(value.get(), policy, parent);
    }

    static std::reference_wrapper<T> toCpp(Local<Value> const& value) { return std::ref(binding::toCpp<T&>(value)); }
};

template <>
struct TypeConverter<std::filesystem::path> {
    static Local<Value>
    toJs(std::filesystem::path const& value, ReturnValuePolicy /* policy */, Local<Value> const& /* parent */) {
        return String::newString(value.string());
    }
    static std::filesystem::path toCpp(Local<Value> const& value) { return {value.asString().getValue()}; }
};


// free functions
template <typename T>
Local<Value> toJs(T&& val) {
    return toJs(std::forward<T>(val), ReturnValuePolicy::kAutomatic, {});
}

template <typename T>
Local<Value> toJs(T&& val, ReturnValuePolicy policy, Local<Value> parent) {
    return RawTypeConverter<T>::toJs(std::forward<T>(val), policy, parent);
}

template <typename T>
decltype(auto) toCpp(Local<Value> const& value) {
    using BareT = std::remove_cv_t<std::remove_reference_t<T>>;

    if constexpr (traits::is_unique_ptr_v<BareT>) {
        static_assert(std::is_same_v<T, BareT>, "Unique pointers must be passed by value to toCpp<T*>.");
    }

    using Conv     = RawTypeConverter<T>;
    using RequestT = std::remove_pointer_t<std::remove_reference_t<T>>;

    // ------------------------------------------------------------------
    // TypeConverter that supports perfect forwarding (GenericTypeConverter)
    // ------------------------------------------------------------------
    if constexpr (requires { Conv::template toCpp<RequestT>(value); }) {
        using UnwrappedT = std::conditional_t<
            !std::is_pointer_v<T> && !std::is_reference_v<T>, // if request copy use const RequestT, else use RequestT
            const RequestT,
            RequestT>;
        auto p = Conv::template toCpp<UnwrappedT>(value);
        if (!p) [[unlikely]] {
            throw std::runtime_error("TypeConverter::toCpp returned a null pointer.");
        }
        if constexpr (std::is_pointer_v<T>) {
            return p;
        } else {
            return static_cast<T>(*p);
        }
    }
    // ------------------------------------------------------------------
    // Fixed-length return type ordinary converter (basic types, STL, Enum, etc.)
    // ------------------------------------------------------------------
    else {
        using ConvRet    = decltype(Conv::toCpp(std::declval<Local<Value>>()));
        using RawConvRet = std::remove_cv_t<std::remove_reference_t<ConvRet>>;

        constexpr bool is_conv_ptr  = std::is_pointer_v<std::remove_reference_t<ConvRet>>;
        constexpr bool is_conv_lref = std::is_lvalue_reference_v<ConvRet>;

        // Request a lvalue reference (T& or const T&)
        if constexpr (std::is_lvalue_reference_v<T>) {
            if constexpr (is_conv_ptr) {
                auto p = Conv::toCpp(value);
                if (!p) [[unlikely]] {
                    throw std::runtime_error("TypeConverter::toCpp returned a null pointer.");
                }
                return static_cast<T>(*p);
            } else if constexpr (is_conv_lref || std::is_const_v<std::remove_reference_t<T>>) {
                // Return directly, letting decltype(auto) decide whether to return
                //  by reference or by value (to prevent dangling of temporary variables)
                return Conv::toCpp(value);
            } else {
                static_assert(
                    is_conv_ptr || is_conv_lref || std::is_const_v<std::remove_reference_t<T>>,
                    "TypeConverter must return T* or T& when toCpp<T&> is requested (unless target is const T&)."
                );
            }
        }
        // Request pointer (T*)
        else if constexpr (std::is_pointer_v<T>) {
            if constexpr (is_conv_ptr) {
                return Conv::toCpp(value);
            } else if constexpr (is_conv_lref) {
                return std::addressof(Conv::toCpp(value));
            } else {
                static_assert(
                    is_conv_ptr || is_conv_lref,
                    "TypeConverter must return T* or T& when toCpp<T*> is requested."
                );
            }
        }
        // Pass by value (T)
        else {
            static_assert(
                !std::is_polymorphic_v<BareT>,
                "toCpp<T> with polymorphic T by value is forbidden (slicing UB). Use T&, const T&, or T* instead."
            );
            if constexpr (!is_conv_ptr && !is_conv_lref) {
                if constexpr (
                    std::is_same_v<RawConvRet, BareT> || internal::CppValueTypeTransformer_v<RawConvRet, BareT>
                ) {
                    return Conv::toCpp(value);
                } else {
                    static_assert(
                        std::is_same_v<RawConvRet, BareT> || internal::CppValueTypeTransformer_v<RawConvRet, BareT>,
                        "TypeConverter return type mismatch with requested value type."
                    );
                }
            } else {
                static_assert(!is_conv_ptr && !is_conv_lref, "TypeConverter must return by value for a value request.");
            }
        }
    }
}

namespace adapter {

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
// Adapter impl
// ---------------------

// JavaScript lambda -> std::function
template <typename R, typename... Args>
std::function<R(Args...)> wrapScriptCallback(Local<Value> const& value) {
    if (!value.isFunction()) [[unlikely]] {
        throw Exception("expected function", Exception::Type::TypeError);
    }
    auto& engine = EngineScope::currentEngineChecked();

    // 使用跟踪句柄，避免 C++ 侧拷贝 Lambda、长期持有 Global 导致引擎析构资源泄漏
    auto safeKeep = TrackedGlobal<Function>::create(value.asFunction());

    return [keep = std::move(safeKeep), engine = &engine](Args&&... args) -> R {
        EngineScope lock{engine};

        auto& global = keep->global();
        if (global.isEmpty()) {
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                // 当跟踪句柄失效，代表引擎可能已销毁，这里抛运行时异常
                throw std::runtime_error{"Engine already destroyed"};
            }
        }

        TransientObjectScope enter{}; // 激活瞬时作用域，避免 JS 闭包逃逸 导致UAF

        std::array<Local<Value>, sizeof...(Args)> argv{
            toJs(std::forward<Args>(args), ReturnValuePolicy::kReference, Local<Value>{})...
        };
        if constexpr (std::is_void_v<R>) {
            global.get().call({}, argv);
            return;
        } else {
            return toCpp<R>(global.get().call({}, argv));
        }
    };
}

// C++ function -> JavaScript function
template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isFunctionCallback_v<Fn>) {
        return std::forward<Fn>(fn);
    } else {
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
                return toJs(std::forward<decltype(ret)>(ret), policy, args.hasThiz() ? args.thiz() : Local<Value>{});
            }
        };
    }
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
            ...);
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
            ...);
        return arr;
    }();

    return _mergeFunctionCallbacks(std::move(overloads));
}


// C++ Getter / Setter -> JavaScript Getter / Setter
template <typename Fn>
GetterCallback wrapGetter(Fn&& getter, ReturnValuePolicy policy) {
    if constexpr (traits::isGetterCallback_v<Fn>) {
        return std::forward<Fn>(getter);
    } else {
        return [get = std::forward<Fn>(getter), policy]() -> Local<Value> {
            using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
            using R     = Trait::ReturnType;
            static_assert(!std::is_void_v<R>, "Getter must return a value");
            static_assert(Trait::ArgsCount == 0, "Getter must not take arguments");

            decltype(auto) value = std::invoke(get);
            return toJs(std::forward<decltype(value)>(value), policy, {});
        };
    }
}
template <typename Fn>
SetterCallback wrapSetter(Fn&& setter) {
    if constexpr (traits::isSetterCallback_v<Fn>) {
        return std::forward<Fn>(setter);
    } else {
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
}

template <typename Ty, bool forceReadonly = false>
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
ConstructorCallback wrapConstructor() {
    return [](Arguments const& args) -> std::unique_ptr<NativeInstance> {
        constexpr size_t N = sizeof...(Args);
        if constexpr (N == 0) {
            static_assert(
                concepts::HasDefaultConstructor<C>,
                "Class C must have a no-argument constructor; otherwise, a constructor must be specified."
            );
            if (args.length() != 0) return nullptr; // Parameter mismatch
            return factory::newNativeInstance<C>();

        } else {
            if (args.length() != N) return nullptr; // Parameter mismatch

            using Tuple = std::tuple<Args...>;

            auto parameters = ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<N>());
            return std::apply(
                [](auto&&... unpackedArgs) {
                    return factory::newNativeInstance<C>(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                },
                std::move(parameters)
            );
        }
    };
}

template <typename C, typename Fn>
InstanceMethodCallback wrapInstanceMethod(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isInstanceMethodCallback_v<Fn>) {
        return std::forward<Fn>(fn); // 已是标准的回调，直接转发不需要进行绑定
    } else {
        return [f = std::forward<Fn>(fn), policy](InstancePayload& payload, const Arguments& args) -> Local<Value> {
            using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
            using R     = typename Trait::ReturnType;
            using Tuple = typename Trait::ArgsTuple;

            if constexpr (std::is_member_function_pointer_v<Fn>) {
                // ---------------- 成员函数指针 ----------------
                constexpr size_t ArgsCount = Trait::ArgsCount;
                if (args.length() != ArgsCount) [[unlikely]] {
                    throw Exception("argument count mismatch", Exception::Type::TypeError);
                }

                using UnwrapC = std::conditional_t<Trait::isConst, const C, C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                if constexpr (std::is_void_v<R>) {
                    std::apply(
                        [inst, &f](auto&&... unpackedArgs) {
                            (inst->*f)(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                        },
                        ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<ArgsCount>())
                    );
                    return {}; // undefined
                } else {
                    decltype(auto) ret = std::apply(
                        [inst, &f](auto&&... unpackedArgs) -> R {
                            return (inst->*f)(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                        },
                        ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<ArgsCount>())
                    );
                    // 特殊情况，对于 Builder 模式，返回 this
                    if constexpr (std::is_same_v<R, C&>) {
                        assert(args.hasThiz() && "this is required for Builder pattern");
                        return args.thiz();
                    } else {
                        return toJs(
                            std::forward<decltype(ret)>(ret),
                            policy,
                            args.hasThiz() ? args.thiz() : Local<Value>{}
                        );
                    }
                }
            } else {
                // ---------------- 自由函数 / lambda ----------------
                // 签名约定：第一参数为实例 (C& / const C& / C* / const C*)，其余为方法参数
                static_assert(
                    Trait::ArgsCount >= 1,
                    "Non-member method must take the bound instance as its first argument"
                );
                using Arg0 = std::tuple_element_t<0, Tuple>;
                static_assert(
                    std::is_same_v<traits::RawType_t<Arg0>, C>,
                    "First argument of non-member method must match the bound class. "
                    "Expected instance of C&, C*, const C&, or const C*."
                );
                static_assert(
                    !(std::is_polymorphic_v<C> && !std::is_lvalue_reference_v<Arg0> && !std::is_pointer_v<Arg0>),
                    "Pass-by-value of a polymorphic type is not allowed (slicing risk). Use C&, const C&, or C* "
                    "instead."
                );

                constexpr size_t ArgCount = Trait::ArgsCount - 1; // JS 侧参数个数（不含 self）
                if (args.length() != ArgCount) [[unlikely]] {
                    throw Exception("argument count mismatch", Exception::Type::TypeError);
                }

                // 从第一参数的类型推导宿主可变性（与 wrapInstanceGetter 的 non-member 分支一致）
                using UnwrapC = std::
                    conditional_t<std::is_const_v<std::remove_pointer_t<std::remove_reference_t<Arg0>>>, const C, C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                // 偏移参数转换：args[i] -> tuple_element_t<i + 1, Tuple>（跳过 self）
                auto convertRest = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    using SafeTuple = std::tuple<StorageType_t<std::tuple_element_t<Is + 1, Tuple>>...>;
                    return SafeTuple{toCpp<std::tuple_element_t<Is + 1, Tuple>>(args[Is])...};
                };
                auto rest = convertRest(std::make_index_sequence<ArgCount>());

                auto invoke = [&](auto&&... restArgs) -> R {
                    if constexpr (std::is_pointer_v<Arg0>) {
                        return f(inst, std::forward<decltype(restArgs)>(restArgs)...);
                    } else {
                        if (inst == nullptr) [[unlikely]] {
                            throw std::runtime_error(
                                "Cannot invoke method: instance is null when passing by reference"
                            );
                        }
                        return f(*inst, std::forward<decltype(restArgs)>(restArgs)...);
                    }
                };

                if constexpr (std::is_void_v<R>) {
                    std::apply(invoke, std::move(rest));
                    return {}; // undefined
                } else {
                    decltype(auto) ret = std::apply(invoke, std::move(rest));
                    // Builder 模式：lambda 返回 C& 时返回 thiz
                    if constexpr (std::is_same_v<R, C&>) {
                        assert(args.hasThiz() && "this is required for Builder pattern");
                        return args.thiz();
                    } else {
                        return toJs(
                            std::forward<decltype(ret)>(ret),
                            policy,
                            args.hasThiz() ? args.thiz() : Local<Value>{}
                        );
                    }
                }
            }
        };
    }
}

template <size_t Len>
inline InstanceMethodCallback _mergeMethodCallbacks(std::array<InstanceMethodCallback, Len> overloads) {
    return [fs = std::move(overloads)](InstancePayload& payload, Arguments const& args) -> Local<Value> {
        return dispatchOverloadImpl<Local<Value>>(fs, payload, args);
    };
}

template <typename C, typename... Overload>
InstanceMethodCallback wrapOverloadMethodAndExtraPolicy(Overload&&... fn) {
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
            ...);
    }

    constexpr size_t func_count = sizeof...(Overload) - policy_count;
    static_assert(func_count > 0, "No functions provided to overload");

    auto overloads = [&]() {
        std::array<InstanceMethodCallback, func_count> arr;
        size_t                                         idx = 0;
        (
            [&](auto&& arg) {
                if constexpr (!traits::is_policy<decltype(arg)>::value) {
                    arr[idx++] = wrapInstanceMethod<C>(std::forward<decltype(arg)>(arg), policy);
                }
            }(std::forward<Overload>(fn)),
            ...);
        return arr;
    }();
    return _mergeMethodCallbacks(std::move(overloads));
}


template <typename C, typename Fn>
InstanceGetterCallback wrapInstanceGetter(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isInstanceGetterCallback_v<Fn>) {
        return std::forward<Fn>(fn);
    } else {
        return [f = std::forward<Fn>(fn), policy](InstancePayload& payload, const Arguments& args) -> Local<Value> {
            using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
            using R     = Trait::ReturnType;
            static_assert(!std::is_void_v<R>, "Getter must return a value");

            if constexpr (std::is_member_function_pointer_v<Fn>) {
                static_assert(Trait::ArgsCount == 0, "Getter must not take arguments");
                // Member function: use Trait::isConst to determine instance mutability
                using UnwrapC = std::conditional_t<Trait::isConst, const C, C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                decltype(auto) value = (inst->*f)();
                return toJs(
                    std::forward<decltype(value)>(value),
                    policy,
                    args.hasThiz() ? args.thiz() : Local<Value>{}
                );
            } else {
                // non member function pointer getter, e.g. lambda or function
                static_assert(
                    Trait::ArgsCount == 1,
                    "Non-member function pointer getter must take exactly one argument"
                );

                using Args       = Trait::ArgsTuple;
                using arg_0_type = std::tuple_element_t<0, Args>;
                static_assert(
                    std::is_same_v<traits::RawType_t<arg_0_type>, C>,
                    "First argument of non-member getter must match the bound class. "
                    "Expected instance of C&, C*, const C&, or const C*."
                );
                static_assert(
                    !(std::is_polymorphic_v<C> && !std::is_lvalue_reference_v<arg_0_type>
                      && !std::is_pointer_v<arg_0_type>),
                    "Pass-by-value of a polymorphic type is not allowed (slicing risk). Use C&, const C&, or C* "
                    "instead."
                );

                // Non-member function (Lambda): deduce mutability from the first argument's type
                using UnwrapC = std::conditional_t<
                    std::is_const_v<std::remove_pointer_t<std::remove_reference_t<arg_0_type>>>,
                    const C,
                    C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                if constexpr (std::is_pointer_v<arg_0_type>) {
                    decltype(auto) value = f(inst);
                    return toJs(
                        std::forward<decltype(value)>(value),
                        policy,
                        args.hasThiz() ? args.thiz() : Local<Value>{}
                    );
                } else {
                    static_assert(
                        std::is_lvalue_reference_v<arg_0_type>,
                        "Non-member getter first argument must be C& or C*. Pass-by-value (C) is not supported."
                    );
                    if (inst == nullptr) [[unlikely]] {
                        throw std::runtime_error("Cannot invoke getter: instance is null when passing by reference");
                    }
                    decltype(auto) value = f(*inst);
                    return toJs(
                        std::forward<decltype(value)>(value),
                        policy,
                        args.hasThiz() ? args.thiz() : Local<Value>{}
                    );
                }
            }
        };
    }
}
template <typename C, typename Fn>
InstanceSetterCallback wrapInstanceSetter(Fn&& fn) {
    if constexpr (traits::isInstanceSetterCallback_v<Fn>) {
        return std::forward<Fn>(fn);
    } else {
        return [f = std::forward<Fn>(fn)](InstancePayload& payload, const Arguments& args) {
            using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
            using R     = Trait::ReturnType;
            static_assert(std::is_void_v<R>, "Setter must not return a value");

            if constexpr (std::is_member_function_pointer_v<Fn>) {
                // Member function: use Trait::isConst to determine instance mutability
                static_assert(Trait::ArgsCount == 1, "Setter must take one argument");
                using UnwrapC = std::conditional_t<Trait::isConst, const C, C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                using Args = Trait::ArgsTuple;
                using Type = std::tuple_element_t<0, Args>;
                (inst->*f)(toCpp<Type>(args[0]));
            } else {
                // Non-member functions (such as Lambda) require
                // the first parameter to be a reference/pointer to the bound instance
                static_assert(Trait::ArgsCount == 2, "Non-member setter must take (C&, T) or (C*, T)");

                using Args       = Trait::ArgsTuple;
                using arg_0_type = std::tuple_element_t<0, Args>;
                using Type       = std::tuple_element_t<1, Args>; // JS 传入的参数

                static_assert(
                    std::is_same_v<traits::RawType_t<arg_0_type>, C>,
                    "First argument of non-member setter must match the bound class."
                );

                // deduce mutability from the first argument's type
                using UnwrapC = std::conditional_t<
                    std::is_const_v<std::remove_pointer_t<std::remove_reference_t<arg_0_type>>>,
                    const C,
                    C>;
                UnwrapC* inst = payload.unwrap<UnwrapC>();

                if constexpr (std::is_pointer_v<arg_0_type>) {
                    f(inst, toCpp<Type>(args[0]));
                } else {
                    static_assert(
                        std::is_lvalue_reference_v<arg_0_type>,
                        "Non-member setter first arg must be C& or C*"
                    );
                    if (inst == nullptr) [[unlikely]] {
                        throw std::runtime_error("Cannot invoke setter: instance is null");
                    }
                    f(*inst, toCpp<Type>(args[0]));
                }
            }
        };
    }
}

template <typename T>
struct MemberPointerTraits;
template <typename ClassType, typename ValueType>
struct MemberPointerTraits<ValueType ClassType::*> {
    using class_type              = ClassType;
    using value_type              = ValueType;
    static constexpr bool isConst = false;
};
template <typename ClassType, typename ValueType>
struct MemberPointerTraits<const ValueType ClassType::*> {
    using class_type              = ClassType;
    using value_type              = ValueType;
    static constexpr bool isConst = true;
};


template <typename C, bool forceReadonly, typename MemberPtr>
std::pair<InstanceGetterCallback, InstanceSetterCallback>
wrapInstanceAccessor(MemberPtr member, ReturnValuePolicy policy) {
    using Traits     = MemberPointerTraits<std::remove_cvref_t<MemberPtr>>;
    using value_type = typename Traits::value_type;
    using class_type = typename Traits::class_type;

    static_assert(
        std::is_base_of_v<class_type, C> || std::is_same_v<class_type, C>,
        "Member pointer does not belong to the bound class hierarchy"
    );

    // 实例成员指针 prop 默认采用引用语义：kAutomatic 会把左值引用解析为 kCopy，
    // 导致类类型成员每次访问都产生拷贝、修改无法写回宿主；而成员引用天然依赖
    // 宿主存活，kReferenceInternal（引用 + 保活宿主）是符合直觉的默认。
    // 值类型成员（int/string/enum 等）的转换器忽略策略，不受影响。
    if (policy == ReturnValuePolicy::kAutomatic) {
        policy = ReturnValuePolicy::kReferenceInternal;
    }

    InstanceGetterCallback getter = [member,
                                     policy](InstancePayload& payload, Arguments const& arguments) -> Local<Value> {
        // 按宿主可变性解包：宿主可变且非只读 prop 时返回成员的可变引用（写回生效）；
        // 宿主为 const 或 prop_readonly 时返回 const 引用（只读）。
        // 成员自身声明为 const（const T C::*）时，const 语义由类型系统自动传播。
        if (forceReadonly || payload.getHolder().is_const()) {
            auto           constInst = payload.unwrap<const C>();
            decltype(auto) value     = constInst->*member;
            return toJs(
                std::forward<decltype(value)>(value),
                policy,
                arguments.hasThiz() ? arguments.thiz() : Local<Value>{}
            );
        } else {
            auto           inst  = payload.unwrap<C>();
            decltype(auto) value = inst->*member;
            return toJs(
                std::forward<decltype(value)>(value),
                policy,
                arguments.hasThiz() ? arguments.thiz() : Local<Value>{}
            );
        }
    };
    InstanceSetterCallback setter = nullptr;
    if constexpr (!Traits::isConst && !forceReadonly) {
        setter = [member](InstancePayload& payload, Arguments const& arguments) {
            auto inst     = payload.unwrap<C>();
            inst->*member = toCpp<value_type>(arguments[0]);
        };
    }
    return {std::move(getter), std::move(setter)};
}


} // namespace adapter


} // namespace jspp::binding
