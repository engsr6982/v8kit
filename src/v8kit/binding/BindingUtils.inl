#pragma once
#include "TypeConverter.h"
#include "v8kit/core/Reference.h"


namespace v8kit::binding {


template <typename... Args>
Local<Value> call(Local<Function> const& func, Local<Value> const& thisArg, Args&&... args) {
    std::array<Local<Value>, sizeof...(Args)> argv = {toJs(std::forward<Args>(args))...};
    return func.call(thisArg, std::span<const Local<Value>>(argv));
}

template <typename... Args>
Local<Value> callAsConstructor(Local<Function> const& func, Args&&... args) {
    std::array<Local<Value>, sizeof...(Args)> argv = {toJs(std::forward<Args>(args))...};
    return func.callAsConstructor(std::span<const Local<Value>>(argv));
}


} // namespace v8kit::binding
