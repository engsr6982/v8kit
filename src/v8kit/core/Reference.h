#pragma once
#include "Concepts.h"    // NOLINT
#include "V8TypeAlias.h" // NOLINT
#include "v8kit/Macro.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

V8KIT_WARNING_GUARD_BEGIN
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-primitive.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {

#define SPECIALIZATION_LOCAL(VALUE)                                                                                    \
public:                                                                                                                \
    V8KIT_DISABLE_NEW();                                                                                               \
    ~Local();                                                                                                          \
    Local(Local<VALUE> const&);                                                                                        \
    Local(Local<VALUE>&&) noexcept;                                                                                    \
    Local<VALUE>& operator=(Local const&);                                                                             \
    Local<VALUE>& operator=(Local&&) noexcept;                                                                         \
                                                                                                                       \
    Local<String> toString();                                                                                          \
    bool          operator==(Local<Value> const& other) const;                                                         \
                                                                                                                       \
private:                                                                                                               \
    friend struct ValueHelper;                                                                                         \
    friend class Exception;                                                                                            \
    friend class VALUE;                                                                                                \
    template <typename>                                                                                                \
    friend class Local;                                                                                                \
    template <typename>                                                                                                \
    friend class Global;                                                                                               \
    template <typename>                                                                                                \
    friend class Weak;

#define SPECALIZATION_AS_VALUE(VALUE)                                                                                  \
public:                                                                                                                \
    Local<Value> asValue() const;                                                                                      \
    operator Local<Value>() const { return asValue(); }                                                                \
    bool operator==(Local<VALUE> const& other) const { return operator==(other.asValue()); }

#define SPECALIZATION_V8_LOCAL_TYPE(VALUE)                                                                             \
private:                                                                                                               \
    using Type = internal::V8Type_v<VALUE>;                                                                            \
    explicit Local(v8::Local<Type>);                                                                                   \
    v8::Local<Type> val


template <typename T>
class Local final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");
};

template <>
class Local<Value> {
    SPECIALIZATION_LOCAL(Value);
    SPECALIZATION_V8_LOCAL_TYPE(Value);

    friend class Arguments;

public:
    // default v8::Undefined -> undefined
    Local() noexcept;

    [[nodiscard]] ValueKind kind() const;

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isUndefined() const;
    [[nodiscard]] bool isNullOrUndefined() const; // null or undefined
    [[nodiscard]] bool isBoolean() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isBigInt() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isSymbol() const;
    [[nodiscard]] bool isObject() const;
    [[nodiscard]] bool isArray() const;
    [[nodiscard]] bool isFunction() const;

    [[nodiscard]] Local<Value>     asValue() const;
    [[nodiscard]] Local<Null>      asNull() const;
    [[nodiscard]] Local<Undefined> asUndefined() const;
    [[nodiscard]] Local<Boolean>   asBoolean() const;
    [[nodiscard]] Local<Number>    asNumber() const;
    [[nodiscard]] Local<BigInt>    asBigInt() const;
    [[nodiscard]] Local<String>    asString() const;
    [[nodiscard]] Local<Symbol>    asSymbol() const;
    [[nodiscard]] Local<Object>    asObject() const;
    [[nodiscard]] Local<Array>     asArray() const;
    [[nodiscard]] Local<Function>  asFunction() const;

    /**
     * @tparam T must be the type of as described above
     */
    template <typename T>
        requires concepts::WrapType<T>
    [[nodiscard]] Local<T> as() const;

    void clear();
};

template <>
class Local<Null> {
    SPECIALIZATION_LOCAL(Null);
    SPECALIZATION_AS_VALUE(Null);
    SPECALIZATION_V8_LOCAL_TYPE(Null);
};

template <>
class Local<Undefined> {
    SPECIALIZATION_LOCAL(Undefined);
    SPECALIZATION_AS_VALUE(Undefined);
    SPECALIZATION_V8_LOCAL_TYPE(Undefined);
};

template <>
class Local<Boolean> {
    SPECIALIZATION_LOCAL(Boolean);
    SPECALIZATION_AS_VALUE(Boolean);
    SPECALIZATION_V8_LOCAL_TYPE(Boolean);

public:
    [[nodiscard]] bool getValue() const;
};

template <>
class Local<Number> {
    SPECIALIZATION_LOCAL(Number);
    SPECALIZATION_AS_VALUE(Number);
    SPECALIZATION_V8_LOCAL_TYPE(Number);

public:
    [[nodiscard]] int    getInt32() const;
    [[nodiscard]] float  getFloat() const;
    [[nodiscard]] double getDouble() const;

    template <typename T>
        requires std::is_integral_v<T> || std::is_floating_point_v<T>
    [[nodiscard]] T getValueAs() const {
        return static_cast<T>(getDouble());
    }
};

template <>
class Local<BigInt> {
    SPECIALIZATION_LOCAL(BigInt);
    SPECALIZATION_AS_VALUE(BigInt);
    SPECALIZATION_V8_LOCAL_TYPE(BigInt);

public:
    [[nodiscard]] int64_t  getInt64() const;
    [[nodiscard]] uint64_t getUint64() const;
};

template <>
class Local<String> {
    SPECIALIZATION_LOCAL(String);
    SPECALIZATION_AS_VALUE(String);
    SPECALIZATION_V8_LOCAL_TYPE(String);

public:
    [[nodiscard]] int         length() const;
    [[nodiscard]] std::string getValue() const;
};

template <>
class Local<Symbol> {
    SPECIALIZATION_LOCAL(Symbol);
    SPECALIZATION_AS_VALUE(Symbol);
    SPECALIZATION_V8_LOCAL_TYPE(Symbol);

public:
    Local<Value> getDescription(); // maybe undefined
};

template <>
class Local<Object> {
    SPECIALIZATION_LOCAL(Object);
    SPECALIZATION_AS_VALUE(Object);
    SPECALIZATION_V8_LOCAL_TYPE(Object);

    friend class Arguments;

public:
    [[nodiscard]] bool has(Local<String> const& key) const;

    [[nodiscard]] Local<Value> get(Local<String> const& key) const;

    void set(Local<String> const& key, Local<Value> const& value);

    void remove(Local<String> const& key);

    [[nodiscard]] std::vector<Local<String>> getOwnPropertyNames() const;

    [[nodiscard]] std::vector<std::string> getOwnPropertyNamesAsString() const;

    [[nodiscard]] bool instanceof(Local<Value> const& type) const;

    [[nodiscard]] bool defineOwnProperty(
        Local<String> const& key,
        Local<Value> const&  value,
        PropertyAttribute    attrs = PropertyAttribute::None
    ) const;

    [[nodiscard]] bool defineProperty(Local<String> const& key, PropertyDescriptor& desc) const;
};

template <>
class Local<Array> {
    SPECIALIZATION_LOCAL(Array);
    SPECALIZATION_AS_VALUE(Array);
    SPECALIZATION_V8_LOCAL_TYPE(Array);

public:
    [[nodiscard]] size_t length() const;

    [[nodiscard]] Local<Value> get(size_t index) const;

    void set(size_t index, Local<Value> const& value);

    void push(Local<Value> const& value);

    void clear();

    Local<Value> operator[](size_t index) const;
};

template <>
class Local<Function> {
    SPECIALIZATION_LOCAL(Function);
    SPECALIZATION_AS_VALUE(Function);
    SPECALIZATION_V8_LOCAL_TYPE(Function);

public:
    [[nodiscard]] bool isAsyncFunction() const; // JavaScript: async function

    [[nodiscard]] bool isConstructor() const; // JavaScript: class

    Local<Value> call(Local<Value> const& thiz) const;

    Local<Value> call(Local<Value> const& thiz, std::span<const Local<Value>> args) const;

    [[nodiscard]] Local<Value> callAsConstructor() const;

    [[nodiscard]] Local<Value> callAsConstructor(std::span<const Local<Value>> args) const;
};

#undef SPECIALIZATION_LOCAL
#undef SPECALIZATION_AS_VALUE
#undef SPECALIZATION_V8_LOCAL_TYPE


template <typename T>
class Global final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");

public:
    V8KIT_DISABLE_COPY(Global); // v8::Global is not copyable

    Global() noexcept; // empty

    explicit Global(Local<T> const& val);
    explicit Global(Weak<T> const& val);

    Global(Global<T>&& other) noexcept;
    Global& operator=(Global<T>&& other) noexcept;

    ~Global();

    [[nodiscard]] Local<T> get() const;

    [[nodiscard]] Local<Value> getValue() const;

    [[nodiscard]] bool isEmpty() const;

    void reset();

private:
    using v8Global = v8::Global<internal::V8Type_v<T>>;

    Engine*  engine_;
    v8Global handle_;

    friend Engine;
};

template <typename T>
class Weak final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");

public:
    V8KIT_DISABLE_COPY(Weak); // v8::Global is not copyable

    Weak() noexcept; // empty

    explicit Weak(Local<T> const& val);
    explicit Weak(Global<T> const& val);

    Weak(Weak<T>&& other) noexcept;
    Weak& operator=(Weak<T>&& other) noexcept;

    ~Weak();

    [[nodiscard]] Local<T> get() const;

    [[nodiscard]] Local<Value> getValue() const;

    [[nodiscard]] bool isEmpty() const;

    void reset();

private:
    using v8Global = v8::Global<internal::V8Type_v<T>>;

    Engine*  engine_ = nullptr;
    v8Global handle_;

    inline void markWeak();

    friend Engine;
};


} // namespace v8kit

#include "Reference.inl"