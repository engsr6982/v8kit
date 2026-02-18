#pragma once
#include "v8kit/core/Fwd.h"


namespace v8kit::binding {


template <typename... Args>
Local<Value> call(Local<Function> const& func, Local<Value> const& thisArg, Args&&... args);

template <typename... Args>
Local<Value> callAsConstructor(Local<Function> const& func, Args&&... args);


} // namespace v8kit::binding

#include "BindingUtils.inl" // NOLINT