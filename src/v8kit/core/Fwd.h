#pragma once
#include <functional>

#include "v8kit/Macro.h"

V8KIT_WARNING_GUARD_BEGIN
#include <v8-function.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {

class Engine;
class Exception;
class EngineScope;
class ExitEngineScope;

enum class ValueKind : uint8_t;

class Value;
class Null;
class Undefined;
class Boolean;
class Number;
class BigInt;
class String;
class Symbol;
class Function;
class Object;
class Array;

class Arguments;

template <typename>
class Local;

template <typename>
class Global;

template <typename>
class Weak;

using PropertyAttribute  = v8::PropertyAttribute;
using PropertyDescriptor = v8::PropertyDescriptor;


using FunctionCallback = std::function<Local<Value>(Arguments const& args)>;
using GetterCallback   = std::function<Local<Value>()>;
using SetterCallback   = std::function<void(Local<Value> const& value)>;


struct InstancePayload;
class NativeInstance;
using ConstructorCallback    = std::function<std::unique_ptr<NativeInstance>(Arguments const& args)>;
using InstanceMethodCallback = std::function<Local<Value>(InstancePayload&, Arguments const& args)>;
using InstanceGetterCallback = std::function<Local<Value>(InstancePayload&, Arguments const& args)>;
using InstanceSetterCallback = std::function<void(InstancePayload&, Arguments const& args)>;


} // namespace v8kit

inline v8kit::PropertyAttribute operator|(v8kit::PropertyAttribute lhs, v8kit::PropertyAttribute rhs) {
    return static_cast<v8kit::PropertyAttribute>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}
inline v8kit::PropertyAttribute operator&(v8kit::PropertyAttribute lhs, v8kit::PropertyAttribute rhs) {
    return static_cast<v8kit::PropertyAttribute>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}