#include "Reference.h"

#include "EngineScope.h"
#include "Exception.h"
#include "Value.h"
#include "v8kit/Macro.h"

#include <algorithm>
#include <stdexcept>


V8KIT_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-local-handle.h>
#include <v8-primitive.h>
#include <v8-value.h>
V8KIT_WARNING_GUARD_END

namespace v8kit {


// Local<Value>
Local<Value>::Local() noexcept : val(Undefined::newUndefined().val){}; // default constructor
bool Local<Value>::isNull() const { return !val.IsEmpty() && val->IsNull(); }
bool Local<Value>::isUndefined() const { return !val.IsEmpty() && val->IsUndefined(); }
bool Local<Value>::isNullOrUndefined() const { return !val.IsEmpty() && val->IsNullOrUndefined(); }
bool Local<Value>::isBoolean() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsBoolean(); }
bool Local<Value>::isNumber() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsNumber(); }
bool Local<Value>::isBigInt() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsBigInt(); }
bool Local<Value>::isString() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsString(); }
bool Local<Value>::isSymbol() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsSymbol(); }
bool Local<Value>::isObject() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsObject(); }
bool Local<Value>::isArray() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsArray(); }
bool Local<Value>::isFunction() const { return !val.IsEmpty() && !isNullOrUndefined() && val->IsFunction(); }

Local<Value> Local<Value>::asValue() const { return *this; }
Local<Null>  Local<Value>::asNull() const {
    if (isNull()) return Local<Null>{val.As<v8::Primitive>()};
    throw Exception("cannot convert to Null");
}
Local<Undefined> Local<Value>::asUndefined() const {
    if (isUndefined()) return Local<Undefined>{val.As<v8::Primitive>()};
    throw Exception("cannot convert to Undefined");
}
Local<Boolean> Local<Value>::asBoolean() const {
    if (isBoolean()) return Local<Boolean>{val.As<v8::Boolean>()};
    throw Exception("cannot convert to Boolean");
}
Local<Number> Local<Value>::asNumber() const {
    if (isNumber()) return Local<Number>{val.As<v8::Number>()};
    throw Exception("cannot convert to Number");
}
Local<BigInt> Local<Value>::asBigInt() const {
    if (isBigInt()) return Local<BigInt>{val.As<v8::BigInt>()};
    throw Exception("cannot convert to BigInt");
}
Local<String> Local<Value>::asString() const {
    if (isString()) return Local<String>{val.As<v8::String>()};
    throw Exception("cannot convert to String");
}
Local<Symbol> Local<Value>::asSymbol() const {
    if (isSymbol()) return Local<Symbol>{val.As<v8::Symbol>()};
    throw Exception("cannot convert to Symbol");
}
Local<Object> Local<Value>::asObject() const {
    if (isObject()) return Local<Object>{val.As<v8::Object>()};
    throw Exception("cannot convert to Object");
}
Local<Array> Local<Value>::asArray() const {
    if (isArray()) return Local<Array>{val.As<v8::Array>()};
    throw Exception("cannot convert to Array");
}
Local<Function> Local<Value>::asFunction() const {
    if (isFunction()) return Local<Function>{val.As<v8::Function>()};
    throw Exception("cannot convert to Function");
}

void Local<Value>::clear() { val.Clear(); }

ValueKind Local<Value>::kind() const {
    if (isNull()) return ValueKind::kNull;
    if (isUndefined()) return ValueKind::kUndefined;
    if (isBoolean()) return ValueKind::kBoolean;
    if (isNumber()) return ValueKind::kNumber;
    if (isBigInt()) return ValueKind::kBigInt;
    if (isString()) return ValueKind::kString;
    if (isSymbol()) return ValueKind::kSymbol;
    if (isObject()) return ValueKind::kObject;
    if (isArray()) return ValueKind::kArray;
    if (isFunction()) return ValueKind::kFunction;
    [[unlikely]] throw std::logic_error("Unknown type, did you forget to add if branch?");
}


#define IMPL_SPECIALIZATION_LOCAL(VALUE)                                                                               \
    Local<VALUE>::~Local() = default;                                                                                  \
    Local<VALUE>::Local(Local<VALUE> const& cp) : val(cp.val) {}                                                       \
    Local<VALUE>::Local(Local<VALUE>&& mv) noexcept : val(mv.val) { mv.val.Clear(); }                                  \
    Local<VALUE>& Local<VALUE>::operator=(Local const& cp) {                                                           \
        if (&cp != this) {                                                                                             \
            val = cp.val;                                                                                              \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<VALUE>& Local<VALUE>::operator=(Local&& mv) noexcept {                                                       \
        if (&mv != this) {                                                                                             \
            val = mv.val;                                                                                              \
            mv.val.Clear();                                                                                            \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<String> Local<VALUE>::toString() {                                                                           \
        if (asValue().isNull()) return String::newString("null");                                                      \
        if (asValue().isUndefined()) return String::newString("undefined");                                            \
        auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();                                        \
        v8::TryCatch vtry{isolate};                                                                                    \
        auto         maybe = val->ToString(ctx);                                                                       \
        return Local<String>{maybe.ToLocalChecked()};                                                                  \
    }                                                                                                                  \
    bool Local<VALUE>::operator==(Local<Value> const& other) const { return val->StrictEquals(other.val); }

#define IMPL_SPECALIZATION_AS_VALUE(VALUE)                                                                             \
    Local<Value> Local<VALUE>::asValue() const { return Local<Value>{val.As<v8::Value>()}; }

#define IMPL_SPECALIZATION_V8_LOCAL_TYPE(VALUE)                                                                        \
    Local<VALUE>::Local(v8::Local<Type> v8Type) : val{v8Type} {                                                        \
        if (val.IsEmpty()) throw Exception("Incorrect reference, v8::Local<T> is empty");                              \
    }


IMPL_SPECIALIZATION_LOCAL(Value)
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Value)

IMPL_SPECIALIZATION_LOCAL(Null);
IMPL_SPECALIZATION_AS_VALUE(Null);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Null);

IMPL_SPECIALIZATION_LOCAL(Undefined);
IMPL_SPECALIZATION_AS_VALUE(Undefined);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Undefined);

IMPL_SPECIALIZATION_LOCAL(Boolean);
IMPL_SPECALIZATION_AS_VALUE(Boolean);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Boolean);
bool Local<Boolean>::getValue() const { return val->Value(); }
Local<Boolean>::operator bool() const { return val->Value(); }

IMPL_SPECIALIZATION_LOCAL(Number);
IMPL_SPECALIZATION_AS_VALUE(Number);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Number);
int    Local<Number>::getInt32() const { return static_cast<int>(val->Value()); }
float  Local<Number>::getFloat() const { return static_cast<float>(val->Value()); }
double Local<Number>::getDouble() const { return val->Value(); }


IMPL_SPECIALIZATION_LOCAL(BigInt);
IMPL_SPECALIZATION_AS_VALUE(BigInt);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(BigInt);
int64_t  Local<BigInt>::getInt64() const { return val->Int64Value(/* lossless? */); }
uint64_t Local<BigInt>::getUint64() const { return val->Uint64Value(/* lossless? */); }


IMPL_SPECIALIZATION_LOCAL(String);
IMPL_SPECALIZATION_AS_VALUE(String);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(String);
int         Local<String>::length() const { return val->Length(); }
std::string Local<String>::getValue() const {
    auto                  isolate = EngineScope::currentRuntimeIsolateChecked();
    v8::String::Utf8Value utf8(isolate, val);
    if (*utf8 == nullptr) {
        throw Exception("Cannot convert v8::String to std::string");
    }
    return std::string{*utf8, static_cast<size_t>(utf8.length())};
}


IMPL_SPECIALIZATION_LOCAL(Symbol);
IMPL_SPECALIZATION_AS_VALUE(Symbol);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Symbol);
Local<Value> Local<Symbol>::getDescription() {
    auto isolate = EngineScope::currentRuntimeIsolateChecked();
    auto maybe   = val->Description(isolate);
    return Local<Value>{maybe};
}


IMPL_SPECIALIZATION_LOCAL(Object);
IMPL_SPECALIZATION_AS_VALUE(Object);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Object);
bool Local<Object>::has(Local<String> const& key) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    auto         maybe = val->Has(ctx, key.val);
    Exception::rethrow(vtry);
    return maybe.ToChecked();
}

Local<Value> Local<Object>::get(Local<String> const& key) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    auto         maybe = val->Get(ctx, key.val);
    Exception::rethrow(vtry);
    return Local<Value>{maybe.ToLocalChecked()};
}

void Local<Object>::set(Local<String> const& key, Local<Value> const& value) {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    (void)val->Set(ctx, key.val, value.val).ToChecked();
    Exception::rethrow(vtry);
}

void Local<Object>::remove(Local<String> const& key) {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    (void)val->Delete(ctx, key.val).ToChecked();
    Exception::rethrow(vtry);
}

std::vector<Local<String>> Local<Object>::getOwnPropertyNames() const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    auto maybe = val->GetOwnPropertyNames(ctx);
    Exception::rethrow(vtry);
    auto array = maybe.ToLocalChecked();

    std::vector<Local<String>> result;
    result.reserve(array->Length());

    for (uint32_t i = 0; i < array->Length(); ++i) {
        internal::V8EscapeScope scope{isolate};

        auto maybeVal = array->Get(ctx, i);
        Exception::rethrow(vtry);
        auto value = maybeVal.ToLocalChecked();
        if (value->IsString()) {
            result.push_back(Local<String>{scope.escape(value.As<v8::String>())});
        }
    }
    return result;
}

std::vector<std::string> Local<Object>::getOwnPropertyNamesAsString() const {
    auto keys = getOwnPropertyNames();

    std::vector<std::string> result;
    result.reserve(keys.size());
    std::transform(keys.begin(), keys.end(), std::back_inserter(result), [](Local<String> const& key) {
        return key.getValue();
    });
    return result;
}

bool Local<Object>::instanceof(Local<Value> const& type) const {
    if (!type.isObject()) {
        return false;
    }
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    auto maybe = val->InstanceOf(ctx, type.asObject().val);
    Exception::rethrow(vtry);
    return maybe.ToChecked();
}

bool Local<Object>::defineOwnProperty(
    Local<String> const& key,
    Local<Value> const&  value,
    PropertyAttribute    attrs
) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    auto maybe = val->DefineOwnProperty(ctx, key.val, value.val, attrs);
    Exception::rethrow(vtry);
    return maybe.ToChecked();
}

bool Local<Object>::defineProperty(Local<String> const& key, PropertyDescriptor& desc) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    auto maybe = val->DefineProperty(ctx, key.val, desc);
    Exception::rethrow(vtry);
    return maybe.ToChecked();
}


IMPL_SPECIALIZATION_LOCAL(Array);
IMPL_SPECALIZATION_AS_VALUE(Array);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Array);
size_t Local<Array>::length() const { return static_cast<size_t>(val->Length()); }

Local<Value> Local<Array>::get(size_t index) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    auto         maybe = val->Get(ctx, static_cast<uint32_t>(index));
    Exception::rethrow(vtry);
    return Local<Value>{maybe.ToLocalChecked()};
}

void Local<Array>::set(size_t index, Local<Value> const& value) {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    (void)val->Set(ctx, static_cast<uint32_t>(index), value.val).ToChecked();
    Exception::rethrow(vtry);
}

void Local<Array>::push(Local<Value> const& value) {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};
    (void)val->Set(ctx, val->Length(), value.val).ToChecked();
    Exception::rethrow(vtry);
}

void Local<Array>::clear() {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    // Method 1: Set length = 0
    auto len_str = v8::String::NewFromUtf8Literal(isolate, "length");
    (void)val->Set(ctx, len_str, v8::Integer::New(isolate, 0)).ToChecked();

    Exception::rethrow(vtry);
}

Local<Value> Local<Array>::operator[](size_t index) const { return get(index); }


IMPL_SPECIALIZATION_LOCAL(Function);
IMPL_SPECALIZATION_AS_VALUE(Function);
IMPL_SPECALIZATION_V8_LOCAL_TYPE(Function);
bool Local<Function>::isAsyncFunction() const { return val->IsAsyncFunction(); }

bool Local<Function>::isConstructor() const { return val->IsConstructor(); }

Local<Value> Local<Function>::call(Local<Value> const& thiz) const {
    return call(thiz, std::span<const Local<Value>>{});
}

Local<Value> Local<Function>::call(Local<Value> const& thiz, std::span<const Local<Value>> args) const {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    int argc = static_cast<int>(args.size());

    v8::Local<v8::Value>* argv = nullptr;
    if (!args.empty()) {
        static_assert(
            sizeof(Local<Value>) == sizeof(v8::Local<v8::Value>),
            "Local<Value> must be binary-compatible with v8::Local<v8::Value>"
        );
        argv = reinterpret_cast<v8::Local<v8::Value>*>(const_cast<Local<Value>*>(args.data()));
    }

    auto result = val->Call(ctx, thiz.val, argc, argv);
    Exception::rethrow(vtry);
    return Local<Value>{result.ToLocalChecked()};
}

Local<Value> Local<Function>::callAsConstructor() const { return callAsConstructor(std::span<const Local<Value>>{}); }

Local<Value> Local<Function>::callAsConstructor(std::span<const Local<Value>> args) const {
    if (!isConstructor()) {
        throw std::logic_error("Local<Function>::callAsConstructor called on non-constructor");
    }
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();
    v8::TryCatch vtry{isolate};

    int argc = static_cast<int>(args.size());

    v8::Local<v8::Value>* argv = nullptr;
    if (!args.empty()) {
        static_assert(
            sizeof(Local<Value>) == sizeof(v8::Local<v8::Value>),
            "Local<Value> must be binary-compatible with v8::Local<v8::Value>"
        );
        argv = reinterpret_cast<v8::Local<v8::Value>*>(const_cast<Local<Value>*>(args.data()));
    }

    auto result = val->CallAsConstructor(ctx, argc, argv);
    Exception::rethrow(vtry);
    return Local<Value>{result.ToLocalChecked()};
}


#undef IMPL_SPECALIZATION_LOCAL
#undef IMPL_SPECALIZATION_AS_VALUE
#undef IMPL_SPECALIZATION_V8_LOCAL_TYPE

} // namespace v8kit