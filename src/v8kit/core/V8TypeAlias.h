#pragma once
#include "Fwd.h"
#include "v8kit/Macro.h"

V8KIT_WARNING_GUARD_BEGIN
#include <v8-function.h>
#include <v8-object.h>
#include <v8-primitive.h>
#include <v8-value.h>
V8KIT_WARNING_GUARD_END


namespace v8kit::internal {


template <typename T>
struct V8TypeAlias {
    static_assert(sizeof(T) == 0, "V8TypeAlias not defined for this type");
};

template <typename T>
using V8Type_v = typename V8TypeAlias<T>::type;


#define TYPE_ALIAS(WRAP_TYPE, V8_TYPE)                                                                                 \
    template <>                                                                                                        \
    struct V8TypeAlias<WRAP_TYPE> {                                                                                    \
        using type = V8_TYPE;                                                                                          \
    };

TYPE_ALIAS(Value, v8::Value);
TYPE_ALIAS(Null, v8::Primitive);
TYPE_ALIAS(Undefined, v8::Primitive);
TYPE_ALIAS(Boolean, v8::Boolean);
TYPE_ALIAS(Number, v8::Number);
TYPE_ALIAS(BigInt, v8::BigInt);
TYPE_ALIAS(String, v8::String);
TYPE_ALIAS(Symbol, v8::Symbol);
TYPE_ALIAS(Function, v8::Function);
TYPE_ALIAS(Object, v8::Object);
TYPE_ALIAS(Array, v8::Array);


#undef TYPE_ALIAS

} // namespace v8kit::internal