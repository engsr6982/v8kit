#pragma once
#include "Fwd.h"
#include "V8TypeAlias.h"
#include "v8kit/Macro.h"

#include <string>
#include <string_view>

V8KIT_WARNING_GUARD_BEGIN
#include <v8-function-callback.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {

enum class ValueKind : uint8_t {
    kUndefined,
    kNull,
    kBoolean,
    kNumber,
    kBigInt,
    kString,
    kSymbol,
    kObject,
    kArray,
    kFunction,
};

class Value {
public:
    Value() = delete;
};

class Null : public Value {
public:
    Null() = delete;
    [[nodiscard]] static Local<Null> newNull();
};

class Undefined : public Value {
public:
    Undefined() = delete;
    [[nodiscard]] static Local<Undefined> newUndefined();
};

class Boolean : public Value {
public:
    Boolean() = delete;
    [[nodiscard]] static Local<Boolean> newBoolean(bool b);
};

class Number : public Value {
public:
    Number() = delete;
    [[nodiscard]] static Local<Number> newNumber(double d);
    [[nodiscard]] static Local<Number> newNumber(int i);
    [[nodiscard]] static Local<Number> newNumber(float f);
};

class BigInt : public Value {
public:
    BigInt() = delete;

    [[nodiscard]] static Local<BigInt> newBigInt(int64_t i);

    [[nodiscard]] static Local<BigInt> newBigIntUnsigned(uint64_t i);
};

class String : public Value {
public:
    String() = delete;
    [[nodiscard]] static Local<String> newString(const char* str);
    [[nodiscard]] static Local<String> newString(std::string const& str);
    [[nodiscard]] static Local<String> newString(std::string_view sv);
};

class Symbol : public Value {
public:
    Symbol() = delete;
    [[nodiscard]] static Local<Symbol> newSymbol();
    [[nodiscard]] static Local<Symbol> newSymbol(std::string_view description);
    [[nodiscard]] static Local<Symbol> newSymbol(const char* description);
    [[nodiscard]] static Local<Symbol> newSymbol(std::string const& description);

    [[nodiscard]] static Local<Symbol> forKey(Local<String> const& str); // JavaScript: Symbol.for
};

class Function : public Value {
public:
    Function() = delete;

    [[nodiscard]] static Local<Function> newFunction(FunctionCallback&& callback);
};

class Object : public Value {
public:
    Object() = delete;
    [[nodiscard]] static Local<Object> newObject();
};

class Array : public Value {
public:
    Array() = delete;
    [[nodiscard]] static Local<Array> newArray(size_t length = 0);
};

class Engine; // forward declaration
class Arguments {
    Engine*                             engine_;
    v8::FunctionCallbackInfo<v8::Value> args_;

    explicit Arguments(Engine* engine, v8::FunctionCallbackInfo<v8::Value> const& args);

    friend class Engine;
    friend class Function;

public:
    V8KIT_DISABLE_COPY_MOVE(Arguments);

    [[nodiscard]] Engine* runtime() const;

    [[nodiscard]] bool hasThiz() const;

    [[nodiscard]] Local<Object> thiz() const; // this

    [[nodiscard]] size_t length() const;

    Local<Value> operator[](size_t index) const;
};


} // namespace v8kit