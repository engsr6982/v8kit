#pragma once
#include "NativeInstanceImpl.h"
#include "ReturnValuePolicy.h"
#include "TypeConverter.h" // NOLINT
#include "traits/Polymorphic.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"

#include <cassert>
#include <stdexcept>
#include <variant>


namespace v8kit::binding {

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


template <typename T>
struct GenericTypeConverter {

};


template <typename T>
    requires binding::IsBindingClass_v<T>
struct TypeConverter<T> {
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
        using RawType = std::decay_t<T>; // 去除引用和修饰符
        policy        = handleAutomaticPolicy<T>(policy);

        auto& engine = EngineScope::currentRuntimeChecked();

        // 应用 PolymorphicTypeHook
        const std::type_info* runtimeTypeInfo = nullptr;
        const void*           mostDerivedPtr  = nullptr;
        if constexpr (std::is_pointer_v<RawType>) {
            if (!value) return Null::newNull();
            // Hook: 传入指针，获取真实类型和最顶层地址
            mostDerivedPtr = traits::PolymorphicTypeHook<std::remove_pointer_t<RawType>>::get(value, runtimeTypeInfo);
        } else {
            // 引用取地址后传入 Hook
            mostDerivedPtr = traits::PolymorphicTypeHook<RawType>::get(&value, runtimeTypeInfo);
        }

        // 如果 Hook 没找到类型 (比如非多态)，回退到静态类型 T
        std::type_index typeIdx = runtimeTypeInfo ? std::type_index(*runtimeTypeInfo)
                                                  : std::type_index(typeid(std::remove_pointer_t<RawType>));

        auto* meta = engine.getClassDefine(typeIdx);
        if (!meta) {
            // 如果真实类型没注册 (比如 Dog 没注册，只注册了 Animal)，
            // 尝试回退到声明类型 (Animal) 的 Meta
            meta = engine.getClassDefine(std::type_index(typeid(std::remove_pointer_t<RawType>)));
            if (!meta) {
                throw Exception("Class not registered: " + std::string(typeIdx.name()));
            }
            // 此时 mostDerivedPtr 可能对于 Animal 来说是错误的 (如果 offsets 存在)，
            // 但如果回退到了 Animal，我们应该用原始指针 value，而不是 mostDerivedPtr。
            // 这是一个细微的 Edge Case，通常如果注册了多态基类，应该能处理。
            // 简单起见，如果回退，我们重置指针：
            if constexpr (std::is_pointer_v<RawType>) mostDerivedPtr = value;
            else mostDerivedPtr = &value;
        }

        // 根据 Policy 创建 NativeInstance
        std::unique_ptr<NativeInstance> instance;

        auto createImpl = [&](auto&& holder) {
            // NativeInstanceFactory 需要实现类似 pybind11 的逻辑，
            // 这里的 holder 类型决定了 Impl 的模板参数
            using HolderT = std::decay_t<decltype(holder)>;
            using ValT    = typename std::pointer_traits<
                   typename std::conditional<std::is_pointer_v<HolderT>, HolderT, typename HolderT::element_type*>::type>::
                element_type;

            return std::make_unique<NativeInstanceImpl<ValT, HolderT>>(meta, std::forward<decltype(holder)>(holder));
        };

        using ElementType = std::remove_pointer_t<RawType>;

        switch (policy) {
        case ReturnValuePolicy::kCopy: {
            // 必须拷贝：Holder 是 unique_ptr<T>
            // 注意：必须拷贝静态类型 T，不能拷贝多态指针 (除非有 clone 虚函数)
            // 如果是多态切片 (Slicing)，这里会发生切片拷贝。这是符合 C++ 语义的。
            if constexpr (std::is_copy_constructible_v<ElementType>) {
                if constexpr (std::is_pointer_v<RawType>) {
                    instance = createImpl(std::make_unique<ElementType>(*value));
                } else {
                    instance = createImpl(std::make_unique<ElementType>(value));
                }
            } else {
                throw Exception("Object is not copy constructible");
            }
            break;
        }
        case ReturnValuePolicy::kMove: {
            // 必须移动
            if constexpr (std::is_move_constructible_v<ElementType>) {
                // 注意：移动通常意味着源对象失效，这通常只对右值引用有效
                instance = createImpl(std::make_unique<ElementType>(std::move(value)));
            }
            break;
        }
        case ReturnValuePolicy::kTakeOwnership: {
            if constexpr (std::is_pointer_v<RawType>) {
                // 只有指针能接管所有权, 强转为 unique_ptr
                instance = createImpl(std::unique_ptr<ElementType>(value));
            } else {
                throw Exception("Cannot take ownership of non-pointer");
            }
            break;
        }
        case ReturnValuePolicy::kReference:
        case ReturnValuePolicy::kReferenceInternal: { // Internal 暂且当做 Reference，后续处理 keep alive
            // Holder 是 T* (裸指针)
            // 这里的 value 可能是 Derived*，但我们创建 Impl<Derived, Derived*>
            // 关键点：即使我们查到了 DerivedMeta，我们的 Impl 模板参数 T 应该是声明类型还是真实类型？
            // 答：为了安全，Impl 应该持有 声明类型 (Static Type) 的指针，但 meta 是 运行时类型。
            // 这样 NativeInstanceImpl::cast 会工作正常。

            if constexpr (std::is_pointer_v<RawType>) {
                instance = createImpl(value);
            } else {
                instance = createImpl(&value);
            }
            break;
        }
        default:
            break;
        }

        // 6. 包装为 JS 对象
        Local<Object> jsObj = engine.newInstance(*meta, std::move(instance));

        // 7. 处理 ReferenceInternal (Keep Alive)
        if (policy == ReturnValuePolicy::kReferenceInternal) {
            if (!parent.isObject()) {
                throw Exception("kReferenceInternal requires a valid parent object");
            }
            // 这是一个高级操作：让 jsObj 依赖 parent
            // V8 不直接提供 "obj keep alive parent"，通常是 "parent keep alive obj" (Set)
            // 但这里反过来了：返回的子对象需要保活父对象。
            // 实现方式：利用 InternalField 或者 WeakCallback 或者 HiddenValue。
            // 简单做法：给 jsObj 设置一个 Hidden Reference 指向 parent。
            // jsObj->SetPrivate(context, keepAliveSymbol, parent);
            // 或者使用 engine.addKeepAlive(jsObj, parent);
            // 这一步需要 Engine 支持。
            // TODO: impl
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